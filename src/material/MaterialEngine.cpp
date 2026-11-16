#include "MaterialEngine.h"
#include "MaterialTileStore.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace komapaint::material {
namespace {
constexpr double Pi = 3.14159265358979323846;
constexpr std::array<double, SpectralBands> Wavelengths{
    405, 430, 455, 480, 505, 530, 555, 580, 605, 630, 655, 680
};

// CIE 1931 2-degree CMF under D65 normalized so flat reflectance 1.0 maps to
// linear sRGB (1.0, 1.0, 1.0).
constexpr std::array<std::array<double, SpectralBands>, 3> SpectralToLinearRgb{{
    { 0.00366,  0.01884, -0.00674, -0.07931, -0.16926, -0.20265,
      0.01365,  0.35078,  0.54764,  0.37885,  0.12488,  0.01970 },
    {-0.00346, -0.03989, -0.03106,  0.04790,  0.17985,  0.34724,
      0.32625,  0.18391,  0.02402, -0.02598, -0.00913,  0.00035 },
    { 0.02958,  0.37334,  0.46845,  0.21079,  0.03578, -0.02798,
     -0.03970, -0.03008, -0.01414, -0.00445, -0.00139, -0.00020 }
}};

// Smooth calibrated primary/secondary reflectance spectra for arbitrary RGB
// compatibility; every corner lies strictly in [0.015, 0.985] and preserves
// subtractive mixing behaviour without inventing named pigment identities.
constexpr Spectrum BasisRed{
    0.0446, 0.0344, 0.0150, 0.0150, 0.0150, 0.0150,
    0.0150, 0.1321, 0.8026, 0.9850, 0.9850, 0.9850
};
constexpr Spectrum BasisGreen{
    0.0150, 0.0150, 0.0150, 0.2721, 0.9428, 0.9850,
    0.9850, 0.7532, 0.1803, 0.0150, 0.0150, 0.0221
};
constexpr Spectrum BasisBlue{
    0.9850, 0.9850, 0.9841, 0.6361, 0.2150, 0.0150,
    0.0150, 0.0150, 0.0226, 0.0941, 0.1417, 0.1659
};
constexpr Spectrum BasisCyan{
    0.9554, 0.9656, 0.9850, 0.9850, 0.9850, 0.9850,
    0.9850, 0.8679, 0.1974, 0.0150, 0.0150, 0.0150
};
constexpr Spectrum BasisMagenta{
    0.9850, 0.9850, 0.9850, 0.7279, 0.0572, 0.0150,
    0.0150, 0.2468, 0.8197, 0.9850, 0.9850, 0.9779
};
constexpr Spectrum BasisYellow{
    0.0150, 0.0150, 0.0159, 0.3639, 0.7850, 0.9850,
    0.9850, 0.9850, 0.9774, 0.9059, 0.8583, 0.8341
};

double clamp01(double value) noexcept
{
    return std::clamp(value, 0.0, 1.0);
}

double finiteOrZero(double value) noexcept
{
    return std::isfinite(value) ? value : 0.0;
}

double srgbToLinear(double value) noexcept
{
    value = clamp01(value);
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double linearToSrgb(double value) noexcept
{
    value = clamp01(value);
    return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

double gaussian(double x, double center, double width) noexcept
{
    const double q = (x - center) / width;
    return std::exp(-0.5 * q * q);
}

std::uint32_t packStraight(double r, double g, double b, double a = 1.0) noexcept
{
    const auto byte = [](double v) {
        return static_cast<std::uint32_t>(std::lround(clamp01(v) * 255.0));
    };
    return (byte(a) << 24U) | (byte(linearToSrgb(r)) << 16U)
        | (byte(linearToSrgb(g)) << 8U) | byte(linearToSrgb(b));
}

std::array<double, 3> unpackLinear(std::uint32_t argb) noexcept
{
    return {srgbToLinear(double((argb >> 16U) & 0xffU) / 255.0),
            srgbToLinear(double((argb >> 8U) & 0xffU) / 255.0),
            srgbToLinear(double(argb & 0xffU) / 255.0)};
}

std::array<double, 3> spectrumToLinearRgb(const Spectrum& spectrum) noexcept
{
    std::array<double, 3> rgb{0.0, 0.0, 0.0};
    for (int c = 0; c < 3; ++c) {
        double sum = 0.0;
        for (std::size_t i = 0; i < SpectralBands; ++i)
            sum += SpectralToLinearRgb[static_cast<std::size_t>(c)][i] * spectrum[i];
        rgb[static_cast<std::size_t>(c)] = clamp01(sum);
    }
    return rgb;
}

double computeSurfaceToothHeight(std::int64_t x, std::int64_t y, const SurfaceProfile& surface) noexcept
{
    const double px = double(x) + 0.5;
    const double py = double(y) + 0.5;
    const double coarse = std::sin(px * 0.41 + py * 0.37) * std::cos(px * 0.19 - py * 0.43);
    const double medium = std::sin(px * 1.13 - py * 0.97 + 0.7);
    const double fine = std::cos(px * 2.17 + py * 1.83);
    const double raw = clamp01(0.5 + 0.28 * coarse + 0.15 * medium + 0.07 * fine);
    const double amplitude = clamp01(surface.tooth * 0.68 + surface.roughness * 0.32);
    return clamp01(0.5 + (raw - 0.5) * amplitude);
}

struct DabContactResult {
    double contact = 0.0;
    double radial = 1.0;
};

DabContactResult dabContactAt(const BrushDefinition& brush,
                              std::int64_t x, std::int64_t y,
                              double centerX, double centerY,
                              double ca, double sa, double rx, double ry,
                              double contactFirmness,
                              const SurfaceProfile* surface = nullptr,
                              double dryBrushFactor = 0.0) noexcept
{
    const double dx = double(x) + 0.5 - centerX;
    const double dy = double(y) + 0.5 - centerY;
    const double u = (ca * dx + sa * dy) / std::max(0.5, rx);
    const double v = (-sa * dx + ca * dy) / std::max(0.5, ry);
    const double radial = u * u + v * v;
    if (radial > 1.0) return {};
    double contact = clamp01(1.0 - radial);
    if (brush.tip == TipGeometry::Flat || brush.tip == TipGeometry::Spatula) {
        contact = clamp01((1.0 - std::abs(v)) * 2.0) * clamp01((1.1 - std::abs(u)) * 2.0);
    } else if (brush.tip == TipGeometry::Bristle && brush.bristleCount > 0) {
        const double bristleSpacing = 2.0 / double(brush.bristleCount);
        const double nearest = std::abs(std::remainder(u + 1.0, bristleSpacing));
        const double bristleWidth = std::max(0.02, bristleSpacing * (0.32 + brush.bristleSpread * 0.4));
        contact *= nearest < bristleWidth ? clamp01(1.0 - nearest / bristleWidth * 0.2) : 0.08;
    } else if (brush.tip == TipGeometry::Mop) {
        contact = std::sqrt(std::max(0.0, contact));
    }
    if (brush.hardness >= 1.0) {
        contact = contact > 0.0 ? 1.0 : 0.0;
    } else {
        const double h = clamp01(brush.hardness);
        const double profile = radial <= h ? 1.0 : clamp01((1.0 - radial) / std::max(1e-6, 1.0 - h));
        if (brush.tip == TipGeometry::Round) contact = profile;
        else contact *= profile;
    }
    if (brush.textureStrength > 0.0) {
        std::uint32_t n = static_cast<std::uint32_t>(x) * 0x8da6b343U ^ static_cast<std::uint32_t>(y) * 0xd8163841U;
        n ^= n >> 13;
        n *= 0x85ebca6bU;
        n ^= n >> 16;
        const double grain = 0.30 + 0.70 * double(n & 255U) / 255.0;
        contact *= 1.0 - clamp01(brush.textureStrength) * (1.0 - grain);
    }
    if (surface && dryBrushFactor > 0.02) {
        const double toothHeight = computeSurfaceToothHeight(x, y, *surface);
        const double gapThreshold = clamp01(0.50 + (dryBrushFactor - 0.35) * 0.32);
        if (toothHeight < gapThreshold) return {0.0, radial};
        contact *= clamp01((toothHeight - gapThreshold) / std::max(0.05, 1.0 - gapThreshold) * 1.45);
    }
    contact *= contactFirmness;
    return {clamp01(contact), radial};
}

Spectrum rgbToReflectance(std::uint32_t argb) noexcept
{
    const auto rgb = unpackLinear(argb);
    const double r = rgb[0], g = rgb[1], b = rgb[2];
    const double w = std::min({r, g, b});
    const double maxC = std::max({r, g, b});
    const double k = clamp01(1.0 - maxC);

    // Express the residual chromaticity as a convex combination of one
    // secondary and one primary basis curve, plus neutral white and black.
    const Spectrum* secondary = &BasisYellow;
    const Spectrum* primary = &BasisRed;
    double secWeight = 0.0;
    double priWeight = 0.0;
    if (r >= g && g >= b) {
        secWeight = g - b; priWeight = r - g; secondary = &BasisYellow; primary = &BasisRed;
    } else if (g >= r && r >= b) {
        secWeight = r - b; priWeight = g - r; secondary = &BasisYellow; primary = &BasisGreen;
    } else if (g >= b && b >= r) {
        secWeight = b - r; priWeight = g - b; secondary = &BasisCyan; primary = &BasisGreen;
    } else if (b >= g && g >= r) {
        secWeight = g - r; priWeight = b - g; secondary = &BasisCyan; primary = &BasisBlue;
    } else if (b >= r && r >= g) {
        secWeight = r - g; priWeight = b - r; secondary = &BasisMagenta; primary = &BasisBlue;
    } else {
        secWeight = b - g; priWeight = r - b; secondary = &BasisMagenta; primary = &BasisRed;
    }

    Spectrum result{};
    for (std::size_t i = 0; i < SpectralBands; ++i) {
        const double value = w * 0.985 + k * 0.015
            + secWeight * (*secondary)[i] + priWeight * (*primary)[i];
        result[i] = std::clamp(value, 0.015, 0.985);
    }
    return result;
}

PigmentDefinition pigmentFromReflectance(std::string id, std::string name,
                                         const Spectrum& reflectance,
                                         double size, double density,
                                         double tint, double transparency,
                                         double granulation, double staining,
                                         double liftability, double settling,
                                         std::vector<std::string> media)
{
    PigmentDefinition p;
    p.id = std::move(id);
    p.name = std::move(name);
    p.particleSizeMicrons = size;
    p.physicalDensity = density;
    p.tintStrength = tint;
    p.transparency = transparency;
    p.granulation = granulation;
    p.staining = staining;
    p.liftability = liftability;
    p.settling = settling;
    p.media = std::move(media);
    for (std::size_t i = 0; i < SpectralBands; ++i) {
        const double r = std::clamp(reflectance[i], 0.015, 0.985);
        const double ratio = (1.0 - r) * (1.0 - r) / (2.0 * r);
        // Fine, well-dispersed pigment scatters more evenly. Density and
        // particle size affect transport and deposition separately from the
        // artist's pigment-concentration control.
        const double scatter = std::clamp(0.6 + size * 0.035 + density * 0.08, 0.5, 2.0);
        p.scattering[i] = scatter;
        p.absorption[i] = ratio * scatter * std::max(0.1, tint);
    }
    return p;
}

Spectrum yellowSpectrum() noexcept
{
    Spectrum s{};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double w = Wavelengths[i];
        s[i] = clamp01(0.018 + 0.91 / (1.0 + std::exp(-(w - 486.0) / 8.0)));
    }
    return s;
}

Spectrum blueSpectrum() noexcept
{
    Spectrum s{};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double w = Wavelengths[i];
        s[i] = clamp01(0.055 + 0.78 / (1.0 + std::exp((w - 492.0) / 14.0))
                       + 0.055 / (1.0 + std::exp(-(w - 635.0) / 14.0)));
    }
    return s;
}

Spectrum redSpectrum() noexcept
{
    // Quinacridone Magenta (PR122): strong red reflection, secondary violet-blue
    // reflection, and strong green absorption trough.
    Spectrum s{};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double w = Wavelengths[i];
        s[i] = clamp01(0.018 + 0.86 / (1.0 + std::exp(-(w - 578.0) / 14.0))
                       + 0.48 / (1.0 + std::exp((w - 446.0) / 14.0)));
    }
    return s;
}

Spectrum whiteSpectrum() noexcept
{
    Spectrum s{};
    s.fill(0.94);
    return s;
}

Spectrum blackSpectrum() noexcept
{
    Spectrum s{};
    s.fill(0.022);
    return s;
}

Spectrum earthSpectrum() noexcept
{
    Spectrum s{};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double w = Wavelengths[i];
        const double red = 0.28 / (1.0 + std::exp(-(w - 565.0) / 22.0));
        s[i] = clamp01(0.025 + red + 0.04 * gaussian(w, 525.0, 45.0));
    }
    return s;
}

const MediumProfile kGouache{
    "gouache", "Gouache", 0.78, 0.78, 0.72, 0.94,
    0.18, 0.38, 0.72, 0.88, 0.18, 0.82, 0.24
};
const MediumProfile kWatercolour{
    "watercolour", "Watercolour", 0.24, 0.38, 0.44, 0.22,
    0.90, 0.86, 0.32, 0.18, 0.72, 0.34, 0.66
};
const MediumProfile kInk{
    "ink", "Ink", 0.31, 0.58, 0.80, 0.62,
    0.52, 0.42, 0.82, 0.35, 0.24, 0.91, 0.12
};
const SurfaceProfile kHotPressed{
    "hot-press", 0.34, 0.14, 0.08, 0.0,
    0.28, 0.78, 0.98, 0.94, 0.32, 0.82, 0.12, 0.72,
    "Hot Press", 0.30, 0.0, 0.16
};
const SurfaceProfile kColdPressed{
    "cold-press", 0.62, 0.58, 0.42, 0.0,
    0.56, 0.62, 0.92, 0.91, 0.54, 1.00, 0.32, 0.58,
    "Cold Press", 0.58, 0.0, 0.68
};
const SurfaceProfile kRoughPaper{
    "rough-paper", 0.82, 0.92, 0.86, 0.0,
    0.84, 0.44, 0.84, 0.86, 0.36, 1.28, 0.52, 0.42,
    "Rough", 0.84, 0.0, 0.96
};
const SurfaceProfile kSmoothIllustration{
    "smooth-illustration", 0.42, 0.10, 0.06, 0.0,
    0.26, 0.72, 0.99, 0.97, 0.44, 0.92, 0.06, 0.28,
    "Smooth Illustration", 0.36, 0.0, 0.12
};
const SurfaceProfile kTexturedIllustration{
    "textured-illustration", 0.56, 0.66, 0.54, 0.0,
    0.48, 0.58, 0.96, 0.93, 0.52, 1.08, 0.14, 0.34,
    "Textured Illustration", 0.52, 0.0, 0.48
};
const SurfaceProfile kPaper = kColdPressed;
const SurfaceProfile kPalette{
    "non-absorbent-palette", 0.02, 0.15, 0.02, 0.98,
    0.01, 0.96, 0.98, 0.99, 0.94, 0.18, 0.02, 0.90,
    "Non-Absorbent Palette", 0.02, 0.0, 0.05
};

std::uint32_t blendPremultiplied(std::uint32_t dst, std::uint32_t srcStraight,
                                 double alpha) noexcept
{
    alpha = clamp01(alpha * double((srcStraight >> 24U) & 0xffU) / 255.0);
    const double inverse = 1.0 - alpha;
    const std::uint32_t srcChannels[3] = {
        (srcStraight >> 16U) & 0xffU, (srcStraight >> 8U) & 0xffU, srcStraight & 0xffU
    };
    const std::uint32_t shifts[3] = {16U, 8U, 0U};
    std::uint32_t out = 0;
    for (int i = 0; i < 3; ++i) {
        const double srcPremul = double(srcChannels[i]) * alpha;
        const double dstPremul = double((dst >> shifts[i]) & 0xffU);
        out |= static_cast<std::uint32_t>(std::clamp(std::lround(srcPremul + dstPremul * inverse), 0L, 255L)) << shifts[i];
    }
    const double dstAlpha = double((dst >> 24U) & 0xffU) / 255.0;
    const auto outAlpha = static_cast<std::uint32_t>(std::lround((alpha + dstAlpha * inverse) * 255.0));
    return (out & 0x00ffffffU) | (outAlpha << 24U);
}

bool finiteNonnegative(double n) noexcept { return std::isfinite(n) && n >= 0.0; }

bool validMaterialSample(const MaterialSample& sample) noexcept
{
    return std::isfinite(sample.x) && std::isfinite(sample.y)
        && std::isfinite(sample.input.pressure) && std::isfinite(sample.input.speed)
        && std::isfinite(sample.input.tilt) && std::isfinite(sample.input.rotationRadians);
}

bool clipAxis(double position, double delta, double low, double high,
              double& start, double& end) noexcept
{
    if (delta == 0) return position >= low && position <= high;
    double a = (low - position) / delta;
    double b = (high - position) / delta;
    if (a > b) std::swap(a, b);
    start = std::max(start, a);
    end = std::min(end, b);
    return start <= end;
}

MaterialSample interpolate(const MaterialSample& a, const MaterialSample& b, double t) noexcept
{
    auto lerp = [t](double x, double y) { return x + (y - x) * t; };
    return {lerp(a.x, b.x), lerp(a.y, b.y),
            {lerp(a.input.pressure, b.input.pressure),
             lerp(a.input.speed, b.input.speed), lerp(a.input.tilt, b.input.tilt),
             lerp(a.input.rotationRadians, b.input.rotationRadians)}};
}

void refreshBatchState(PaintBatch& batch) noexcept
{
    const double total = batch.totalMass();
    if (total <= 1e-12) {
        batch.wetness = 0.0;
        batch.consistency = 0.0;
        return;
    }
    batch.wetness = clamp01(batch.water / total);
    batch.consistency = clamp01((batch.pigmentMass() + batch.binder + batch.filler) / total);
}

} // namespace

PigmentLibrary::PigmentLibrary()
{
    const std::vector<std::string> paintMedia{"gouache", "watercolour", "ink"};
    m_pigments.push_back(pigmentFromReflectance("PY3", "Primary Yellow", yellowSpectrum(),
                                                0.55, 1.45, 1.0, 0.30, 0.12, 0.52, 0.62, 0.16, paintMedia));
    m_pigments.push_back(pigmentFromReflectance("PB15", "Phthalo Blue", blueSpectrum(),
                                                0.08, 1.62, 1.25, 0.72, 0.18, 0.94, 0.18, 0.11, paintMedia));
    m_pigments.push_back(pigmentFromReflectance("PR122", "Quinacridone Magenta", redSpectrum(),
                                                0.12, 1.42, 0.95, 0.70, 0.08, 0.78, 0.30, 0.14, paintMedia));
    m_pigments.push_back(pigmentFromReflectance("PW6", "Titanium White", whiteSpectrum(),
                                                0.28, 4.10, 0.85, 0.02, 0.0, 0.28, 0.84, 0.48, {"gouache"}));
    m_pigments.push_back(pigmentFromReflectance("PBk7", "Carbon Black", blackSpectrum(),
                                                0.035, 1.80, 1.10, 0.12, 0.0, 0.90, 0.15, 0.18, paintMedia));
    m_pigments.push_back(pigmentFromReflectance("PBr7", "Raw Umber", earthSpectrum(),
                                                4.8, 3.4, 0.72, 0.40, 0.92, 0.24, 0.82, 0.78, paintMedia));
}

const PigmentDefinition* PigmentLibrary::find(const std::string& id) const noexcept
{
    const auto it = std::find_if(m_pigments.begin(), m_pigments.end(), [&id](const auto& p) { return p.id == id; });
    return it == m_pigments.end() ? nullptr : &*it;
}

const MediumProfile& gouacheProfile() noexcept { return kGouache; }
const MediumProfile& watercolourProfile() noexcept { return kWatercolour; }
const MediumProfile& inkProfile() noexcept { return kInk; }

const MediumProfile* findMediumProfile(const std::string& id) noexcept
{
    if (id == kGouache.id) return &kGouache;
    if (id == kWatercolour.id) return &kWatercolour;
    if (id == kInk.id) return &kInk;
    return nullptr;
}
const SurfaceProfile& absorbentPaper() noexcept { return kPaper; }
const SurfaceProfile& hotPressedPaper() noexcept { return kHotPressed; }
const SurfaceProfile& coldPressedPaper() noexcept { return kColdPressed; }
const SurfaceProfile& roughPaper() noexcept { return kRoughPaper; }
const SurfaceProfile& smoothIllustrationPaper() noexcept { return kSmoothIllustration; }
const SurfaceProfile& texturedIllustrationPaper() noexcept { return kTexturedIllustration; }
const SurfaceProfile& nonAbsorbentPalette() noexcept { return kPalette; }

const std::vector<SurfaceProfile>& defaultSurfaceProfiles() noexcept
{
    static const std::vector<SurfaceProfile> profiles{
        kColdPressed,
        kHotPressed,
        kRoughPaper,
        kSmoothIllustration,
        kTexturedIllustration,
    };
    return profiles;
}

const SurfaceProfile* findSurfaceProfile(const std::string& id) noexcept
{
    for (const auto& profile : defaultSurfaceProfiles())
        if (profile.id == id) return &profile;
    if (id == kPalette.id) return &kPalette;
    return nullptr;
}

const std::vector<MediumProfile>& defaultMediumProfiles() noexcept
{
    static const std::vector<MediumProfile> media{
        kGouache,
        kWatercolour,
        kInk,
    };
    return media;
}

double surfaceToothHeightAt(std::int64_t x, std::int64_t y, const SurfaceProfile& surface) noexcept
{
    return computeSurfaceToothHeight(x, y, surface);
}

double PaintBatch::pigmentMass() const noexcept
{
    double total = 0;
    for (const auto& component : pigments)
        if (finiteNonnegative(component.mass)) total += component.mass;
    return total;
}

double PaintBatch::totalMass() const noexcept
{
    return pigmentMass() + std::max(0.0, finiteOrZero(water))
        + std::max(0.0, finiteOrZero(binder)) + std::max(0.0, finiteOrZero(filler));
}

double PaintBatch::pigmentConcentration() const noexcept
{
    const double total = totalMass();
    return total > 0 ? pigmentMass() / total : 0.0;
}

bool PaintBatch::valid() const noexcept
{
    if (mediumId.empty() || !finiteNonnegative(water) || !finiteNonnegative(binder)
        || !finiteNonnegative(filler) || !std::isfinite(wetness) || wetness < 0 || wetness > 1
        || !std::isfinite(consistency) || consistency < 0 || consistency > 1
        || !std::isfinite(drying) || drying < 0 || drying > 1
        || !std::isfinite(settling) || settling < 0 || settling > 1) return false;
    for (const auto& p : pigments)
        if (!finiteNonnegative(p.mass) || p.mass == 0 || (p.pigmentId.empty() && p.residualArgb == 0)) return false;
    return true;
}

void PaintBatch::addWater(double quantity)
{
    if (finiteNonnegative(quantity)) {
        const double solids = pigmentMass() + binder + filler;
        if (quantity > 0.0 && solids > 0.0) {
            const double rewet = clamp01(quantity / (solids + quantity));
            drying *= 1.0 - rewet * 0.65;
            settling *= 1.0 - rewet * 0.25;
        }
        water += quantity;
        refreshBatchState(*this);
    }
}
void PaintBatch::addBinder(double quantity)
{
    if (finiteNonnegative(quantity)) { binder += quantity; refreshBatchState(*this); }
}
void PaintBatch::addFiller(double quantity)
{
    if (finiteNonnegative(quantity)) { filler += quantity; refreshBatchState(*this); }
}
void PaintBatch::scale(double factor) noexcept
{
    if (!finiteNonnegative(factor)) return;
    for (auto& p : pigments) p.mass *= factor;
    water *= factor;
    binder *= factor;
    filler *= factor;
}

bool PaintBatch::addPigment(std::string pigmentId, double quantity)
{
    if (pigmentId.empty() || !finiteNonnegative(quantity) || quantity == 0) return false;
    auto it = std::find_if(pigments.begin(), pigments.end(), [&pigmentId](const auto& p) {
        return p.pigmentId == pigmentId && p.residualArgb == 0;
    });
    if (it == pigments.end()) pigments.push_back({std::move(pigmentId), 0, quantity});
    else it->mass += quantity;
    refreshBatchState(*this);
    return true;
}

bool PaintBatch::addResidual(std::uint32_t argb, double quantity)
{
    if (!finiteNonnegative(quantity) || quantity == 0 || ((argb >> 24U) & 0xffU) == 0) return false;
    const std::uint32_t rgb = (argb & 0x00ffffffU) | 0xff000000U;
    auto it = std::find_if(pigments.begin(), pigments.end(), [rgb](const auto& p) {
        return p.pigmentId.empty() && p.residualArgb == rgb;
    });
    if (it == pigments.end()) pigments.push_back({{}, rgb, quantity});
    else it->mass += quantity;
    refreshBatchState(*this);
    return true;
}

PaintBatch mix(const PaintBatch& a, const PaintBatch& b, const std::string& resultMedium)
{
    PaintBatch result;
    result.mediumId = resultMedium.empty() ? (a.mediumId == b.mediumId ? a.mediumId : a.mediumId) : resultMedium;
    result.pigments = a.pigments;
    for (const auto& component : b.pigments) {
        auto it = std::find_if(result.pigments.begin(), result.pigments.end(), [&component](const auto& p) {
            return p.pigmentId == component.pigmentId && p.residualArgb == component.residualArgb;
        });
        if (it == result.pigments.end()) result.pigments.push_back(component);
        else it->mass += component.mass;
    }
    result.water = a.water + b.water;
    result.binder = a.binder + b.binder;
    result.filler = a.filler + b.filler;
    result.wetness = (a.wetness * a.totalMass() + b.wetness * b.totalMass())
        / std::max(1e-12, a.totalMass() + b.totalMass());
    result.consistency = (a.consistency * a.totalMass() + b.consistency * b.totalMass())
        / std::max(1e-12, a.totalMass() + b.totalMass());
    const double total = std::max(1e-12, a.totalMass() + b.totalMass());
    result.drying = clamp01((a.drying * a.totalMass() + b.drying * b.totalMass()) / total);
    result.settling = clamp01((a.settling * a.totalMass() + b.settling * b.totalMass()) / total);
    refreshBatchState(result);
    return result;
}

PaintBatch makeResidualPaint(std::uint32_t argb, double pigmentMass, double water,
                             double binder, const std::string& mediumId)
{
    PaintBatch result;
    result.mediumId = mediumId;
    result.addResidual(argb, pigmentMass);
    result.water = water;
    result.binder = binder;
    return result;
}

double SensorCurve::evaluate(double input) const noexcept
{
    input = clamp01(finiteOrZero(input));
    const double x = input * 2.0;
    const std::size_t segment = x < 1.0 ? 0 : 1;
    const double t = x - double(segment);
    const double a = std::isfinite(points[segment]) ? points[segment] : 0.0;
    const double b = std::isfinite(points[segment + 1]) ? points[segment + 1] : 0.0;
    return clamp01(a + (b - a) * t);
}

double BrushDefinition::effectiveReservoirCapacity() const noexcept
{
    const double base = std::max(1.0, nominalSize);
    return reservoirCapacity * std::max(0.25, size / base);
}

bool BrushDefinition::valid() const noexcept
{
    const auto unit = [](double v) { return std::isfinite(v) && v >= 0 && v <= 1; };
    const auto curveValid = [&unit](const SensorCurve& curve) {
        return std::all_of(curve.points.begin(), curve.points.end(), unit);
    };
    return !id.empty() && !name.empty() && std::isfinite(size) && size >= 1 && size <= 4096
        && std::isfinite(nominalSize) && nominalSize >= 1 && nominalSize <= 4096
        && unit(spacingRatio) && spacingRatio > 0 && unit(hardness) && std::isfinite(aspect) && aspect > 0 && aspect <= 8
        && std::isfinite(rotationRadians) && unit(bristleSpread) && unit(textureStrength)
        && finiteNonnegative(reservoirCapacity)
        && reservoirCapacity > 0 && unit(initialLoad) && unit(releaseRate)
        && finiteNonnegative(depletionRate) && depletionRate <= 4.0
        && unit(pickupRate) && unit(reloadThreshold)
        && unit(pressureTransfer) && unit(contactPressureScale) && unit(speedTransfer)
        && unit(tiltTransfer) && unit(tiltFlattening)
        && unit(contactTransfer) && unit(waterSpread) && unit(pigmentDiffusion)
        && unit(paintBody) && std::isfinite(dryingRate) && dryingRate >= 0.05 && dryingRate <= 10.0
        && unit(toothResponse) && unit(edgeAccumulation) && unit(granulationResponse)
        && unit(paintLiftability) && unit(staining) && unit(rewetting)
        && curveValid(pressureCurve) && curveValid(contactPressureCurve)
        && curveValid(speedCurve) && curveValid(tiltCurve)
        && initialPaint.valid() && initialPaint.mediumId == mediumId;
}

BrushReservoir::BrushReservoir(double capacity) { setCapacity(capacity); }

double BrushReservoir::fillRatio() const noexcept
{
    return m_capacity > 0 ? clamp01(quantity() / m_capacity) : 0.0;
}

bool BrushReservoir::load(const PaintBatch& paint, double amount)
{
    if (!paint.valid() || !finiteNonnegative(paint.totalMass()) || paint.totalMass() == 0 || m_capacity <= 0)
        return false;
    const double requested = amount < 0 ? m_capacity : amount;
    if (!finiteNonnegative(requested)) return false;
    const double available = std::min({requested, paint.totalMass(), m_capacity});
    if (available <= 0) return false;
    m_batch = paint;
    m_batch.scale(available / paint.totalMass());
    m_loaded = true;
    return true;
}

bool BrushReservoir::reload(const BrushDefinition& brush)
{
    if (!brush.valid() || m_capacity <= 0.0 || brush.initialLoad <= 0.0)
        return false;
    const double sourceMass = brush.initialPaint.totalMass();
    if (!finiteNonnegative(sourceMass) || sourceMass <= 0.0)
        return false;
    const double targetMass = std::min(m_capacity, m_capacity * brush.initialLoad);
    if (targetMass <= 0.0) return false;
    m_batch = brush.initialPaint;
    m_batch.scale(targetMass / sourceMass);
    m_loaded = true;
    return true;
}

bool BrushReservoir::reloadIfNeeded(const BrushDefinition& brush)
{
    if (!brush.valid() || brush.reloadThreshold <= 0 || brush.initialLoad <= brush.reloadThreshold
        || fillRatio() > brush.reloadThreshold) return false;
    return load(brush.initialPaint, m_capacity * brush.initialLoad);
}

PaintBatch BrushReservoir::withdraw(double amount)
{
    PaintBatch out;
    out.mediumId = m_batch.mediumId;
    if (!m_loaded || !finiteNonnegative(amount) || amount <= 0 || quantity() <= 0) return out;
    const double removed = std::min(amount, quantity());
    const double ratio = removed / quantity();
    out = m_batch;
    out.scale(ratio);
    m_batch.scale(1.0 - ratio);
    if (quantity() <= 1e-12) {
        m_batch = {};
        m_loaded = false;
    }
    return out;
}

bool BrushReservoir::pickup(const PaintBatch& paint, double amount)
{
    if (!paint.valid() || !finiteNonnegative(amount) || amount <= 0 || quantity() >= m_capacity) return false;
    const double accepted = std::min(amount, m_capacity - quantity());
    PaintBatch addition = paint;
    const double source = paint.totalMass();
    if (source <= 0) return false;
    addition.scale(accepted / source);
    if (!m_loaded || quantity() <= 0) m_batch = std::move(addition);
    else m_batch = mix(m_batch, addition, m_batch.mediumId);
    m_loaded = quantity() > 0;
    return true;
}

bool BrushReservoir::pickupFromSurface(const BrushDefinition& brush,
                                       const PaintBatch& paint,
                                       double contactArea)
{
    if (!brush.valid() || !finiteNonnegative(contactArea) || contactArea <= 0) return false;
    const double amount = std::min(quantity() < m_capacity ? m_capacity - quantity() : 0.0,
                                   brush.pickupRate * contactArea * (0.25 + brush.paintLiftability));
    return pickup(paint, amount);
}

void BrushReservoir::clean(double waterAmount, const std::string& mediumId)
{
    m_batch = {};
    m_batch.mediumId = mediumId.empty() ? "gouache" : mediumId;
    if (finiteNonnegative(waterAmount)) m_batch.water = waterAmount;
    refreshBatchState(m_batch);
    m_loaded = waterAmount > 0;
}

void BrushReservoir::setCapacity(double capacity)
{
    if (!finiteNonnegative(capacity) || capacity <= 0) return;
    m_capacity = capacity;
    if (quantity() > m_capacity) {
        const double factor = m_capacity / quantity();
        m_batch.scale(factor);
    }
}

double BrushReservoir::transferredAmount(const BrushDefinition& brush,
                                          const BrushInput& input,
                                          double contactArea) const noexcept
{
    if (!brush.valid() || !std::isfinite(input.pressure) || !std::isfinite(input.speed)
        || !std::isfinite(input.tilt) || !finiteNonnegative(contactArea) || contactArea <= 0) return 0;
    const double pressure = brush.pressureCurve.evaluate(input.pressure);
    const double speed = brush.speedCurve.evaluate(input.speed);
    const double tilt = brush.tiltCurve.evaluate(input.tilt);
    const double response = 1.0 + brush.pressureTransfer * (pressure - 0.5) * 2.0
        + brush.speedTransfer * (speed - 0.5) * 2.0
        + brush.tiltTransfer * (tilt - 0.5) * 2.0;
    const double loadedResponse = 0.2 + 0.8 * std::sqrt(fillRatio());
    const double amount = brush.releaseRate * brush.depletionRate * brush.contactTransfer
        * std::max(0.0, response) * loadedResponse * contactArea;
    return std::min(quantity(), amount);
}

Spectrum MaterialEngine::reflectance(const PaintBatch& paint, double depositShare) const noexcept
{
    Spectrum k{}, s{};
    if (!std::isfinite(depositShare) || depositShare <= 0.0) depositShare = 0.0;
    for (const auto& component : paint.pigments) {
        if (!finiteNonnegative(component.mass) || component.mass <= 0.0) continue;
        const PigmentDefinition* pigment = component.pigmentId.empty() ? nullptr : m_pigments.find(component.pigmentId);
        Spectrum residual{};
        if (!pigment) residual = rgbToReflectance(component.residualArgb | 0xff000000U);
        for (std::size_t i = 0; i < SpectralBands; ++i) {
            double absorption = 0.0, scattering = 1.0;
            if (pigment) {
                absorption = pigment->absorption[i];
                scattering = pigment->scattering[i];
            } else {
                const double r = std::clamp(residual[i], 0.015, 0.985);
                absorption = (1.0 - r) * (1.0 - r) / (2.0 * r);
            }
            k[i] += absorption * component.mass;
            s[i] += scattering * component.mass;
        }
    }
    if (finiteNonnegative(paint.filler) && paint.filler > 0.0) {
        // Inert white filler / chalk adds diffuse scattering and minimal
        // absorption, increasing gouache opacity and slightly lifting value.
        for (std::size_t i = 0; i < SpectralBands; ++i) {
            s[i] += 1.8 * paint.filler;
            k[i] += 0.01 * paint.filler;
        }
    }
    Spectrum result{};
    const double localPigment = paint.pigmentMass() * depositShare;
    if (localPigment <= 1e-12) { result.fill(1.0); return result; }
    // Keep intrinsic pigment chroma even for thin glazes/tints; thin-film
    // transparency is carried by OpticalResult::alpha rather than washing out
    // straight RGB toward white.
    const double depthScale = 0.35 + 0.65 * clamp01(localPigment);
    for (std::size_t i = 0; i < SpectralBands; ++i) {
        const double ratio = k[i] / std::max(1e-12, s[i]);
        const double opticalDepth = std::max(0.0, ratio * depthScale);
        result[i] = clamp01(1.0 + opticalDepth - std::sqrt(opticalDepth * opticalDepth + 2.0 * opticalDepth));
    }
    return result;
}

OpticalResult MaterialEngine::opticalResult(const PaintBatch& paint,
                                            const MediumProfile& medium,
                                            const SurfaceProfile& surface,
                                            double depositShare) const noexcept
{
    OpticalResult out;
    const double pigmentMass = paint.pigmentMass();
    const double totalMass = paint.totalMass();
    if (pigmentMass <= 1e-12 || totalMass <= 1e-12
        || !std::isfinite(depositShare) || depositShare <= 0.0) return out;
    const Spectrum spectrum = reflectance(paint, depositShare);
    const auto rgb = spectrumToLinearRgb(spectrum);

    double weightedOpacity = 0.0;
    for (const auto& component : paint.pigments) {
        if (!finiteNonnegative(component.mass) || component.mass <= 0.0) continue;
        const auto* p = component.pigmentId.empty() ? nullptr : m_pigments.find(component.pigmentId);
        const double transparency = p ? clamp01(p->transparency) : 0.25;
        const double tint = p ? std::clamp(p->tintStrength, 0.25, 2.0) : 1.0;
        weightedOpacity += component.mass * (0.65 + 0.35 * (1.0 - transparency)) * (0.7 + 0.3 * tint);
    }
    const double pigmentOpacityFactor = clamp01(weightedOpacity / pigmentMass);
    const double localPigment = pigmentMass * depositShare;
    const double localFiller = std::max(0.0, finiteOrZero(paint.filler)) * depositShare;
    const double effectiveSolids = localPigment * pigmentOpacityFactor + localFiller * 0.45;
    const double binderRatio = clamp01(paint.binder / totalMass);
    const double waterFraction = clamp01(paint.water / totalMass);
    const double wetTransparency = 1.0 - waterFraction * clamp01(medium.waterSpread) * 0.14;
    const double dryFilmDensity = 0.92 + clamp01(paint.drying) * 0.08;
    const double mediumFactor = (0.48 + clamp01(medium.hidingPower) * 1.65)
        * (0.82 + binderRatio * clamp01(medium.binderResponse) * 0.35);
    const double substrateRetention = (0.55 + clamp01(surface.absorbency) * 0.8)
        * (0.72 + clamp01(surface.pigmentRetention) * 0.28)
        * (0.96 - clamp01(surface.porosity) * 0.08);
    const double opticalDepth = std::max(0.0, effectiveSolids * mediumFactor
        * wetTransparency * dryFilmDensity * substrateRetention);
    const double alpha = 1.0 - std::exp(-opticalDepth);
    out.straightArgb = packStraight(rgb[0], rgb[1], rgb[2], 1.0);
    out.alpha = clamp01(alpha);
    double weightedGranulation = 0;
    for (const auto& component : paint.pigments) {
        if (const auto* p = m_pigments.find(component.pigmentId))
            weightedGranulation += p->granulation * component.mass;
    }
    out.granulation = clamp01(weightedGranulation / pigmentMass
                              * (0.20 + surface.tooth * 0.40 + surface.granulationInteraction * 0.40)
                              * (0.35 + medium.pigmentSuspension * 0.65)
                              * (1.0 + clamp01(paint.settling) * 0.45));
    return out;
}

std::uint32_t MaterialEngine::composite(std::uint32_t destinationPremultiplied,
                                        const PaintBatch& paint,
                                        const MediumProfile& medium,
                                        const SurfaceProfile& surface,
                                        double coverage, double depositShare,
                                        double toothContact) const noexcept
{
    const auto optical = opticalResult(paint, medium, surface, depositShare);
    const double surfaceRetention = (1.0 - surface.paletteNonAbsorbency)
        * (0.55 + surface.absorbency * 0.45);
    const double contact = clamp01(finiteOrZero(toothContact));
    const double effectiveAlpha = optical.alpha * clamp01(coverage) * contact
        * (0.42 + surfaceRetention * 0.58);
    return blendPremultiplied(destinationPremultiplied, optical.straightArgb, effectiveAlpha);
}

MaterialSimulationStats MaterialEngine::advanceWetMaterial(MaterialTileStore& store,
                                                            double elapsedSeconds,
                                                            const SurfaceProfile& surface) const
{
    MaterialSimulationStats stats;
    if (!store.isValid() || !std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) {
        stats.failed = true;
        return stats;
    }
    const double dt = std::min(elapsedSeconds, 60.0);
    if (dt <= 0.0) return stats;
    try {
        const auto activeTiles = store.activeTilePoints();
        stats.activeTiles = activeTiles.size();
        for (const TilePoint tile : activeTiles) {
            const auto view = store.tileEntries(tile);
            const std::vector<MaterialEntry> entries(view.begin(), view.end());
            store.beginBatch();
            struct BatchEnd { MaterialTileStore& store; ~BatchEnd() { store.endBatch(); } } batchEnd{store};
            const auto& geometry = store.geometry();
            const auto tileSize = static_cast<std::int64_t>(geometry.tileSize());
            for (const auto& entry : entries) {
                if (entry.cell.water <= 1.0e-7F || entry.cell.drying >= 0.9999F) continue;
                if (entry.localIndex >= static_cast<std::size_t>(geometry.tileSize())
                        * static_cast<std::size_t>(geometry.tileSize())) continue;
                const TilePoint point{
                    tile.x * tileSize + static_cast<std::int64_t>(entry.localIndex % static_cast<std::size_t>(geometry.tileSize())),
                    tile.y * tileSize + static_cast<std::int64_t>(entry.localIndex / static_cast<std::size_t>(geometry.tileSize()))};
                const auto* composition = store.composition(entry.cell.compositionId);
                if (!composition || composition->mediumId.empty()) { stats.failed = true; return stats; }
                const double originalWater = entry.cell.water;
                const double pigmentMass = entry.cell.pigmentMass;
                const double binder = entry.cell.binder;
                const double filler = entry.cell.filler;
                const double totalMass = pigmentMass + originalWater + binder + filler;
                if (originalWater <= 1.0e-7 || totalMass <= 1.0e-12) continue;
                ++stats.cellsVisited;
                const MediumProfile* medium = findMediumProfile(composition->mediumId);
                if (!medium) medium = &gouacheProfile();
                const double porousAbsorption = clamp01(surface.absorbency)
                    * (0.35 + 0.65 * clamp01(surface.absorptionRate))
                    * clamp01(surface.porosity);
                const double ambientMoistureDamping = 1.0 - 0.45 * clamp01(surface.surfaceWetness);
                // Two distinct sinks act on water: absorption into porous stock
                // and evaporation from the exposed film. Exponential integration
                // is stable for large lazy catch-up intervals.
                const double evaporationRate = std::max(0.0, medium->dryingRate)
                    * (0.008 + clamp01(surface.dryingAcceleration) * 0.010
                       + clamp01(surface.roughness) * 0.004)
                    * ambientMoistureDamping;
                const double absorptionRate = porousAbsorption
                    * (0.012 + clamp01(medium->absorption) * 0.015);
                const double waterSurvival = std::exp(-(evaporationRate + absorptionRate) * dt);
                double nextWater = originalWater * waterSurvival;
                stats.waterEvaporated += originalWater * (1.0 - std::exp(-evaporationRate * dt));
                const double cureRate = evaporationRate * (0.35 + clamp01(surface.dryingAcceleration) * 0.4)
                    + absorptionRate * 0.22;
                double nextDrying = clamp01(1.0 - (1.0 - double(entry.cell.drying)) * std::exp(-cureRate * dt));

                double settlingTendency = 0.0;
                double granulationTendency = 0.0;
                if (pigmentMass > 1.0e-12 && !composition->pigments.empty()) {
                    for (const auto& component : composition->pigments) {
                        const auto* pigment = component.pigmentId.empty()
                            ? nullptr : m_pigments.find(component.pigmentId);
                        settlingTendency += component.mass * (pigment ? pigment->settling : 0.25);
                        granulationTendency += component.mass * (pigment ? pigment->granulation : 0.15);
                    }
                    settlingTendency = clamp01(settlingTendency);
                    granulationTendency = clamp01(granulationTendency);
                }
                const double valleyFactor = 1.0 - computeSurfaceToothHeight(point.x, point.y, surface);
                const double valleyBias = 0.70 + 0.65 * valleyFactor * clamp01(surface.granulationInteraction) * granulationTendency;
                const double liquidFraction = clamp01(originalWater / std::max(1.0e-12, totalMass));
                const double settleRate = settlingTendency * (1.0 - clamp01(medium->pigmentSuspension))
                    * liquidFraction * (0.008 + clamp01(surface.tooth) * 0.016) * valleyBias;
                const double nextSettling = clamp01(1.0 - (1.0 - double(entry.cell.settling)) * std::exp(-settleRate * dt));
                if (nextWater <= std::max(1.0e-6, (pigmentMass + binder + filler) * 1.0e-5)) {
                    nextWater = 0.0;
                    nextDrying = 1.0;
                }
                if (nextWater != originalWater || nextDrying != double(entry.cell.drying)
                    || nextSettling != double(entry.cell.settling)) {
                    const auto result = store.updateCellState(point, nextWater, binder, filler,
                                                              nextDrying, nextSettling);
                    if (result != MaterialTileStore::Result::Changed
                        && result != MaterialTileStore::Result::Unchanged) {
                        stats.failed = true;
                        return stats;
                    }
                    if (result == MaterialTileStore::Result::Changed) ++stats.cellsChanged;
                }
            }
        }

        // Conservative pairwise flow over sparse active cells. The cell list is
        // tile-local and already sorted; only its right and lower neighbours are
        // visited, so each edge is handled once and tile boundaries work the
        // same as interior boundaries. Wetness drives spreading; vertical
        // settling and paper edge response bias suspended pigment transport.
        for (const TilePoint tile : activeTiles) {
            const auto view = store.tileEntries(tile);
            const std::vector<MaterialEntry> entries(view.begin(), view.end());
            store.beginBatch();
            struct BatchEnd { MaterialTileStore& store; ~BatchEnd() { store.endBatch(); } } batchEnd{store};
            const auto& geometry = store.geometry();
            const auto tileSize = static_cast<std::int64_t>(geometry.tileSize());
            for (const auto& entry : entries) {
                if (entry.cell.water <= 1.0e-7F || entry.cell.drying >= 0.9999F) continue;
                const TilePoint point{
                    tile.x * tileSize + static_cast<std::int64_t>(entry.localIndex % static_cast<std::size_t>(geometry.tileSize())),
                    tile.y * tileSize + static_cast<std::int64_t>(entry.localIndex / static_cast<std::size_t>(geometry.tileSize()))};
                const auto source = store.batchAt(point);
                if (!source || source->water <= 1.0e-7 || source->drying >= 0.9999) continue;
                const MediumProfile* medium = findMediumProfile(source->mediumId);
                if (!medium) medium = &gouacheProfile();
                const double sourcePotential = source->wetness * (0.25 + clamp01(medium->waterSpread))
                    * (1.0 - clamp01(surface.flowResistance)) * (1.0 - source->drying);
                for (const TilePoint offset : {TilePoint{1, 0}, TilePoint{0, 1}}) {
                    const TilePoint neighbour{point.x + offset.x, point.y + offset.y};
                    if (!geometry.address(neighbour)) continue;
                    const auto target = store.batchAt(neighbour);
                    if (!target) { stats.failed = true; return stats; }
                    const MediumProfile* targetMedium = findMediumProfile(target->mediumId);
                    if (!targetMedium) targetMedium = medium;
                    const double targetPotential = target->totalMass() > 0.0
                        ? target->wetness * (0.25 + clamp01(targetMedium->waterSpread))
                            * (1.0 - clamp01(surface.flowResistance)) * (1.0 - target->drying)
                        : 0.0;
                    double gradient = sourcePotential - targetPotential;
                    if (offset.y > 0)
                        gradient += source->settling * 0.12 * (1.0 - clamp01(medium->pigmentSuspension));
                    if (std::abs(gradient) <= 1.0e-5) continue;
                    const bool sourceDonor = gradient > 0.0;
                    const TilePoint donorPoint = sourceDonor ? point : neighbour;
                    const TilePoint receiverPoint = sourceDonor ? neighbour : point;
                    const PaintBatch& donor = sourceDonor ? *source : *target;
                    const PaintBatch& receiver = sourceDonor ? *target : *source;
                    const MediumProfile* donorMedium = sourceDonor ? medium : targetMedium;
                    const MediumProfile* receiverMedium = sourceDonor ? targetMedium : medium;
                    const double mobility = clamp01(1.0 - donor.drying);
                    const double edgeBoost = 1.0 + clamp01(surface.edgeAccumulation) * std::abs(gradient) * 1.5;
                    const double fraction = std::min(0.14, std::abs(gradient) * dt
                        * (0.025 + clamp01(donorMedium->waterSpread) * 0.055)
                        * mobility * edgeBoost);
                    const double movedMass = donor.totalMass() * fraction;
                    if (movedMass <= 1.0e-8) continue;
                    PaintBatch moved;
                    const auto removed = store.take(donorPoint, movedMass, &moved);
                    if (removed != MaterialTileStore::Result::Changed) continue;
                    // Capillary water advances faster than heavy settled pigment;
                    // retain excess settled solids on the donor cell without
                    // creating new composition proportions.
                    const double pigmentTransportRatio = clamp01((1.0 - donor.settling * 0.68)
                        * (0.30 + 0.70 * clamp01(donorMedium->pigmentSuspension))
                        * (1.0 - clamp01(surface.flowResistance) * 0.30));
                    PaintBatch retainedSolids;
                    if (pigmentTransportRatio < 0.999 && (moved.pigmentMass() > 0.0 || moved.binder > 0.0 || moved.filler > 0.0)) {
                        retainedSolids = moved;
                        retainedSolids.water = 0.0;
                        retainedSolids.scale(1.0 - pigmentTransportRatio);
                        refreshBatchState(retainedSolids);
                        for (auto& p : moved.pigments) p.mass *= pigmentTransportRatio;
                        moved.binder *= pigmentTransportRatio;
                        moved.filler *= pigmentTransportRatio;
                        refreshBatchState(moved);
                    }
                    // When capillary water flows into an existing drier pigment cell,
                    // rewet it so dry marks loosen and mobilize naturally.
                    if (moved.water > 1.0e-8 && receiver.totalMass() > 1.0e-9 && receiver.drying > 0.0) {
                        const double wetting = clamp01((moved.water / std::max(1.0e-12, receiver.totalMass() + moved.water))
                            * receiverMedium->rewetting * surface.rewetting * 1.4);
                        const double nextDrying = receiver.drying * (1.0 - wetting);
                        const double nextSettling = receiver.settling * (1.0 - wetting * 0.30);
                        store.updateCellState(receiverPoint, receiver.water, receiver.binder,
                                              receiver.filler, nextDrying, nextSettling);
                    }
                    const auto deposited = store.deposit(receiverPoint, moved);
                    if (retainedSolids.totalMass() > 1.0e-12)
                        store.deposit(donorPoint, retainedSolids);
                    if (deposited != MaterialTileStore::Result::Changed
                        && deposited != MaterialTileStore::Result::Unchanged) {
                        // Keep a failed transfer conservative. The source cell
                        // is still allocated, so put its batch back before
                        // reporting that this simulation tick was incomplete.
                        store.deposit(donorPoint, moved);
                        stats.failed = true;
                        return stats;
                    }
                    if (deposited == MaterialTileStore::Result::Changed) {
                        ++stats.transfers;
                        stats.materialMoved += moved.totalMass();
                    }
                }
            }
        }
        store.reclaimUnreferenced();
    } catch (const std::bad_alloc&) {
        stats.failed = true;
    } catch (const std::length_error&) {
        stats.failed = true;
    }
    return stats;
}

SparseTileStore::WriteResult MaterialDab::apply(SparseTileStore& store,
                                                const BrushDefinition& brush,
                                                BrushReservoir& reservoir,
                                                double centerX, double centerY,
                                                const BrushInput& input)
{
    using Result = SparseTileStore::WriteResult;
    if (!brush.valid() || !std::isfinite(centerX) || !std::isfinite(centerY)
        || !std::isfinite(input.pressure) || input.pressure < 0 || input.pressure > 1
        || !store.isValid()) return Result::InvalidPixel;
    reservoir.reloadIfNeeded(brush);
    const auto& geometry = store.geometry();
    const double angle = brush.rotationRadians + input.rotationRadians;
    const double ca = std::cos(angle), sa = std::sin(angle);
    const double carriedMass = reservoir.batch().totalMass();
    const double waterFraction = carriedMass > 0 ? reservoir.batch().water / carriedMass : 0.0;
    const double bodyDamping = 1.0 - 0.55 * clamp01(brush.paintBody) * (0.35 + 0.65 * clamp01(m_medium.viscosity));
    const double spread = 1.0 + clamp01(m_medium.waterSpread) * bodyDamping
        * waterFraction * (0.15 + brush.waterSpread + brush.pigmentDiffusion * 0.5);
    const double dryBrushFactor = brush.toothResponse > 0.45
        ? clamp01((0.42 - waterFraction) / 0.42)
            * clamp01((brush.toothResponse - 0.45) / 0.55)
            * (0.25 + 0.75 * clamp01(m_surface.tooth))
        : 0.0;
    const double contactCurve = brush.contactPressureCurve.evaluate(input.pressure);
    const double contactRadiusScale = 1.0 - clamp01(brush.contactPressureScale) * 0.55 * (1.0 - contactCurve);
    const double contactFirmness = 1.0 - clamp01(brush.contactPressureScale) * 0.40 * (1.0 - contactCurve);
    const double tiltFactor = brush.tiltCurve.evaluate(input.tilt);
    const double rxBase = brush.size * 0.5 * spread * contactRadiusScale;
    const double rx = rxBase * (1.0 + clamp01(brush.tiltFlattening) * tiltFactor * 0.6);
    const double ry = rxBase / brush.aspect;
    const double bound = std::max(rx, ry) + 1.0;
    const auto left = static_cast<std::int64_t>(std::max(0.0, std::floor(centerX - bound)));
    const auto top = static_cast<std::int64_t>(std::max(0.0, std::floor(centerY - bound)));
    const auto right = static_cast<std::int64_t>(std::min(double(geometry.width()), std::ceil(centerX + bound)));
    const auto bottom = static_cast<std::int64_t>(std::min(double(geometry.height()), std::ceil(centerY + bound)));
    if (left >= right || top >= bottom) return Result::Unchanged;

    double contactWeight = 0.0;
    for (auto y = top; y < bottom; ++y)
        for (auto x = left; x < right; ++x)
            contactWeight += dabContactAt(brush, x, y, centerX, centerY, ca, sa, rx, ry,
                                          contactFirmness, &m_surface, dryBrushFactor).contact;
    if (contactWeight <= 0) return Result::Unchanged;
    constexpr double kPi = 3.14159265358979323846;
    const double ellipseArea = std::max(1.0, kPi * rx * ry);
    const double fillFraction = contactWeight / ellipseArea;
    const double sizeScale = std::max(1.0, brush.size) / 18.0;
    const double normalizedArea = fillFraction * 0.52 * sizeScale * sizeScale;
    const double requested = reservoir.transferredAmount(brush, input, normalizedArea);
    if (requested <= 0) return Result::Unchanged;
    PaintBatch transferred = reservoir.withdraw(requested);
    if (transferred.totalMass() <= 0) return Result::Unchanged;

    store.beginBatch();
    bool changed = false;
    const double particleGranulation = m_engine.opticalResult(transferred, m_medium, m_surface).granulation;
    for (auto y = top; y < bottom; ++y) {
        for (auto x = left; x < right; ++x) {
            const auto [contact, radial] = dabContactAt(brush, x, y, centerX, centerY, ca, sa, rx, ry,
                                                        contactFirmness, &m_surface, dryBrushFactor);
            if (contact <= 0) continue;
            const double toothHeight = computeSurfaceToothHeight(x, y, m_surface);
            const double valley = 1.0 - toothHeight;
            const double grain = brush.granulationResponse * particleGranulation * clamp01(m_surface.granulationInteraction);
            const double tooth = (1.0 - brush.toothResponse * m_surface.tooth * (1.0 - toothHeight) * 0.5)
                * (1.0 - grain * (1.0 - valley) * 0.35);
            const double edge = 1.0 + brush.edgeAccumulation * std::exp(-std::pow((1.0 - radial) * brush.size, 2.0) / 3.0);
            const double diffusion = 1.0 + brush.pigmentDiffusion * m_medium.waterSpread * waterFraction;
            // Local deposited share of the transferred batch at this pixel,
            // scaled to the shared reference unit so optical depth matches
            // MaterialTileStore + MaterialRenderer.
            constexpr double ReferenceBatchPixels = 16384.0;
            const double depositShare = ReferenceBatchPixels * contact * edge * diffusion / contactWeight;
            const auto old = store.pixel({x, y}).value_or(0U);
            const auto next = m_engine.composite(old, transferred, m_medium, m_surface,
                                                 contact * edge, depositShare * (1.0 + particleGranulation * 0.08), tooth);
            const auto result = store.setPixel({x, y}, next);
            if (result == Result::Changed) changed = true;
            else if (result != Result::Unchanged) { store.endBatch(); return result; }
        }
    }
    store.endBatch();
    return changed ? Result::Changed : Result::Unchanged;
}

SparseTileStore::WriteResult MaterialDab::apply(MaterialTileStore& store,
                                                const BrushDefinition& brush,
                                                BrushReservoir& reservoir,
                                                double centerX, double centerY,
                                                const BrushInput& input) try
{
    using Result = SparseTileStore::WriteResult;
    if (!brush.valid() || !std::isfinite(centerX) || !std::isfinite(centerY)
        || !std::isfinite(input.pressure) || input.pressure < 0 || input.pressure > 1
        || !std::isfinite(input.speed) || input.speed < 0 || input.speed > 1
        || !std::isfinite(input.tilt) || input.tilt < 0 || input.tilt > 1
        || !std::isfinite(input.rotationRadians) || !store.isValid()) return Result::InvalidPixel;
    reservoir.reloadIfNeeded(brush);
    const auto& geometry = store.geometry();
    const double angle = brush.rotationRadians + input.rotationRadians;
    const double ca = std::cos(angle), sa = std::sin(angle);
    const double carriedMass = reservoir.batch().totalMass();
    const double waterFraction = carriedMass > 0 ? reservoir.batch().water / carriedMass : 0.0;
    const TilePoint centerTilePoint{
        std::clamp<std::int64_t>(std::llround(centerX), 0, std::max<std::int64_t>(0, geometry.width() - 1)),
        std::clamp<std::int64_t>(std::llround(centerY), 0, std::max<std::int64_t>(0, geometry.height() - 1))};
    const auto centerPrior = store.cellAt(centerTilePoint);
    const double priorCenterWet = (centerPrior && !centerPrior->empty())
        ? double(centerPrior->wetness) * (1.0 - double(centerPrior->drying))
        : 0.0;
    const double ambientWetness = clamp01(std::max(m_surface.surfaceWetness, priorCenterWet));
    const double bodyDamping = 1.0 - 0.55 * clamp01(brush.paintBody) * (0.35 + 0.65 * clamp01(m_medium.viscosity));
    const double wetOnWetBoost = 1.0 + ambientWetness * 0.25 * clamp01(m_medium.waterSpread) * bodyDamping;
    const double spread = (1.0 + clamp01(m_medium.waterSpread) * bodyDamping
        * waterFraction * (0.15 + brush.waterSpread + brush.pigmentDiffusion * 0.5)) * wetOnWetBoost;
    const double dryBrushFactor = brush.toothResponse > 0.45
        ? clamp01((0.42 - waterFraction) / 0.42)
            * clamp01((brush.toothResponse - 0.45) / 0.55)
            * (0.25 + 0.75 * clamp01(m_surface.tooth))
            * (1.0 - ambientWetness * 0.8)
        : 0.0;
    const double contactCurve = brush.contactPressureCurve.evaluate(input.pressure);
    const double contactRadiusScale = 1.0 - clamp01(brush.contactPressureScale) * 0.55 * (1.0 - contactCurve);
    const double contactFirmness = 1.0 - clamp01(brush.contactPressureScale) * 0.40 * (1.0 - contactCurve);
    const double tiltFactor = brush.tiltCurve.evaluate(input.tilt);
    const double rxBase = brush.size * 0.5 * spread * contactRadiusScale;
    const double rx = rxBase * (1.0 + clamp01(brush.tiltFlattening) * tiltFactor * 0.6);
    const double ry = rxBase / brush.aspect;
    const double bound = std::max(rx, ry) + 1.0;
    const auto left = static_cast<std::int64_t>(std::max(0.0, std::floor(centerX - bound)));
    const auto top = static_cast<std::int64_t>(std::max(0.0, std::floor(centerY - bound)));
    const auto right = static_cast<std::int64_t>(std::min(double(geometry.width()), std::ceil(centerX + bound)));
    const auto bottom = static_cast<std::int64_t>(std::min(double(geometry.height()), std::ceil(centerY + bound)));
    if (left >= right || top >= bottom) return Result::Unchanged;

    double contactWeight = 0.0;
    for (auto y = top; y < bottom; ++y)
        for (auto x = left; x < right; ++x)
            contactWeight += dabContactAt(brush, x, y, centerX, centerY, ca, sa, rx, ry,
                                          contactFirmness, &m_surface, dryBrushFactor).contact;
    if (contactWeight <= 0.0) return Result::Unchanged;
    store.beginBatch();
    struct BatchEnd { MaterialTileStore& store; ~BatchEnd() { store.endBatch(); } } batchEnd{store};
    bool pickedUp = false;
    const bool isLiftingBrush = brush.paintLiftability > 0.70 && brush.releaseRate <= 0.03;
    const double brushMass = reservoir.quantity();
    const double brushWaterFraction = brushMass > 0.0 ? reservoir.batch().water / brushMass : 0.0;
    const double pickupScale = isLiftingBrush ? 0.55 : 0.12;
    const double pickupBudget = std::min(reservoir.remainingCapacity(),
        brush.pickupRate * (contactWeight / (isLiftingBrush ? 36.0 : 64.0))
        * pickupScale * (0.25 + brushWaterFraction));
    if (pickupBudget > 1.0e-9) {
        double distributed = 0.0;
        for (auto y = top; y < bottom; ++y) {
            for (auto x = left; x < right; ++x) {
                const double contact = dabContactAt(brush, x, y, centerX, centerY, ca, sa, rx, ry,
                                                    contactFirmness, &m_surface, dryBrushFactor).contact;
                if (contact <= 0.0) continue;
                const TilePoint point{static_cast<std::int64_t>(x), static_cast<std::int64_t>(y)};
                const auto prior = store.batchAt(point);
                if (!prior || prior->totalMass() <= 1.0e-9) continue;
                const MediumProfile* priorMedium = findMediumProfile(prior->mediumId);
                if (!priorMedium) priorMedium = &m_medium;
                double staining = 0.0;
                double liftability = 0.5;
                const double priorPigment = prior->pigmentMass();
                if (priorPigment > 1.0e-12) {
                    liftability = 0.0;
                    for (const auto& component : prior->pigments) {
                        const auto* pigment = component.pigmentId.empty()
                            ? nullptr : m_engine.pigments().find(component.pigmentId);
                        staining += component.mass * (pigment ? pigment->staining : 0.5);
                        liftability += component.mass * (pigment ? pigment->liftability : 0.5);
                    }
                    staining = clamp01(staining / priorPigment);
                    liftability = clamp01(liftability / priorPigment);
                }
                const double wetMobility = prior->wetness * priorMedium->waterSpread
                    * (1.0 - prior->drying) * (1.0 - m_surface.flowResistance * 0.55);
                const double rewetMobility = brushWaterFraction * brush.rewetting
                    * priorMedium->rewetting * m_surface.rewetting
                    * brush.paintLiftability * (1.0 - brush.staining)
                    * (1.0 - staining * 0.68) * (0.28 + 0.72 * liftability)
                    * (0.22 + 0.55 * (1.0 - prior->drying));
                const double mobility = clamp01(std::max(wetMobility, rewetMobility));
                if (mobility <= 1.0e-5) continue;
                const double substrateEase = 1.0 - clamp01(m_surface.tooth * 0.32 + m_surface.absorbency * 0.24);
                const double maxLiftShare = isLiftingBrush
                    ? clamp01(0.75 * (1.0 - staining * 0.55) * (0.40 + 0.60 * liftability) * substrateEase)
                    : 0.35;
                const double desired = isLiftingBrush
                    ? prior->totalMass() * maxLiftShare * mobility * clamp01(contact) * (0.45 + 0.55 * input.pressure)
                    : std::min(prior->totalMass() * maxLiftShare,
                        pickupBudget * (contact / contactWeight) * mobility * (0.25 + input.pressure));
                const double amount = std::min(prior->totalMass() * maxLiftShare, desired);
                if (amount <= 1.0e-9) continue;

                if (isLiftingBrush) {
                    // Per-pigment selective lifting: staining pigments cling to
                    // paper fibres while non-staining pigments lift more freely.
                    PaintBatch remaining = *prior;
                    PaintBatch lifted;
                    lifted.mediumId = prior->mediumId;
                    const double baseShare = clamp01(amount / std::max(1e-12, prior->totalMass()));
                    double liftedSum = 0.0;
                    for (std::size_t i = 0; i < remaining.pigments.size(); ++i) {
                        const auto& comp = prior->pigments[i];
                        const auto* pigment = comp.pigmentId.empty()
                            ? nullptr : m_engine.pigments().find(comp.pigmentId);
                        const double compLift = pigment
                            ? clamp01(pigment->liftability * (1.0 - pigment->staining * 0.60))
                            : 0.5;
                        const double takeShare = clamp01(baseShare * (0.45 + 0.90 * compLift));
                        const double takenMass = comp.mass * takeShare;
                        if (takenMass > 1.0e-12) {
                            remaining.pigments[i].mass -= takenMass;
                            if (comp.pigmentId.empty()) lifted.addResidual(comp.residualArgb, takenMass);
                            else lifted.addPigment(comp.pigmentId, takenMass);
                            liftedSum += takenMass;
                        }
                    }
                    remaining.pigments.erase(
                        std::remove_if(remaining.pigments.begin(), remaining.pigments.end(),
                            [](const PigmentComponent& c) { return c.mass <= 1.0e-12; }),
                        remaining.pigments.end());
                    const double waterTaken = prior->water * baseShare * 0.55;
                    const double binderTaken = prior->binder * baseShare;
                    const double fillerTaken = prior->filler * baseShare;
                    remaining.water = std::max(0.0, remaining.water - waterTaken);
                    remaining.binder = std::max(0.0, remaining.binder - binderTaken);
                    remaining.filler = std::max(0.0, remaining.filler - fillerTaken);
                    lifted.addWater(waterTaken);
                    lifted.addBinder(binderTaken);
                    lifted.addFiller(fillerTaken);
                    refreshBatchState(remaining);
                    if (liftedSum > 1.0e-9) {
                        const auto setRes = store.setBatch(point, remaining);
                        if (setRes == MaterialTileStore::Result::Changed) {
                            pickedUp = true;
                            distributed += lifted.totalMass();
                            const double sampleAmount = std::min(lifted.totalMass(),
                                (lifted.totalMass() / std::max(1.0, contactWeight)) * brush.pickupRate * 0.25);
                            if (sampleAmount > 1.0e-9) reservoir.pickup(lifted, sampleAmount);
                        } else if (setRes != MaterialTileStore::Result::Unchanged) {
                            return Result::AllocationFailed;
                        }
                    }
                } else {
                    PaintBatch lifted;
                    const auto takeResult = store.take(point, amount, &lifted);
                    if (takeResult == MaterialTileStore::Result::Changed) {
                        const double liftedMass = lifted.totalMass();
                        if (reservoir.pickup(lifted, liftedMass)) {
                            pickedUp = true;
                            distributed += liftedMass;
                        } else {
                            store.deposit(point, lifted);
                        }
                    } else if (takeResult != MaterialTileStore::Result::Unchanged) {
                        return Result::AllocationFailed;
                    }
                }
            }
        }
        (void)distributed;
    }

    constexpr double kPi = 3.14159265358979323846;
    const double ellipseArea = std::max(1.0, kPi * rx * ry);
    const double fillFraction = contactWeight / ellipseArea;
    const double sizeScale = std::max(1.0, brush.size) / 18.0;
    const double normalizedArea = fillFraction * 0.52 * sizeScale * sizeScale;
    const double requested = reservoir.transferredAmount(brush, input, normalizedArea);
    if (requested <= 0.0) return pickedUp ? Result::Changed : Result::Unchanged;
    PaintBatch transferred = reservoir.withdraw(requested);
    const double transferredMass = transferred.totalMass();
    if (transferredMass <= 0.0) return pickedUp ? Result::Changed : Result::Unchanged;

    const bool isCleanWater = transferred.pigmentMass() <= 1e-12 && transferred.water > 0.0;
    const double particleGranulation = m_engine.opticalResult(transferred, m_medium, m_surface).granulation;
    const double edgeStrength = (brush.edgeAccumulation
        + clamp01(m_surface.edgeAccumulation) * 0.35 * clamp01(m_medium.waterSpread))
        * (1.0 - 0.70 * ambientWetness);
    const auto weightAt = [&](std::int64_t x, std::int64_t y, double contact, double radial) {
        if (contact <= 0.0) return 0.0;
        const double toothHeight = computeSurfaceToothHeight(x, y, m_surface);
        const double valley = 1.0 - toothHeight;
        const double grain = brush.granulationResponse * particleGranulation * clamp01(m_surface.granulationInteraction);
        const double tooth = (1.0 - brush.toothResponse * m_surface.tooth * (1.0 - toothHeight) * 0.45)
            * (1.0 + grain * (valley - 0.5) * 1.15);
        const double edge = 1.0 + edgeStrength
            * std::exp(-std::pow((1.0 - radial) * brush.size, 2.0) / 3.0);
        const double diffusion = 1.0 + brush.pigmentDiffusion * m_medium.waterSpread * waterFraction;
        return std::max(0.0, contact * tooth * edge * diffusion);
    };
    double totalWeight = 0.0;
    for (auto y = top; y < bottom; ++y) for (auto x = left; x < right; ++x) {
        const auto [contact, radial] = dabContactAt(brush, x, y, centerX, centerY, ca, sa, rx, ry,
                                                    contactFirmness, &m_surface, dryBrushFactor);
        totalWeight += weightAt(x, y, contact, radial);
    }
    if (!(totalWeight > 0.0) || !std::isfinite(totalWeight)) {
        reservoir.pickup(transferred, transferredMass);
        return Result::InvalidPixel;
    }

    bool changed = pickedUp;
    Result failure = Result::Unchanged;
    for (auto y = top; y < bottom; ++y) {
        for (auto x = left; x < right; ++x) {
            const auto [contact, radial] = dabContactAt(brush, x, y, centerX, centerY, ca, sa, rx, ry,
                                                        contactFirmness, &m_surface, dryBrushFactor);
            const double weight = weightAt(x, y, contact, radial);
            if (weight <= 0.0) continue;
            PaintBatch part = transferred;
            part.scale(weight / totalWeight);
            // Absorption into a porous sheet removes liquid from the paint
            // phase; pigment and binder retention independently control what
            // remains in the material cell.
            const double absorption = clamp01(m_surface.absorbency)
                * (0.35 + 0.65 * clamp01(m_surface.absorptionRate))
                * clamp01(m_surface.porosity) * clamp01(m_medium.absorption);
            const double dryingAccel = clamp01((brush.dryingRate - 1.0) * 0.25);
            part.water *= clamp01(m_surface.waterRetention) * (1.0 - 0.45 * absorption) * (1.0 - 0.35 * dryingAccel);
            const double pigmentRetained = clamp01(m_surface.pigmentRetention)
                * (1.0 - clamp01(m_surface.porosity) * 0.08);
            for (auto& pigment : part.pigments) pigment.mass *= pigmentRetained;
            part.binder *= clamp01(m_surface.binderRetention);
            part.filler *= clamp01(m_surface.pigmentRetention);
            part.drying = clamp01(part.drying + (brush.dryingRate - 1.0) * 0.12);
            refreshBatchState(part);
            const TilePoint point{static_cast<std::int64_t>(x), static_cast<std::int64_t>(y)};
            const auto prior = store.cellAt(point);
            if (prior && !prior->empty() && part.water > 0.0) {
                const double priorMass = double(prior->pigmentMass) + double(prior->water)
                    + double(prior->binder) + double(prior->filler);
                if (priorMass > 0.0) {
                    const double wetting = clamp01((part.water / std::max(1.0e-12, part.totalMass()))
                        * m_medium.rewetting * brush.rewetting * m_surface.rewetting);
                    const double nextWater = double(prior->water)
                        + (isCleanWater ? std::max(part.water, contact * 0.35 * brush.rewetting) : 0.0);
                    const double nextDrying = double(prior->drying) * (1.0 - wetting);
                    const double nextSettling = double(prior->settling) * (1.0 - wetting * 0.35);
                    const auto stateResult = store.updateCellState(point, nextWater, prior->binder,
                                                                   prior->filler, nextDrying, nextSettling);
                    if (stateResult != MaterialTileStore::Result::Changed
                        && stateResult != MaterialTileStore::Result::Unchanged) {
                        failure = Result::AllocationFailed;
                        break;
                    }
                }
            }
            const auto result = store.deposit(point, part);
            if (result == MaterialTileStore::Result::Changed) changed = true;
            else if (result != MaterialTileStore::Result::Unchanged) {
                switch (result) {
                case MaterialTileStore::Result::OutOfBounds: failure = Result::OutOfBounds; break;
                case MaterialTileStore::Result::InvalidData: failure = Result::InvalidPixel; break;
                case MaterialTileStore::Result::DocumentMemoryLimit: failure = Result::DocumentMemoryLimit; break;
                case MaterialTileStore::Result::TrackingLimit: failure = Result::TrackingLimit; break;
                case MaterialTileStore::Result::AllocationFailed: failure = Result::AllocationFailed; break;
                case MaterialTileStore::Result::Changed: case MaterialTileStore::Result::Unchanged: break;
                }
                break;
            }
        }
        if (failure != Result::Unchanged) break;
    }
    if (failure == Result::Unchanged && isCleanWater) {
        // Clean Water rewets and diffuses mobile pigment down its concentration
        // gradient into adjacent wet cells without creating pigment or erasing.
        for (auto y = top; y < bottom - 1; ++y) {
            for (auto x = left; x < right - 1; ++x) {
                const double contact = dabContactAt(brush, x, y, centerX, centerY, ca, sa, rx, ry,
                                                    contactFirmness, &m_surface, 0.0).contact;
                if (contact <= 0.0) continue;
                const TilePoint p{static_cast<std::int64_t>(x), static_cast<std::int64_t>(y)};
                const auto a = store.batchAt(p);
                if (!a || a->totalMass() <= 1.0e-9) continue;
                for (const TilePoint off : {TilePoint{1, 0}, TilePoint{0, 1}}) {
                    const TilePoint q{p.x + off.x, p.y + off.y};
                    if (!geometry.address(q)) continue;
                    const auto b = store.batchAt(q);
                    if (!b || b->water <= 1.0e-9) continue;
                    const double aPig = a->pigmentMass();
                    const double bPig = b->pigmentMass();
                    if (std::abs(aPig - bPig) <= 1.0e-8) continue;
                    const bool aDonor = aPig > bPig;
                    const TilePoint donorPt = aDonor ? p : q;
                    const TilePoint recvPt = aDonor ? q : p;
                    const PaintBatch& donor = aDonor ? *a : *b;
                    const double mobility = clamp01(1.0 - donor.drying) * clamp01(donor.wetness)
                        * clamp01(m_surface.rewetting) * (1.0 - clamp01(m_surface.flowResistance) * 0.40);
                    const double moveMass = donor.totalMass() * std::min(0.24, 0.20 * contact * mobility);
                    if (moveMass <= 1.0e-8) continue;
                    PaintBatch moved;
                    if (store.take(donorPt, moveMass, &moved) == MaterialTileStore::Result::Changed) {
                        if (store.deposit(recvPt, moved) == MaterialTileStore::Result::Changed) changed = true;
                        else store.deposit(donorPt, moved);
                    }
                }
            }
        }
    }
    if (failure != Result::Unchanged) {
        reservoir.pickup(transferred, transferredMass);
        return failure;
    }
    return changed ? Result::Changed : Result::Unchanged;
} catch (const std::bad_alloc&) { return SparseTileStore::WriteResult::AllocationFailed; }
  catch (const std::length_error&) { return SparseTileStore::WriteResult::AllocationFailed; }

MaterialStroke::MaterialStroke(SparseTileStore& surfacePixels, BrushDefinition brush,
                               BrushReservoir& reservoir, const MaterialEngine& engine,
                               const MediumProfile& medium, const SurfaceProfile& surface)
    : m_surfacePixels(&surfacePixels), m_brush(std::move(brush)), m_reservoir(reservoir),
      m_dab(engine, medium, surface),
      m_spacing(std::max(0.5, m_brush.size * m_brush.spacingRatio)),
      m_remaining(m_spacing)
{
    if (!m_brush.valid() || !surfacePixels.isValid()) {
        m_failed = true;
        m_failureReason = SparseTileStore::WriteResult::InvalidPixel;
    }
}

MaterialStroke::MaterialStroke(MaterialTileStore& surfaceMaterial, BrushDefinition brush,
                               BrushReservoir& reservoir, const MaterialEngine& engine,
                               const MediumProfile& medium, const SurfaceProfile& surface,
                               bool eraseMaterial)
    : m_surfaceMaterial(&surfaceMaterial), m_brush(std::move(brush)), m_reservoir(reservoir),
      m_dab(engine, medium, surface),
      m_spacing(std::max(0.5, m_brush.size * m_brush.spacingRatio)),
      m_remaining(m_spacing), m_eraseMaterial(eraseMaterial)
{
    if (!m_brush.valid() || !surfaceMaterial.isValid()) {
        m_failed = true;
        m_failureReason = SparseTileStore::WriteResult::InvalidPixel;
    }
}

SparseTileStore::WriteResult MaterialStroke::eraseMaterialDab(MaterialSample sample) try
{
    using Result = SparseTileStore::WriteResult;
    if (!m_surfaceMaterial) return Result::InvalidPixel;
    auto& store = *m_surfaceMaterial;
    const auto& geometry = store.geometry();
    if (sample.input.pressure <= 0.0) return Result::Unchanged;
    const double angle = m_brush.rotationRadians + sample.input.rotationRadians;
    const double ca = std::cos(angle), sa = std::sin(angle);
    const double rx = m_brush.size * 0.5;
    const double ry = rx / m_brush.aspect;
    const double bound = std::max(rx, ry) + 1.0;
    const auto left = static_cast<std::int64_t>(std::max(0.0, std::floor(sample.x - bound)));
    const auto top = static_cast<std::int64_t>(std::max(0.0, std::floor(sample.y - bound)));
    const auto right = static_cast<std::int64_t>(std::min(double(geometry.width()), std::ceil(sample.x + bound)));
    const auto bottom = static_cast<std::int64_t>(std::min(double(geometry.height()), std::ceil(sample.y + bound)));
    if (left >= right || top >= bottom) return Result::Unchanged;

    store.beginBatch();
    struct BatchEnd { MaterialTileStore& store; ~BatchEnd() { store.endBatch(); } } batchEnd{store};
    bool changed = false;
    Result failure = Result::Unchanged;
    for (auto y = top; y < bottom && failure == Result::Unchanged; ++y) {
        for (auto x = left; x < right; ++x) {
            const double contact = dabContactAt(m_brush, x, y, sample.x, sample.y, ca, sa, rx, ry, 1.0).contact;
            if (contact <= 0.0) continue;
            const double coverage = clamp01(contact * sample.input.pressure * m_brush.contactTransfer);
            if (coverage <= 0.0) continue;
            const auto result = store.scaleCell({x, y}, 1.0 - coverage);
            if (result == MaterialTileStore::Result::Changed) changed = true;
            else if (result != MaterialTileStore::Result::Unchanged) {
                switch (result) {
                case MaterialTileStore::Result::OutOfBounds: failure = Result::OutOfBounds; break;
                case MaterialTileStore::Result::InvalidData: failure = Result::InvalidPixel; break;
                case MaterialTileStore::Result::DocumentMemoryLimit: failure = Result::DocumentMemoryLimit; break;
                case MaterialTileStore::Result::TrackingLimit: failure = Result::TrackingLimit; break;
                case MaterialTileStore::Result::AllocationFailed: failure = Result::AllocationFailed; break;
                case MaterialTileStore::Result::Changed: case MaterialTileStore::Result::Unchanged: break;
                }
                break;
            }
        }
    }
    return failure != Result::Unchanged ? failure : changed ? Result::Changed : Result::Unchanged;
} catch (const std::bad_alloc&) { return SparseTileStore::WriteResult::AllocationFailed; }
  catch (const std::length_error&) { return SparseTileStore::WriteResult::AllocationFailed; }

bool MaterialStroke::stamp(MaterialSample sample)
{
    using Result = SparseTileStore::WriteResult;
    if (m_failed) return false;
    if (!validMaterialSample(sample) || sample.input.pressure < 0 || sample.input.pressure > 1
        || sample.input.speed < 0 || sample.input.speed > 1
        || sample.input.tilt < 0 || sample.input.tilt > 1) {
        m_failed = true;
        m_failureReason = Result::InvalidPixel;
        return false;
    }
    const auto& geometry = m_surfacePixels ? m_surfacePixels->geometry() : m_surfaceMaterial->geometry();
    const double reach = m_brush.size * 4.0 + 1.0;
    if (sample.x < -reach || sample.y < -reach
        || sample.x > double(geometry.width()) + reach
        || sample.y > double(geometry.height()) + reach)
        return true;
    const auto result = m_eraseMaterial
        ? eraseMaterialDab(sample)
        : m_surfacePixels
            ? m_dab.apply(*m_surfacePixels, m_brush, m_reservoir, sample.x, sample.y, sample.input)
            : m_dab.apply(*m_surfaceMaterial, m_brush, m_reservoir, sample.x, sample.y, sample.input);
    if (result == Result::Unchanged || result == Result::Changed) return true;
    m_failed = true;
    m_failureReason = result;
    return false;
}

bool MaterialStroke::begin(MaterialSample sample)
{
    if (m_started || m_failed) return false;
    if (!m_eraseMaterial && m_brush.autoReloadOnStroke && m_brush.valid()) {
        if (!m_reservoir.reload(m_brush)) {
            m_failed = true;
            m_failureReason = SparseTileStore::WriteResult::InvalidPixel;
            return false;
        }
    }
    if (!stamp(sample)) return false;
    m_previous = sample;
    m_started = true;
    m_remaining = m_spacing;
    return true;
}

bool MaterialStroke::extend(MaterialSample sample)
{
    if (!m_started || m_failed || !validMaterialSample(sample)) {
        if (!m_failed) {
            m_failed = true;
            m_failureReason = SparseTileStore::WriteResult::InvalidPixel;
        }
        return false;
    }
    const double dx = sample.x - m_previous.x;
    const double dy = sample.y - m_previous.y;
    const double length = std::hypot(dx, dy);
    if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(length)) {
        m_failed = true;
        m_failureReason = SparseTileStore::WriteResult::InvalidPixel;
        return false;
    }
    if (length == 0.0) {
        m_previous = sample;
        return stamp(sample);
    }

    // Clip distant pointer segments to the sparse canvas bounds before making
    // dabs. This prevents off-canvas coordinates from causing overflow or
    // work proportional to the cursor's distance outside the artwork.
    const auto& geometry = m_surfacePixels ? m_surfacePixels->geometry() : m_surfaceMaterial->geometry();
    const double reach = m_brush.size * 4.0 + 1.0;
    double enter = 0.0, leave = 1.0;
    if (!clipAxis(m_previous.x, dx, -reach, double(geometry.width()) + reach, enter, leave)
        || !clipAxis(m_previous.y, dy, -reach, double(geometry.height()) + reach, enter, leave)) {
        m_previous = sample;
        return true;
    }
    const double firstDistance = std::max(m_remaining, enter * length);
    const double lastDistance = leave * length;
    const double tangentAngle = std::atan2(dy, dx);
    double distance = firstDistance;
    while (distance <= lastDistance) {
        const double t = distance / length;
        MaterialSample dabSample = interpolate(m_previous, sample, t);
        if (m_brush.followDirection) dabSample.input.rotationRadians += tangentAngle;
        if (!stamp(dabSample)) return false;
        distance += m_spacing;
    }
    m_remaining = distance - length;
    m_previous = sample;
    return true;
}

bool MaterialStroke::end(MaterialSample sample)
{
    if (!extend(sample)) return false;
    if (!stamp(sample)) return false;
    m_started = false;
    return true;
}

} // namespace komapaint::material
