#include "BrushEngine.h"
#include "brush/BrushModel.h"
#include "core/FastRounding.h"
#include <algorithm>
#include <cmath>

namespace komapaint {
namespace {
double clamp01(double n) { return std::clamp(n, 0.0, 1.0); }
unsigned scale(unsigned value, unsigned factor) { return (value * factor + 127U) / 255U; }
bool valid(InputSample p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.pressure) &&
           std::isfinite(p.speed);
}

// One place applies the Speed Dynamics factors. Size changes the footprint
// (and therefore the spacing step), opacity and flow scale the mark, and
// spacing changes how closely the marks are laid down. The multipliers are
// independent of pressure: a real stylus keeps its own pressure response.
BrushDefinition withSpeed(const BrushDefinition& brush, double speed) noexcept
{
    const input::SpeedFactors factors = input::speedFactors(brush.speed, speed);
    if (factors.neutral()) return brush;
    BrushDefinition adjusted = brush;
    adjusted.size = std::clamp(brush.size * factors.size, double(brush::MinBrushDiameter), double(brush::MaxBrushDiameter));
    adjusted.opacity = std::clamp(brush.opacity * factors.opacity, 0.0, 1.0);
    adjusted.flow = std::clamp(brush.flow * factors.flow, 0.0, 1.0);
    adjusted.spacing = std::clamp(brush.spacing * factors.spacing, 0.05, 1.0);
    return adjusted;
}

// The one place that turns pressure and tilt into a dab diameter. Both the
// rasterizer and the stroke's spacing use it, so the distance between stamps
// can never drift away from the size of the stamp being placed.
double effectiveDiameterFor(const BrushDefinition& b, const InputSample& p) noexcept
{
    const double pShaped = b.pressureSizeCurve
        ? clamp01(b.pressureSizeCurve->evaluate(clamp01(p.pressure)))
        : BrushRasterizer::shapedPressure(p.pressure, b.pressureGamma);
    const double pSizeWeight = clamp01(b.pressureSize);
    const double minScale = std::clamp(b.minSizeRatio, 0.05, 1.0);
    const double sizeScale = (1.0 - pSizeWeight) + pSizeWeight * (minScale + (1.0 - minScale) * pShaped);
    const double tiltMag = (p.hasTilt && std::isfinite(p.tiltX) && std::isfinite(p.tiltY))
        ? std::clamp(std::hypot(p.tiltX, p.tiltY) / 90.0, 0.0, 1.0)
        : 0.0;
    const double tiltScale = (p.hasTilt && b.tiltSize > 0.0)
        ? (1.0 + clamp01(b.tiltSize) * tiltMag * 1.5)
        : 1.0;
    return std::max(0.5, b.size * sizeScale * tiltScale);
}

bool clipAxis(double position, double delta, double lo, double hi, double& start, double& end) {
    if (delta == 0) return position >= lo && position <= hi;
    double a = (lo - position) / delta, b = (hi - position) / delta;
    if (a > b) std::swap(a, b);
    start = std::max(start, a); end = std::min(end, b);
    return start <= end;
}

std::uint32_t hash2d(std::int64_t x, std::int64_t y, std::uint32_t seed = 0)
{
    std::uint32_t n = static_cast<std::uint32_t>(x) * 0x8da6b343U
                    ^ static_cast<std::uint32_t>(y) * 0xd8163841U
                    ^ seed * 0x9e3779b9U;
    n ^= n >> 13;
    n *= 0x85ebca6bU;
    n ^= n >> 16;
    return n;
}

std::uint32_t straightToPremul(std::uint32_t straight)
{
    const unsigned a = (straight >> 24) & 255U;
    const unsigned r = scale((straight >> 16) & 255U, a);
    const unsigned g = scale((straight >> 8) & 255U, a);
    const unsigned b = scale(straight & 255U, a);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

std::uint32_t lerpPremul(std::uint32_t a, std::uint32_t b, double t)
{
    const double u = clamp01(t);
    const double inv = 1.0 - u;
    auto ch = [&](unsigned shift) -> unsigned {
        const double va = static_cast<double>((a >> shift) & 255U);
        const double vb = static_cast<double>((b >> shift) & 255U);
        const double blended = va * inv + vb * u + 0.5;
        return std::min(255U, static_cast<unsigned>(blended > 0.0 ? blended : 0.0));
    };
    const unsigned outA = ch(24);
    const unsigned outR = std::min(outA, ch(16));
    const unsigned outG = std::min(outA, ch(8));
    const unsigned outB = std::min(outA, ch(0));
    return (outA << 24) | (outR << 16) | (outG << 8) | outB;
}

struct BitmapSample {
    double coverage = 0;
    std::uint32_t colour = 0;
};

// UVs refer to the original image rectangle, not a procedural ellipse. Filter
// in premultiplied space so RGB in a transparent texel can't fringe a mark.
BitmapSample sampleBitmap(const brush::BrushResource& bitmap, double u, double v,
                          bool colour) noexcept {
    if (!(std::abs(u) < 1.0 && std::abs(v) < 1.0)) return {};
    const auto& bytes = bitmap.pixels();
    const double px = std::clamp((u + 1.0) * (bitmap.width * 0.5) - 0.5, 0.0, double(bitmap.width - 1));
    const double py = std::clamp((v + 1.0) * (bitmap.height * 0.5) - 0.5, 0.0, double(bitmap.height - 1));
    const int x0 = static_cast<int>(px), y0 = static_cast<int>(py);
    const int x1 = std::min(x0 + 1, bitmap.width - 1), y1 = std::min(y0 + 1, bitmap.height - 1);
    const double fx = px - x0, fy = py - y0;
    const bool rgba = bitmap.kind == brush::ResourceKind::Rgba8;
    double alpha = 0, red = 0, green = 0, blue = 0;
    auto add = [&](int x, int y, double weight) {
        const auto i = std::size_t(y) * std::size_t(bitmap.width) + std::size_t(x);
        if (!rgba) { alpha += bytes[i] * weight; return; }
        const auto* pixel = bytes.data() + i * 4;
        const double a = pixel[3] * weight;
        alpha += a;
        if (colour) { red += pixel[0] * a; green += pixel[1] * a; blue += pixel[2] * a; }
    };
    add(x0, y0, (1.0 - fx) * (1.0 - fy)); add(x1, y0, fx * (1.0 - fy));
    add(x0, y1, (1.0 - fx) * fy); add(x1, y1, fx * fy);
    if (alpha <= 0.0) return {};
    auto straight = [&](double c) { return static_cast<std::uint32_t>(std::clamp(c / alpha + 0.5, 0.0, 255.0)); };
    return {clamp01(alpha / 255.0), colour ? (0xff000000U | (straight(red) << 16) |
                                               (straight(green) << 8) | straight(blue)) : 0U};
}

// A texture layer repeats instead of fading to zero outside one tile. Bitmap
// tips and legacy masks keep the original non-repeating sample exactly.
BitmapSample sampleTexture(const brush::BrushResource& bitmap, double u, double v) noexcept {
    auto wrap = [](double value) {
        value = std::fmod(value + 1.0, 2.0);
        if (value < 0.0) value += 2.0;
        return value - 1.0;
    };
    const double wu = wrap(u);
    const double wv = wrap(v);
    if (bitmap.kind == brush::ResourceKind::Rgba8) {
        // A colour texture keeps its source pixels; the grain is its perceived
        // brightness, held back by transparency. Grayscale stays a direct read.
        const auto sample = sampleBitmap(bitmap, wu, wv, true);
        const unsigned rgb = sample.colour;
        const double luma = (54.0 * ((rgb >> 16) & 255U) + 183.0 * ((rgb >> 8) & 255U) +
                             19.0 * (rgb & 255U)) / 255.0;
        return {clamp01(sample.coverage * luma / 255.0), 0U};
    }
    return sampleBitmap(bitmap, wu, wv, false);
}

struct Batch {
    SparseTileStore& store;
    explicit Batch(SparseTileStore& s) : store(s) { s.beginBatch(); }
    ~Batch() { store.endBatch(); }
};
} // namespace

double BrushRasterizer::sensorOpacity(double pressure) { return clamp01(pressure); }

double BrushRasterizer::shapedPressure(double pressure, double gamma)
{
    const double p = clamp01(pressure);
    const double g = std::clamp(gamma, 0.25, 3.0);
    return std::pow(p, g);
}

double BrushRasterizer::tipCoverage(BrushTip tip, double dx, double dy, double radius,
                                    std::int64_t x, std::int64_t y, double hardness,
                                    double aspectRatio, double angleRadians,
                                    int bristleCount, double bristleSpread)
{
    const double aspect = std::clamp(aspectRatio, 1.0, 4.0);
    double lx = dx;
    double ly = dy;
    if (angleRadians != 0.0) {
        const double c = std::cos(-angleRadians);
        const double s = std::sin(-angleRadians);
        lx = dx * c - dy * s;
        ly = dx * s + dy * c;
    }
    const double sy = ly * aspect;
    const double dist = std::hypot(lx, sy);

    const double softness = 1.0 - clamp01(hardness);
    const double band = std::max(1.0, softness * (radius + 0.5));
    double coverage = clamp01((radius + 0.5 - dist) / band);
    if (softness > 0) coverage += softness * (coverage * coverage * (3.0 - 2.0 * coverage) - coverage);
    if (coverage <= 0.0) return 0.0;

    switch (tip) {
    case BrushTip::Round:
        return coverage;
    case BrushTip::Textured: {
        const std::uint32_t n = hash2d(x, y);
        return coverage * (0.25 + 0.75 * (n & 255U) / 255.0);
    }
    case BrushTip::Bristle: {
        // Multi-tuft bristle profile across the brush width (lx) with organic taper.
        const double normX = radius > 0.0 ? std::clamp(lx / radius, -1.0, 1.0) : 0.0;
        const int count = bristleCount > 0 ? std::min(bristleCount, 512) : 9;
        const double tuftWave = 0.5 + 0.5 * std::cos(normX * 3.14159265358979323846 * count);
        const std::uint32_t n = hash2d(x, y, 17U);
        const double micro = 0.55 + 0.45 * ((n & 255U) / 255.0);
        const double depth = std::clamp(0.68 + 0.22 * clamp01(bristleSpread), 0.0, 1.0);
        const double bristleMask = std::clamp(1.0 - depth * (1.0 - std::pow(tuftWave, 0.7) * micro), 0.0, 1.0);
        return coverage * bristleMask;
    }
    case BrushTip::Graphite: {
        // Paper-tooth graphite grain: crisp core with dry paper valleys on edges.
        const double toothA = (hash2d(x, y, 31U) & 255U) / 255.0;
        const double toothB = (hash2d(x / 2, y / 2, 53U) & 255U) / 255.0;
        const double grain = 0.65 * toothA + 0.35 * toothB;
        const double coreBoost = clamp01(coverage * 1.25);
        const double mask = std::clamp((0.28 + 0.72 * grain) * (0.6 + 0.4 * coreBoost), 0.0, 1.0);
        return coverage * mask;
    }
    case BrushTip::Stipple: {
        // Organic stipple / scatter texture anchored in document cells.
        const double cell = (hash2d(x / 2, y / 2, 79U) & 255U) / 255.0;
        const double fine = (hash2d(x, y, 101U) & 255U) / 255.0;
        const double dot = std::clamp((cell * 0.7 + fine * 0.3 - 0.25) * 1.6, 0.0, 1.0);
        return coverage * dot;
    }
    }
    return coverage;
}

std::uint32_t BrushRasterizer::deposit(std::uint32_t dst, std::uint32_t color, double amount)
{
    const auto alpha = roundHalfUpNonNegative(clamp01(amount) * ((color >> 24) & 255U));
    const auto inverse = 255U - alpha;
    auto channel = [&](unsigned shift) { return std::min(255U, scale((color >> shift) & 255U, alpha) + scale((dst >> shift) & 255U, inverse)); };
    const auto outAlpha = std::min(255U, alpha + scale((dst >> 24) & 255U, inverse));
    return (outAlpha << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

std::uint32_t BrushRasterizer::blend(std::uint32_t dst, std::uint32_t carryPremul, double amount)
{
    return lerpPremul(dst, carryPremul, amount);
}

std::uint32_t BrushRasterizer::erase(std::uint32_t dst, double amount)
{
    const auto keep = 255U - roundHalfUpNonNegative(clamp01(amount) * 255.0);
    auto channel = [&](unsigned shift) { return scale((dst >> shift) & 255U, keep); };
    return (channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

namespace {
// Source-over with a per-channel blend of the source colour into the existing
// paint. The source alpha still controls coverage, so a multiply component is
// a translucent multiply stamp rather than a fully opaque replacement.
std::uint32_t blendColour(std::uint32_t dst, std::uint32_t src, double amount,
                          brush::ComponentBlend mode) noexcept
{
    const auto alpha = roundHalfUpNonNegative(clamp01(amount) * ((src >> 24) & 255U));
    if (alpha == 0U) return dst;
    const unsigned dstA = (dst >> 24) & 255U;
    const unsigned inv = 255U - alpha;
    auto blended = [&](unsigned shift) -> unsigned {
        const double s = ((src >> shift) & 255U) / 255.0;
        const double d = ((dst >> shift) & 255U) / 255.0;
        double b = s;
        switch (mode) {
        case brush::ComponentBlend::Multiply: b = s * d; break;
        case brush::ComponentBlend::Screen: b = 1.0 - (1.0 - s) * (1.0 - d); break;
        case brush::ComponentBlend::Overlay:
            b = d < 0.5 ? 2.0 * s * d : 1.0 - 2.0 * (1.0 - s) * (1.0 - d);
            break;
        case brush::ComponentBlend::Normal:
        case brush::ComponentBlend::Height:
        case brush::ComponentBlend::Mask:
            b = s;
            break;
        }
        const double out = (b * 255.0 * alpha + d * 255.0 * inv) / 255.0;
        return static_cast<unsigned>(std::clamp(std::lround(out), 0L, 255L));
    };
    const unsigned outA = std::min(255U, alpha + scale(dstA, inv));
    return (outA << 24) | (std::min(outA, blended(16)) << 16) |
           (std::min(outA, blended(8)) << 8) | std::min(outA, blended(0));
}
} // namespace

double BrushRasterizer::effectiveDiameter(const BrushDefinition& brush, const InputSample& sample) noexcept
{
    return effectiveDiameterFor(brush, sample);
}

SparseTileStore::WriteResult BrushRasterizer::dab(SparseTileStore& store, const BrushDefinition& b,
                                                  InputSample p, double strokeAngle,
                                                  std::uint32_t carryPremul, bool hasCarry)
{
    using R = SparseTileStore::WriteResult;
    if (!valid(p) || !std::isfinite(b.size) || !std::isfinite(b.opacity) || !std::isfinite(b.flow)
        || (b.nativeAspect ? b.size <= 0.0 : b.size < 1.0) || b.opacity < 0 || b.opacity > 1 || b.flow < 0 || b.flow > 1
        || !std::isfinite(b.hardness) || b.hardness < 0 || b.hardness > 1
        || !std::isfinite(b.aspectRatio) || !std::isfinite(b.angleRadians)
        || !std::isfinite(b.pressureSize) || !std::isfinite(b.pressureOpacity)
        || !std::isfinite(b.pressureFlow) || !std::isfinite(b.pressureGamma)
        || !std::isfinite(b.minSizeRatio) || !std::isfinite(b.tiltSize)
        || !std::isfinite(b.blendStrength) || !std::isfinite(b.colorMix)) {
        return R::InvalidPixel;
    }
    const auto& g = store.geometry();
    if (!store.isValid()) return R::OutOfBounds;

    const double pShaped = shapedPressure(p.pressure, b.pressureGamma);
    const double pShapedOpacity = b.pressureOpacityCurve
        ? clamp01(b.pressureOpacityCurve->evaluate(clamp01(p.pressure))) : pShaped;
    const double pShapedFlow = b.pressureFlowCurve
        ? clamp01(b.pressureFlowCurve->evaluate(clamp01(p.pressure))) : pShaped;
    const double tiltMag = (p.hasTilt && std::isfinite(p.tiltX) && std::isfinite(p.tiltY))
        ? std::clamp(std::hypot(p.tiltX, p.tiltY) / 90.0, 0.0, 1.0)
        : 0.0;
    // Only a lower bound: a large brush paints whatever part of the canvas it
    // covers, which is clipped below. The old 256 px ceiling silently failed
    // every stroke drawn with a bigger tip.
    const double effectiveSize = effectiveDiameterFor(b, p);
    const double radius = effectiveSize / 2.0;

    const double pOpWeight = clamp01(b.pressureOpacity);
    const double pressureAlpha = (1.0 - pOpWeight) + pOpWeight * pShapedOpacity;
    const double pFlowWeight = clamp01(b.pressureFlow);
    const double pressureFlowScale = (1.0 - pFlowWeight) + pFlowWeight * pShapedFlow;

    const auto* bitmap = b.bitmapTip;
    if (bitmap && (!bitmap->expectedBytes() || bitmap->pixels().size() != bitmap->expectedBytes() ||
                   (b.bitmapColour && bitmap->kind != brush::ResourceKind::Rgba8))) return R::InvalidPixel;
    if (b.maskTip && (!b.maskTip->expectedBytes() || b.maskTip->pixels().size() != b.maskTip->expectedBytes()))
        return R::InvalidPixel;
    if (b.textureGates) {
        for (const auto& gate : *b.textureGates) {
            if (!gate.image || !gate.image->expectedBytes() ||
                gate.image->pixels().size() != gate.image->expectedBytes()) return R::InvalidPixel;
            if (!std::isfinite(gate.strength) || gate.strength < 0.0 || gate.strength > 1.0 ||
                !std::isfinite(gate.scale) || gate.scale <= 0.0 ||
                !std::isfinite(gate.rotationRadians) || !std::isfinite(gate.offsetX) ||
                !std::isfinite(gate.offsetY) || !std::isfinite(gate.contrast) ||
                gate.contrast < 0.0 || gate.contrast > 2.0 ||
                !std::isfinite(gate.variation) || gate.variation < 0.0 || gate.variation > 1.0)
                return R::InvalidPixel;
        }
    }
    if (!std::isfinite(b.maskScale) || b.maskScale <= 0.0 || !std::isfinite(b.maskRotationRadians) ||
        !std::isfinite(b.maskOffsetX) || !std::isfinite(b.maskOffsetY) || !std::isfinite(b.maskStrength) ||
        !std::isfinite(b.maskContrast) || !std::isfinite(b.maskDepth))
        return R::InvalidPixel;
    const double reach = bitmap ? radius * std::sqrt(2.0) : radius;
    if (!std::isfinite((reach + 0.5) * (reach + 0.5))) return R::InvalidPixel;
    const auto docBounds = g.rasterBounds(p.x - reach - 1, p.y - reach - 1,
                                          p.x + reach + 1, p.y + reach + 1);
    if (!docBounds) return R::Unchanged;
    const auto [docLeft, docTop, docRight, docBottom] = *docBounds;
    const auto tileRange = g.tilesForRect(*docBounds);
    if (!tileRange) return R::Unchanged;

    double totalAngle = b.angleRadians + (b.followDirection ? strokeAngle : 0.0);
    if (p.hasTilt && b.tiltAngle && tiltMag > 0.02) {
        totalAngle += std::atan2(p.tiltY, p.tiltX);
    }
    if (p.hasRotation && b.stylusRotation && std::isfinite(p.rotationRadians)) {
        totalAngle += p.rotationRadians;
    }
    if (!std::isfinite(totalAngle)) return R::InvalidPixel;
    const bool hasRotation = totalAngle != 0.0;
    const double cosA = hasRotation ? std::cos(-totalAngle) : 1.0;
    const double sinA = hasRotation ? std::sin(-totalAngle) : 0.0;
    if (b.nativeAspect && !(b.aspectRatio > 0.0)) return R::InvalidPixel;
    const double baseAspect = b.nativeAspect ? b.aspectRatio : std::clamp(b.aspectRatio, 1.0, 4.0);
    const double tiltedAspect = baseAspect * ((p.hasTilt && b.tiltAngle) ? (1.0 + tiltMag * 0.75) : 1.0);
    const double aspectY = b.nativeAspect ? std::max(1.0, tiltedAspect) : std::clamp(tiltedAspect, 1.0, 4.0);
    const double aspectX = b.nativeAspect ? std::max(1.0, 1.0 / tiltedAspect) : 1.0;
    if (!std::isfinite(aspectX) || !std::isfinite(aspectY)) return R::InvalidPixel;
    const double longSide = bitmap ? double(std::max(bitmap->width, bitmap->height)) : 1.0;
    const double bitmapX = bitmap ? (longSide / bitmap->width) * aspectX / radius : 0.0;
    const double bitmapY = bitmap ? (longSide / bitmap->height) * aspectY / radius : 0.0;
    if (!std::isfinite(bitmapX) || !std::isfinite(bitmapY)) return R::InvalidPixel;
    const double softness = 1.0 - clamp01(b.hardness);
    const double radiusPlusHalf = radius + 0.5;
    const double maxDistSq = radiusPlusHalf * radiusPlusHalf;
    const double invBand = 1.0 / std::max(1.0, softness * radiusPlusHalf);
    const double opacityFlowPressure = b.opacity * b.flow * pressureAlpha * pressureFlowScale;
    if (opacityFlowPressure <= 0.0) return R::Unchanged;

    struct ResolvedTexture {
        const brush::BrushResource* image = nullptr;
        double strength = 0.0;
        double scale = 1.0;
        double cosR = 1.0;
        double sinR = 0.0;
        double offsetX = 0.0;
        double offsetY = 0.0;
        double contrast = 1.0;
        bool invert = false;
        double jitterX = 0.0;
        double jitterY = 0.0;
        brush::TexturePlacement placement = brush::TexturePlacement::Tip;
        brush::ComponentBlend blend = brush::ComponentBlend::Multiply;
    };
    // Reused across dabs on the same stroke thread: a tablet stamp must not
    // allocate a fresh vector 100+ times a second. The contents are rebuilt
    // from the immutable stroke gates on every call.
    static thread_local std::vector<ResolvedTexture> textures;
    textures.clear();
    if (b.textureGates) {
        textures.reserve(b.textureGates->size());
        std::size_t layerIndex = 0;
        for (const auto& gate : *b.textureGates) {
            ResolvedTexture resolved;
            resolved.image = gate.image;
            resolved.strength = gate.strength;
            resolved.scale = gate.scale;
            resolved.offsetX = gate.offsetX;
            resolved.offsetY = gate.offsetY;
            resolved.contrast = gate.contrast;
            resolved.invert = gate.invert;
            resolved.placement = gate.placement;
            resolved.blend = gate.blend;
            const double angle = gate.rotationRadians +
                (gate.placement == brush::TexturePlacement::Stroke ? strokeAngle : 0.0);
            resolved.cosR = std::cos(-angle);
            resolved.sinR = std::sin(-angle);
            if (gate.variation > 0.0) {
                const auto x = hash2d(static_cast<std::int64_t>(layerIndex) * 2 + 1,
                                      static_cast<std::int64_t>(b.stampSeed), 91U);
                const auto y = hash2d(static_cast<std::int64_t>(layerIndex) * 2 + 2,
                                      static_cast<std::int64_t>(b.stampSeed), 137U);
                resolved.jitterX = ((x & 1023U) / 1023.0 - 0.5) * 2.0 * gate.variation;
                resolved.jitterY = ((y & 1023U) / 1023.0 - 0.5) * 2.0 * gate.variation;
            }
            textures.push_back(resolved);
            ++layerIndex;
        }
    }

    const double blendFactor = clamp01(b.blendStrength);
    const bool blending = !b.erase && blendFactor > 0.0;
    const std::uint32_t fgPremul = straightToPremul(b.color);
    const std::uint32_t effectiveCarry = hasCarry
        ? lerpPremul(carryPremul, fgPremul, clamp01(b.colorMix))
        : fgPremul;
    const unsigned colorAlpha = (b.color >> 24) & 255U;
    const unsigned colorR = (b.color >> 16) & 255U;
    const unsigned colorG = (b.color >> 8) & 255U;
    const unsigned colorB = b.color & 255U;
    const std::size_t tileStride = static_cast<std::size_t>(g.tileSize());

    // Inside this radius a hard tip is fully covered, so the square root is only
    // needed on the antialiased rim. The inner test is exact: (radiusPlusHalf -
    // dist) is at least 1 there, which clamp01 turns into exactly 1.
    const double hardInnerSq = softness > 0.0 ? -1.0
        : (radiusPlusHalf - 1.0 > 0.0 ? (radiusPlusHalf - 1.0) * (radiusPlusHalf - 1.0) : -1.0);

    auto computeCoverage = [&](double dx, double dy, std::int64_t x, std::int64_t y) noexcept -> double {
        double lx = dx;
        double ly = dy;
        if (hasRotation) {
            lx = dx * cosA - dy * sinA;
            ly = dx * sinA + dy * cosA;
        }
        const double sx = lx * aspectX;
        const double sy = ly * aspectY;
        const double distSq = sx * sx + sy * sy;
        if (distSq >= maxDistSq) return 0.0;
        double coverage;
        if (distSq <= hardInnerSq) {
            coverage = 1.0;
        } else {
            const double dist = std::sqrt(distSq);
            coverage = clamp01((radiusPlusHalf - dist) * invBand);
            if (softness > 0.0) coverage += softness * (coverage * coverage * (3.0 - 2.0 * coverage) - coverage);
        }
        if (coverage <= 0.0) return 0.0;

        switch (b.tip) {
        case BrushTip::Round:
            return coverage;
        case BrushTip::Textured: {
            const std::uint32_t n = hash2d(x, y);
            return coverage * (0.25 + 0.75 * (n & 255U) / 255.0);
        }
        case BrushTip::Bristle: {
            const double normX = radius > 0.0 ? std::clamp(lx / radius, -1.0, 1.0) : 0.0;
            const int count = b.bristleCount > 0 ? std::min(b.bristleCount, 512) : 9;
            const double tuftWave = 0.5 + 0.5 * std::cos(normX * 3.14159265358979323846 * count);
            const std::uint32_t n = hash2d(x, y, 17U);
            const double micro = 0.55 + 0.45 * ((n & 255U) / 255.0);
            const double depth = std::clamp(0.68 + 0.22 * clamp01(b.bristleSpread), 0.0, 1.0);
            const double bristleMask = std::clamp(1.0 - depth * (1.0 - std::pow(tuftWave, 0.7) * micro), 0.0, 1.0);
            return coverage * bristleMask;
        }
        case BrushTip::Graphite: {
            const double toothA = (hash2d(x, y, 31U) & 255U) / 255.0;
            const double toothB = (hash2d(x / 2, y / 2, 53U) & 255U) / 255.0;
            const double grain = 0.65 * toothA + 0.35 * toothB;
            const double coreBoost = clamp01(coverage * 1.25);
            const double mask = std::clamp((0.28 + 0.72 * grain) * (0.6 + 0.4 * coreBoost), 0.0, 1.0);
            return coverage * mask;
        }
        case BrushTip::Stipple: {
            const double cell = (hash2d(x / 2, y / 2, 79U) & 255U) / 255.0;
            const double fine = (hash2d(x, y, 101U) & 255U) / 255.0;
            const double dot = std::clamp((cell * 0.7 + fine * 0.3 - 0.25) * 1.6, 0.0, 1.0);
            return coverage * dot;
        }
        }
        return coverage;
    };

    Batch batch(store);
    bool changed = false;
    for (auto ty = tileRange->first.y; ty <= tileRange->last.y; ++ty) {
        for (auto tx = tileRange->first.x; tx <= tileRange->last.x; ++tx) {
            const TilePoint tile{tx, ty};
            const auto bounds = g.tileBounds(tile);
            if (!bounds) continue;
            const std::int64_t x0 = std::max(docLeft, bounds->left);
            const std::int64_t y0 = std::max(docTop, bounds->top);
            const std::int64_t x1 = std::min(docRight, bounds->right);
            const std::int64_t y1 = std::min(docBottom, bounds->bottom);
            if (x0 >= x1 || y0 >= y1) continue;

            const auto readSpan = store.tilePixels(tile);
            if (b.erase && readSpan.empty()) continue;
            const std::uint32_t* readPtr = readSpan.empty() ? nullptr : readSpan.data();

            std::uint32_t* writePtr = nullptr;
            std::size_t* nonzeroPtr = nullptr;
            bool tileCreated = false;
            bool tileChanged = false;
            const std::int64_t tileOriginX = bounds->left;
            const std::int64_t tileOriginY = bounds->top;

            for (std::int64_t y = y0; y < y1; ++y) {
                const double dy = static_cast<double>(y) + 0.5 - p.y;
                const std::size_t lyOffset = static_cast<std::size_t>(y - tileOriginY) * tileStride;
                // Horizontal extent of the rotated ellipse on this row. A rotated
                // tip keeps the full row because its span needs the general
                // quadratic; the unrotated case covers nearly every brush.
                std::int64_t rowBegin = x0, rowEnd = x1;
                if (!bitmap && !hasRotation) {
                    const double axis = dy * aspectY;
                    const double remaining = maxDistSq - axis * axis;
                    if (remaining <= 0.0) continue;
                    const double halfSpan = std::sqrt(remaining) / aspectX;
                    // One extra pixel on each side keeps the exact coverage test
                    // inside the loop authoritative.
                    const double lo = p.x - halfSpan - 0.5;
                    const double hi = p.x + halfSpan - 0.5;
                    rowBegin = clampPixelEdge(std::ceil(lo) - 1.0, x0, x1);
                    rowEnd = clampPixelEdge(std::floor(hi) + 2.0, x0, x1);
                }
                for (std::int64_t x = rowBegin; x < rowEnd; ++x) {
                    const double dx = static_cast<double>(x) + 0.5 - p.x;
                    BitmapSample bitmapSample;
                    double coverage;
                    if (bitmap) {
                        const double lx = dx * cosA - dy * sinA;
                        const double ly = dx * sinA + dy * cosA;
                        bitmapSample = sampleBitmap(*bitmap, lx * (b.flipX ? -bitmapX : bitmapX),
                                                    ly * (b.flipY ? -bitmapY : bitmapY), b.bitmapColour);
                        coverage = bitmapSample.coverage;
                    } else {
                        coverage = computeCoverage(dx, dy, x, y);
                    }
                    if (coverage <= 0.0) continue;
                    // Antialiasing is the coverage gradient itself; turning it
                    // off quantises the same footprint to whole pixels so an
                    // artist can deliberately paint a hard, pixel-crisp edge.
                    if (!b.antialias) coverage = coverage >= 0.5 ? 1.0 : 0.0;
                    if (coverage <= 0.0) continue;
                    if (b.maskTip) {
                        // The authored mask lives in the same local tip space.
                        // Its own scale/rotation/offset are applied here, not by
                        // moving the mark: a grain stays over the tip even when
                        // the component itself is scattered.
                        const double c = std::cos(-b.maskRotationRadians);
                        const double s = std::sin(-b.maskRotationRadians);
                        const double localX = dx - b.maskOffsetX * radius;
                        const double localY = dy - b.maskOffsetY * radius;
                        const double mx = (localX * c - localY * s) / (radius * b.maskScale);
                        const double my = (localX * s + localY * c) / (radius * b.maskScale);
                        double gate = clamp01(sampleBitmap(*b.maskTip, mx, my, false).coverage);
                        if (b.maskInvert) gate = 1.0 - gate;
                        // Contrast hardens or softens the grain around mid-grey;
                        // depth decides how fully dark grain removes paint.
                        const double contrast = std::clamp(b.maskContrast, 0.0, 2.0);
                        gate = clamp01(0.5 + (gate - 0.5) * contrast);
                        gate = 1.0 - clamp01(b.maskDepth) * (1.0 - gate);
                        gate = 1.0 - clamp01(b.maskStrength) * (1.0 - gate);
                        coverage *= clamp01(gate);
                        if (coverage <= 0.0) continue;
                    }
                    for (const auto& texture : textures) {
                        double u = 0.0, v = 0.0;
                        if (texture.placement == brush::TexturePlacement::Canvas) {
                            const double gx = static_cast<double>(x) + 0.5 + texture.offsetX * radius;
                            const double gy = static_cast<double>(y) + 0.5 + texture.offsetY * radius;
                            u = (gx * texture.cosR - gy * texture.sinR) / (radius * texture.scale);
                            v = (gx * texture.sinR + gy * texture.cosR) / (radius * texture.scale);
                        } else if (texture.placement == brush::TexturePlacement::Stroke) {
                            const double gx = static_cast<double>(x) + 0.5 - b.strokeOriginX +
                                              texture.offsetX * radius;
                            const double gy = static_cast<double>(y) + 0.5 - b.strokeOriginY +
                                              texture.offsetY * radius;
                            u = (gx * texture.cosR - gy * texture.sinR) / (radius * texture.scale);
                            v = (gx * texture.sinR + gy * texture.cosR) / (radius * texture.scale);
                        } else {
                            const double gx = dx - texture.offsetX * radius;
                            const double gy = dy - texture.offsetY * radius;
                            u = (gx * texture.cosR - gy * texture.sinR) / (radius * texture.scale);
                            v = (gx * texture.sinR + gy * texture.cosR) / (radius * texture.scale);
                        }
                        double gate = sampleTexture(*texture.image, u + texture.jitterX,
                                                    v + texture.jitterY).coverage;
                        if (texture.invert) gate = 1.0 - gate;
                        gate = clamp01(0.5 + (gate - 0.5) * texture.contrast);
                        double strength = texture.strength;
                        // Pressure changes texture only when the device actually
                        // supplied pressure; a mouse is never treated as a stylus.
                        if (b.textureGates && p.hasPressure) {
                            const auto& authored = (*b.textureGates)[
                                static_cast<std::size_t>(&texture - textures.data())];
                            if (authored.pressureStrength)
                                strength *= 0.35 + 0.65 * clamp01(p.pressure);
                        }
                        switch (texture.blend) {
                        case brush::ComponentBlend::Screen:
                            gate = 1.0 - strength * gate;
                            break;
                        case brush::ComponentBlend::Overlay: {
                            const double curve = gate < 0.5
                                ? 2.0 * gate * gate
                                : 1.0 - 2.0 * (1.0 - gate) * (1.0 - gate);
                            gate = 1.0 - strength * (1.0 - curve);
                            break;
                        }
                        case brush::ComponentBlend::Normal:
                            gate = 1.0 - strength * (1.0 - gate) * 0.65;
                            break;
                        case brush::ComponentBlend::Multiply:
                        default:
                            gate = 1.0 - strength * (1.0 - gate);
                            break;
                        }
                        coverage *= clamp01(gate);
                        if (coverage <= 0.0) break;
                    }
                    if (coverage <= 0.0) continue;
                    if (b.pixelMask) {
                        coverage *= clamp01(b.pixelMask(b.pixelMaskContext, x, y));
                        if (coverage <= 0.0) continue;
                    }
                    const std::size_t idx = lyOffset + static_cast<std::size_t>(x - tileOriginX);
                    const std::uint32_t old = writePtr ? writePtr[idx] : (readPtr ? readPtr[idx] : 0U);
                    const double amount = coverage * opacityFlowPressure;
                    std::uint32_t next = old;
                    if (b.erase) {
                        if (old == 0U) continue;
                        next = erase(old, amount);
                    } else if (bitmap && b.bitmapColour) {
                        next = b.blend == brush::ComponentBlend::Normal
                            ? deposit(old, bitmapSample.colour, amount)
                            : blendColour(old, bitmapSample.colour, amount, b.blend);
                    } else if (b.blend != brush::ComponentBlend::Normal) {
                        next = blendColour(old, b.color, amount, b.blend);
                    } else if (blending) {
                        const double mixAmt = clamp01(amount * blendFactor);
                        next = blend(old, effectiveCarry, mixAmt);
                    } else {
                        const auto alpha = static_cast<unsigned>(clamp01(amount) * colorAlpha + 0.5);
                        if (alpha == 0U) continue;
                        if (alpha == 255U && old == 0U) {
                            next = (255U << 24) | (colorR << 16) | (colorG << 8) | colorB;
                        } else if (alpha == 255U) {
                            next = (255U << 24) | (colorR << 16) | (colorG << 8) | colorB;
                        } else {
                            const unsigned inverse = 255U - alpha;
                            const unsigned outA = std::min(255U, alpha + scale((old >> 24) & 255U, inverse));
                            const unsigned outR = std::min(255U, scale(colorR, alpha) + scale((old >> 16) & 255U, inverse));
                            const unsigned outG = std::min(255U, scale(colorG, alpha) + scale((old >> 8) & 255U, inverse));
                            const unsigned outB = std::min(255U, scale(colorB, alpha) + scale(old & 255U, inverse));
                            next = (outA << 24) | (outR << 16) | (outG << 8) | outB;
                        }
                    }
                    if (next == old) continue;
                    if (!writePtr) {
                        const auto editRes = store.beginTileEdit(tile, {x0, y0, x1, y1},
                                                                 writePtr, nonzeroPtr, tileCreated);
                        if (editRes != R::Changed) return editRes;
                    }
                    if (old == 0U) ++(*nonzeroPtr);
                    if (next == 0U) --(*nonzeroPtr);
                    writePtr[idx] = next;
                    tileChanged = true;
                }
            }
            if (writePtr) {
                store.endTileEdit(tile, tileChanged);
                if (tileChanged) changed = true;
            }
        }
    }
    return changed ? R::Changed : R::Unchanged;
}

BrushStroke::BrushStroke(SparseTileStore& store, BrushDefinition brush)
    : m_store(store), m_brush(brush) {
    if (!(std::isfinite(brush.size) && std::isfinite(brush.spacing)
          && brush.size >= brush::MinBrushDiameter && brush.size <= brush::MaxBrushDiameter
          && brush.spacing >= 0.05 && brush.spacing <= 0.5)) {
        m_failed = true;
        m_failureReason = SparseTileStore::WriteResult::InvalidPixel;
    }
    m_randomState = brush.randomSeed ? brush.randomSeed : 1u;
}

BrushStroke::BrushStroke(SparseTileStore& store, BrushDefinition brush, StampSink sink,
                         void* context)
    : m_store(store), m_brush(brush), m_sink(sink), m_sinkContext(context) {
    if (!(std::isfinite(brush.size) && std::isfinite(brush.spacing)
          && brush.size >= brush::MinBrushDiameter && brush.size <= brush::MaxBrushDiameter
          && brush.spacing >= 0.05 && brush.spacing <= 0.5)) {
        m_failed = true;
        m_failureReason = SparseTileStore::WriteResult::InvalidPixel;
    }
    m_randomState = brush.randomSeed ? brush.randomSeed : 1u;
}

double BrushStroke::spacingFor(const InputSample& sample) const
{
    // Spacing is a fraction of the diameter that will actually be stamped at
    // this point. Pressure-to-size and tilt therefore shorten or lengthen the
    // step as the input changes, instead of leaving a nominal-size gap.
    const BrushDefinition adjusted = withSpeed(m_brush, sample.speed);
    const double diameter = BrushRasterizer::effectiveDiameter(adjusted, sample);
    if (!(std::isfinite(diameter) && diameter > 0.0)) return 0.0;
    // Existing lower bound, kept only as a guard against a zero step. The
    // artist's percentage is otherwise used exactly as stored.
    return std::max(0.25, diameter * adjusted.spacing);
}

void BrushStroke::updateCarry(InputSample p)
{
    if (m_brush.erase || m_brush.blendStrength <= 0.0) return;
    const auto& g = m_store.geometry();
    const double step = std::max(1.0, m_brush.size * 0.22);
    const double ox[5] = {0.0, -step, step, 0.0, 0.0};
    const double oy[5] = {0.0, 0.0, 0.0, -step, step};
    double sumA = 0.0, sumR = 0.0, sumG = 0.0, sumB = 0.0;
    int count = 0;
    for (int i = 0; i < 5; ++i) {
        const double sx = std::round(p.x + ox[i]);
        const double sy = std::round(p.y + oy[i]);
        if (!(sx >= 0 && sy >= 0 && sx < double(g.width()) && sy < double(g.height()))) continue;
        const auto px = m_store.pixel({static_cast<std::int64_t>(sx), static_cast<std::int64_t>(sy)}).value();
        sumA += static_cast<double>((px >> 24) & 255U);
        sumR += static_cast<double>((px >> 16) & 255U);
        sumG += static_cast<double>((px >> 8) & 255U);
        sumB += static_cast<double>(px & 255U);
        ++count;
    }
    if (count == 0) return;
    const unsigned a = static_cast<unsigned>(std::clamp(std::lround(sumA / count), 0L, 255L));
    const unsigned r = std::min(a, static_cast<unsigned>(std::clamp(std::lround(sumR / count), 0L, 255L)));
    const unsigned gg = std::min(a, static_cast<unsigned>(std::clamp(std::lround(sumG / count), 0L, 255L)));
    const unsigned b = std::min(a, static_cast<unsigned>(std::clamp(std::lround(sumB / count), 0L, 255L)));
    const std::uint32_t sampled = (a << 24) | (r << 16) | (gg << 8) | b;
    if (!m_hasCarry) {
        m_carryPremul = sampled;
        m_hasCarry = true;
    } else {
        const double pickup = std::clamp(0.35 + 0.40 * (1.0 - clamp01(m_brush.blendStrength)), 0.15, 0.75);
        m_carryPremul = lerpPremul(m_carryPremul, sampled, pickup);
    }
}

bool BrushStroke::stamp(InputSample p, double taper) {
    if (taper <= 0.01) return true;   // the very tip of a taper paints nothing
    updateCarry(p);
    BrushDefinition placed = withSpeed(m_brush, p.speed);
    if (taper < 1.0) placed.size = std::max(1.0, placed.size * taper);
    const auto result = m_sink
        ? m_sink(m_store, placed, p, m_strokeAngle, m_carryPremul, m_hasCarry, m_sinkContext)
        : BrushRasterizer::dab(m_store, placed, p, m_strokeAngle, m_carryPremul, m_hasCarry);
    if (result != SparseTileStore::WriteResult::Changed && result != SparseTileStore::WriteResult::Unchanged) {
        m_failed = true;
        m_failureReason = result;
    }
    return !m_failed;
}

double BrushStroke::taperInAt(double travelled) const {
    if (!(m_taperDistanceIn > 0.0)) return 1.0;
    const double t = std::clamp(travelled / m_taperDistanceIn, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double BrushStroke::randomUnit() {
    // splitmix32: small, deterministic and independent of the standard
    // library's distribution implementation.
    m_randomState += 0x9e3779b9u;
    std::uint32_t z = m_randomState;
    z = (z ^ (z >> 16)) * 0x85ebca6bu;
    z = (z ^ (z >> 13)) * 0xc2b2ae35u;
    z ^= z >> 16;
    return double(z >> 8) * (1.0 / 16777216.0);
}

void BrushStroke::place(InputSample p, double travelled) {
    if (m_taperDistanceOut > 0.0 || m_delayDistance > 0.0) {
        m_pending.push_back({p, travelled});
        return;
    }
    stamp(p, taperInAt(travelled));
}

bool BrushStroke::flushPending(bool final) {
    while (!m_pending.empty()) {
        const double hold = std::max(m_taperDistanceOut, m_delayDistance);
        if (!final && m_travelled - m_pending.front().travelled <= hold + 1.0e-9)
            break;
        const PendingStamp next = m_pending.front();
        m_pending.pop_front();
        double taper = taperInAt(next.travelled);
        if (m_taperDistanceOut > 0.0) {
            const double remaining = std::max(0.0, m_travelled - next.travelled);
            const double out = remaining >= m_taperDistanceOut
                ? 1.0 : std::clamp(remaining / m_taperDistanceOut, 0.0, 1.0);
            const double shaped = out * out * (3.0 - 2.0 * out);
            taper = std::min(taper, shaped);
        }
        if (!stamp(next.sample, taper)) return false;
    }
    return true;
}

bool BrushStroke::begin(InputSample p) {
    if (m_started || m_failed || !valid(p)) {
        if (!m_failed) { m_failed = true; m_failureReason = SparseTileStore::WriteResult::InvalidPixel; }
        return false;
    }
    m_started = true;
    m_previous = p;
    m_remaining = spacingFor(p);
    if (m_brush.spacingJitter > 0.0) {
        m_remaining = std::max(0.25, m_remaining *
            (1.0 + std::clamp(m_brush.spacingJitter, 0.0, 1.0) * (randomUnit() * 2.0 - 1.0)));
    }
    m_travelled = 0.0;
    m_taperDistanceIn = std::max(0.0, std::min(1.0, m_brush.taperIn)) * m_brush.size * 4.0;
    m_taperDistanceOut = std::max(0.0, std::min(1.0, m_brush.taperOut)) * m_brush.size * 4.0;
    m_delayDistance = std::max(0.0, std::min(1.0, m_brush.strokeDelay)) * m_brush.size * 4.0;
    m_pending.clear();
    m_hasAngle = false;
    m_hasCarry = false;
    m_endpointStamped = false;
    m_moved = false;
    if (!(m_remaining > 0.0)) {
        m_failed = true;
        m_failureReason = SparseTileStore::WriteResult::InvalidPixel;
        return false;
    }
    place(p, 0.0);
    return !m_failed;
}

bool BrushStroke::extend(InputSample p) {
    if (!m_started || m_failed || !valid(p)) {
        if (!m_failed) { m_failed = true; m_failureReason = SparseTileStore::WriteResult::InvalidPixel; }
        return false;
    }
    const double dx = p.x - m_previous.x, dy = p.y - m_previous.y;
    const double distance = std::hypot(dx, dy);
    if (!std::isfinite(distance)) { m_failed = true; m_failureReason = SparseTileStore::WriteResult::InvalidPixel; return false; }
    if (distance == 0) { m_previous = p; return true; }
    const double segmentStart = m_travelled;

    if (m_brush.followDirection && distance >= 0.5) {
        const double targetAngle = std::atan2(dy, dx);
        if (!m_hasAngle) {
            m_strokeAngle = targetAngle;
            m_hasAngle = true;
        } else {
            double diff = targetAngle - m_strokeAngle;
            while (diff > 3.14159265358979323846) diff -= 6.28318530717958647692;
            while (diff < -3.14159265358979323846) diff += 6.28318530717958647692;
            m_strokeAngle += diff * 0.45;
        }
    }

    m_endpointStamped = false;
    m_moved = true;

    double start = 0, end = 1;
    const double tiltReach = (p.hasTilt || m_previous.hasTilt)
        ? 1.0 + clamp01(m_brush.tiltSize) * 1.5 : 1.0;
    const double radius = m_brush.size * 0.5 * tiltReach * m_brush.stampReach + 1.0;
    if (!std::isfinite(radius)) { m_failed = true; m_failureReason = SparseTileStore::WriteResult::InvalidPixel; return false; }
    const auto& g = m_store.geometry();
    const bool visible = clipAxis(m_previous.x, dx, -radius, static_cast<double>(g.width()) + radius, start, end) &&
                         clipAxis(m_previous.y, dy, -radius, static_cast<double>(g.height()) + radius, start, end);
    if (!visible) {
        // Nothing on this segment can touch the document. Drop the phase so the
        // next visible segment starts with a dab on its first pixel rather than
        // inheriting an unknowable number of off-canvas steps.
        m_remaining = 0.0;
        m_travelled = segmentStart + distance;
        m_previous = p;
        return true;
    }

    const double first = start * distance;
    const double last = std::min(end * distance, distance);
    if (m_remaining < first) {
        // A long clipped prefix must not be walked stamp by stamp: the phase is
        // irrelevant off-canvas, and starting at the entry point guarantees the
        // visible edge is inked. Inside the document the phase is preserved.
        m_remaining = first;
    }

    double at = m_remaining;
    while (at <= last) {
        const double t = at / distance;
        InputSample interp;
        interp.x = m_previous.x + dx * t;
        interp.y = m_previous.y + dy * t;
        interp.pressure = m_previous.pressure + (p.pressure - m_previous.pressure) * t;
        interp.tiltX = m_previous.tiltX + (p.tiltX - m_previous.tiltX) * t;
        interp.tiltY = m_previous.tiltY + (p.tiltY - m_previous.tiltY) * t;
        interp.rotationRadians = m_previous.rotationRadians + (p.rotationRadians - m_previous.rotationRadians) * t;
        interp.tangentialPressure = m_previous.tangentialPressure + (p.tangentialPressure - m_previous.tangentialPressure) * t;
        interp.speed = m_previous.speed + (p.speed - m_previous.speed) * t;
        interp.hasTilt = m_previous.hasTilt || p.hasTilt;
        interp.hasRotation = m_previous.hasRotation || p.hasRotation;
        place(interp, segmentStart + at);
        if (m_failed) return false;
        if (at >= distance - 1.0e-9) m_endpointStamped = true;
        if (at >= last) break;

        // The next step is decided by the dab just placed, which is what keeps
        // the spacing/diameter ratio constant through a pressure transition.
        double step = spacingFor(interp);
        if (m_brush.spacingJitter > 0.0) {
            step = std::max(0.25, step *
                (1.0 + std::clamp(m_brush.spacingJitter, 0.0, 1.0) * (randomUnit() * 2.0 - 1.0)));
        }
        if (!(step > 0.0) || !std::isfinite(step)) {
            m_failed = true;
            m_failureReason = SparseTileStore::WriteResult::InvalidPixel;
            return false;
        }
        const double next = at + step;
        if (!(next > at)) {
            m_failed = true;
            m_failureReason = SparseTileStore::WriteResult::InvalidPixel;
            return false;
        }
        at = next;
    }

    // If the visible range ended before the segment did, the remaining phase is
    // outside the document; reset it for the next entry. If the whole segment
    // was visible, keep the exact distance to the next stamp.
    m_remaining = (last >= distance - 1.0e-9) ? at - distance : 0.0;
    m_travelled = segmentStart + distance;
    m_previous = p;
    if (!flushPending(false)) return false;
    return true;
}

bool BrushStroke::end(InputSample p) {
    if (!extend(p)) return false;
    m_started = false;
    // Finish the tail with the pointer's final sample unless the last extend
    // already placed a stamp there. This preserves the old "no duplicate dab"
    // behaviour for stationary strokes.
    if (m_moved && !m_endpointStamped && m_remaining > 1.0e-9) place(p, m_travelled);
    if (m_taperDistanceOut > 0.0 || m_delayDistance > 0.0) return flushPending(true);
    return true;
}
} // namespace komapaint
