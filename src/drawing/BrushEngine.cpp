#include "BrushEngine.h"
#include <algorithm>
#include <cmath>

namespace komapaint {
namespace {
double clamp01(double n) { return std::clamp(n, 0.0, 1.0); }
unsigned scale(unsigned value, unsigned factor) { return (value * factor + 127U) / 255U; }
bool valid(InputSample p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.pressure); }

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
                                    double aspectRatio, double angleRadians)
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
        const double tuftWave = 0.5 + 0.5 * std::cos(normX * 3.14159265358979323846 * 9.0);
        const std::uint32_t n = hash2d(x, y, 17U);
        const double micro = 0.55 + 0.45 * ((n & 255U) / 255.0);
        const double bristleMask = std::clamp(0.32 + 0.68 * std::pow(tuftWave, 0.7) * micro, 0.0, 1.0);
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
    const auto alpha = static_cast<unsigned>(std::lround(clamp01(amount) * ((color >> 24) & 255U)));
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
    const auto keep = 255U - static_cast<unsigned>(std::lround(clamp01(amount) * 255.0));
    auto channel = [&](unsigned shift) { return scale((dst >> shift) & 255U, keep); };
    return (channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

SparseTileStore::WriteResult BrushRasterizer::dab(SparseTileStore& store, const BrushDefinition& b,
                                                  InputSample p, double strokeAngle,
                                                  std::uint32_t carryPremul, bool hasCarry)
{
    using R = SparseTileStore::WriteResult;
    if (!valid(p) || !std::isfinite(b.size) || !std::isfinite(b.opacity) || !std::isfinite(b.flow)
        || b.size < 1 || b.size > 256 || b.opacity < 0 || b.opacity > 1 || b.flow < 0 || b.flow > 1
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
    const double pSizeWeight = clamp01(b.pressureSize);
    const double minScale = std::clamp(b.minSizeRatio, 0.05, 1.0);
    const double sizeScale = (1.0 - pSizeWeight) + pSizeWeight * (minScale + (1.0 - minScale) * pShaped);
    const double tiltMag = (p.hasTilt && std::isfinite(p.tiltX) && std::isfinite(p.tiltY))
        ? std::clamp(std::hypot(p.tiltX, p.tiltY) / 90.0, 0.0, 1.0)
        : 0.0;
    const double tiltScale = (p.hasTilt && b.tiltSize > 0.0)
        ? (1.0 + clamp01(b.tiltSize) * tiltMag * 1.5)
        : 1.0;
    const double effectiveSize = std::clamp(b.size * sizeScale * tiltScale, 0.5, 256.0);
    const double radius = effectiveSize / 2.0;

    const double pOpWeight = clamp01(b.pressureOpacity);
    const double pressureAlpha = (1.0 - pOpWeight) + pOpWeight * pShaped;
    const double pFlowWeight = clamp01(b.pressureFlow);
    const double pressureFlowScale = (1.0 - pFlowWeight) + pFlowWeight * pShaped;

    const double left = std::max(0.0, std::floor(p.x - radius - 1));
    const double top = std::max(0.0, std::floor(p.y - radius - 1));
    const double right = std::min(static_cast<double>(g.width()), std::ceil(p.x + radius + 1));
    const double bottom = std::min(static_cast<double>(g.height()), std::ceil(p.y + radius + 1));
    if (left >= right || top >= bottom) return R::Unchanged;

    const auto docLeft = static_cast<std::int64_t>(left);
    const auto docTop = static_cast<std::int64_t>(top);
    const auto docRight = static_cast<std::int64_t>(right);
    const auto docBottom = static_cast<std::int64_t>(bottom);
    const auto tileRange = g.tilesForRect({docLeft, docTop, docRight, docBottom});
    if (!tileRange) return R::Unchanged;

    double totalAngle = b.angleRadians + (b.followDirection ? strokeAngle : 0.0);
    if (p.hasTilt && b.tiltAngle && tiltMag > 0.02) {
        totalAngle += std::atan2(p.tiltY, p.tiltX);
    }
    if (p.hasRotation && b.stylusRotation && std::isfinite(p.rotationRadians)) {
        totalAngle += p.rotationRadians;
    }
    const bool hasRotation = totalAngle != 0.0;
    const double cosA = hasRotation ? std::cos(-totalAngle) : 1.0;
    const double sinA = hasRotation ? std::sin(-totalAngle) : 0.0;
    const double baseAspect = std::clamp(b.aspectRatio, 1.0, 4.0);
    const double aspect = (p.hasTilt && b.tiltAngle)
        ? std::clamp(baseAspect * (1.0 + tiltMag * 0.75), 1.0, 4.0)
        : baseAspect;
    const double softness = 1.0 - clamp01(b.hardness);
    const double radiusPlusHalf = radius + 0.5;
    const double maxDistSq = radiusPlusHalf * radiusPlusHalf;
    const double invBand = 1.0 / std::max(1.0, softness * radiusPlusHalf);
    const double opacityFlowPressure = b.opacity * b.flow * pressureAlpha * pressureFlowScale;
    if (opacityFlowPressure <= 0.0) return R::Unchanged;

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

    auto computeCoverage = [&](double dx, double dy, std::int64_t x, std::int64_t y) noexcept -> double {
        double lx = dx;
        double ly = dy;
        if (hasRotation) {
            lx = dx * cosA - dy * sinA;
            ly = dx * sinA + dy * cosA;
        }
        const double sy = ly * aspect;
        const double distSq = lx * lx + sy * sy;
        if (distSq >= maxDistSq) return 0.0;
        const double dist = std::sqrt(distSq);
        double coverage = clamp01((radiusPlusHalf - dist) * invBand);
        if (softness > 0.0) coverage += softness * (coverage * coverage * (3.0 - 2.0 * coverage) - coverage);
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
            const double tuftWave = 0.5 + 0.5 * std::cos(normX * 3.14159265358979323846 * 9.0);
            const std::uint32_t n = hash2d(x, y, 17U);
            const double micro = 0.55 + 0.45 * ((n & 255U) / 255.0);
            const double bristleMask = std::clamp(0.32 + 0.68 * std::pow(tuftWave, 0.7) * micro, 0.0, 1.0);
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
                for (std::int64_t x = x0; x < x1; ++x) {
                    double coverage = computeCoverage(static_cast<double>(x) + 0.5 - p.x, dy, x, y);
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

BrushStroke::BrushStroke(SparseTileStore& store, BrushDefinition brush) : m_store(store), m_brush(brush) {
    if (std::isfinite(brush.size) && std::isfinite(brush.spacing)
        && brush.size >= 1 && brush.size <= 256 && brush.spacing >= 0.05 && brush.spacing <= 0.5) {
        m_spacing = std::max(0.25, brush.size * brush.spacing);
    } else {
        m_failed = true;
        m_failureReason = SparseTileStore::WriteResult::InvalidPixel;
    }
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
        const auto sx = static_cast<std::int64_t>(std::llround(p.x + ox[i]));
        const auto sy = static_cast<std::int64_t>(std::llround(p.y + oy[i]));
        if (sx < 0 || sy < 0 || sx >= g.width() || sy >= g.height()) continue;
        const auto px = m_store.pixel({sx, sy}).value();
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

bool BrushStroke::stamp(InputSample p) {
    updateCarry(p);
    auto result = BrushRasterizer::dab(m_store, m_brush, p, m_strokeAngle, m_carryPremul, m_hasCarry);
    if (result != SparseTileStore::WriteResult::Changed && result != SparseTileStore::WriteResult::Unchanged) {
        m_failed = true;
        m_failureReason = result;
    }
    return !m_failed;
}

bool BrushStroke::begin(InputSample p) {
    if (m_started || m_failed || !valid(p)) {
        if (!m_failed) { m_failed = true; m_failureReason = SparseTileStore::WriteResult::InvalidPixel; }
        return false;
    }
    m_started = true;
    m_previous = p;
    m_remaining = m_spacing;
    m_hasAngle = false;
    m_hasCarry = false;
    return stamp(p);
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

    double start = 0, end = 1;
    const double radius = m_brush.size / 2 + 1;
    const auto& g = m_store.geometry();
    if (clipAxis(m_previous.x, dx, -radius, static_cast<double>(g.width()) + radius, start, end) &&
        clipAxis(m_previous.y, dy, -radius, static_cast<double>(g.height()) + radius, start, end)) {
        double at = m_remaining;
        if (at < start * distance) at += std::ceil((start * distance - at) / m_spacing) * m_spacing;
        for (; at <= end * distance && at <= distance; at += m_spacing) {
            const double t = at / distance;
            InputSample interp;
            interp.x = m_previous.x + dx * t;
            interp.y = m_previous.y + dy * t;
            interp.pressure = m_previous.pressure + (p.pressure - m_previous.pressure) * t;
            interp.tiltX = m_previous.tiltX + (p.tiltX - m_previous.tiltX) * t;
            interp.tiltY = m_previous.tiltY + (p.tiltY - m_previous.tiltY) * t;
            interp.rotationRadians = m_previous.rotationRadians + (p.rotationRadians - m_previous.rotationRadians) * t;
            interp.tangentialPressure = m_previous.tangentialPressure + (p.tangentialPressure - m_previous.tangentialPressure) * t;
            interp.hasTilt = m_previous.hasTilt || p.hasTilt;
            interp.hasRotation = m_previous.hasRotation || p.hasRotation;
            if (!stamp(interp)) return false;
        }
    }
    const double remainder = std::fmod(distance - m_remaining, m_spacing);
    m_remaining = distance < m_remaining ? m_remaining - distance : (remainder == 0 ? m_spacing : m_spacing - remainder);
    m_previous = p;
    return true;
}

bool BrushStroke::end(InputSample p) {
    if (!extend(p)) return false;
    m_started = false;
    if (m_remaining < m_spacing - 1e-9) return stamp(p);
    return true;
}
} // namespace komapaint
