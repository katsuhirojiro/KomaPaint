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

Spectrum rgbToReflectance(std::uint32_t argb) noexcept
{
    const auto rgb = unpackLinear(argb);
    Spectrum result{};
    // Smooth basis functions provide a continuous spectral approximation for
    // arbitrary RGB input. The component remains explicitly unidentified.
    for (std::size_t i = 0; i < SpectralBands; ++i) {
        const double w = Wavelengths[i];
        const double r = gaussian(w, 610.0, 57.0);
        const double g = gaussian(w, 535.0, 43.0);
        const double b = gaussian(w, 450.0, 38.0);
        const double norm = r + g + b;
        result[i] = norm > 0 ? clamp01((r * rgb[0] + g * rgb[1] + b * rgb[2]) / norm) : 0.0;
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
        p.absorption[i] = ratio * scatter / std::max(0.1, tint);
    }
    return p;
}

Spectrum yellowSpectrum() noexcept
{
    Spectrum s{};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double w = Wavelengths[i];
        s[i] = clamp01(0.035 + 0.90 / (1.0 + std::exp(-(w - 493.0) / 12.0)));
    }
    return s;
}

Spectrum blueSpectrum() noexcept
{
    Spectrum s{};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double w = Wavelengths[i];
        s[i] = clamp01(0.025 + 0.83 / (1.0 + std::exp((w - 490.0) / 15.0)));
    }
    return s;
}

Spectrum redSpectrum() noexcept
{
    Spectrum s{};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double w = Wavelengths[i];
        s[i] = clamp01(0.035 + 0.84 / (1.0 + std::exp(-(w - 585.0) / 18.0)));
    }
    return s;
}

Spectrum whiteSpectrum() noexcept
{
    Spectrum s{};
    s.fill(0.90);
    return s;
}

Spectrum blackSpectrum() noexcept
{
    Spectrum s{};
    s.fill(0.025);
    return s;
}

Spectrum earthSpectrum() noexcept
{
    Spectrum s{};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double w = Wavelengths[i];
        const double red = 0.42 / (1.0 + std::exp(-(w - 565.0) / 22.0));
        s[i] = clamp01(0.035 + red + 0.06 * gaussian(w, 525.0, 45.0));
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
const SurfaceProfile kPaper{"absorbent-paper", 0.82, 0.62, 0.18, 0.0};
const SurfaceProfile kPalette{"non-absorbent-palette", 0.02, 0.15, 0.02, 0.98};

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
const SurfaceProfile& absorbentPaper() noexcept { return kPaper; }
const SurfaceProfile& nonAbsorbentPalette() noexcept { return kPalette; }

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
        || !std::isfinite(consistency) || consistency < 0 || consistency > 1) return false;
    for (const auto& p : pigments)
        if (!finiteNonnegative(p.mass) || p.mass == 0 || (p.pigmentId.empty() && p.residualArgb == 0)) return false;
    return true;
}

void PaintBatch::addWater(double quantity)
{
    if (finiteNonnegative(quantity)) { water += quantity; refreshBatchState(*this); }
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

bool BrushDefinition::valid() const noexcept
{
    const auto unit = [](double v) { return std::isfinite(v) && v >= 0 && v <= 1; };
    const auto curveValid = [&unit](const SensorCurve& curve) {
        return std::all_of(curve.points.begin(), curve.points.end(), unit);
    };
    return !id.empty() && !name.empty() && std::isfinite(size) && size >= 1 && size <= 4096
        && unit(spacingRatio) && spacingRatio > 0 && unit(hardness) && std::isfinite(aspect) && aspect > 0 && aspect <= 8
        && std::isfinite(rotationRadians) && unit(bristleSpread) && finiteNonnegative(reservoirCapacity)
        && reservoirCapacity > 0 && unit(initialLoad) && unit(releaseRate)
        && unit(depletionRate) && unit(pickupRate) && unit(reloadThreshold)
        && unit(pressureTransfer) && unit(speedTransfer) && unit(tiltTransfer)
        && unit(contactTransfer) && unit(waterSpread) && unit(pigmentDiffusion)
        && unit(toothResponse) && unit(edgeAccumulation) && unit(granulationResponse)
        && unit(paintLiftability) && unit(staining) && unit(rewetting)
        && curveValid(pressureCurve) && curveValid(speedCurve) && curveValid(tiltCurve)
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

Spectrum MaterialEngine::reflectance(const PaintBatch& paint, double filmMass) const noexcept
{
    Spectrum k{}, s{};
    if (!std::isfinite(filmMass) || filmMass <= 0) filmMass = 0;
    for (const auto& component : paint.pigments) {
        if (!finiteNonnegative(component.mass) || component.mass <= 0) continue;
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
    Spectrum result{};
    const double amount = paint.pigmentMass() * filmMass;
    if (amount <= 1e-12) { result.fill(1.0); return result; }
    for (std::size_t i = 0; i < SpectralBands; ++i) {
        const double ratio = k[i] / std::max(1e-12, s[i]);
        // Kubelka-Munk infinite-layer reflectance. Film mass modulates optical
        // depth without converting the pigment composition to RGB.
        const double opticalDepth = std::max(0.0, ratio * std::min(1.0, amount));
        result[i] = clamp01(1.0 + opticalDepth - std::sqrt(opticalDepth * opticalDepth + 2.0 * opticalDepth));
    }
    return result;
}

OpticalResult MaterialEngine::opticalResult(const PaintBatch& paint,
                                            const MediumProfile& medium,
                                            const SurfaceProfile& surface,
                                            double filmMass) const noexcept
{
    OpticalResult out;
    const double pigmentMass = paint.pigmentMass();
    const double totalMass = paint.totalMass();
    if (pigmentMass <= 1e-12 || totalMass <= 1e-12) return out;
    const Spectrum spectrum = reflectance(paint, std::max(0.01, filmMass));
    double rgb[3] = {0, 0, 0}, norms[3] = {0, 0, 0};
    for (std::size_t i = 0; i < SpectralBands; ++i) {
        const double w = Wavelengths[i];
        const double weights[3] = {gaussian(w, 610.0, 57.0), gaussian(w, 535.0, 43.0), gaussian(w, 450.0, 38.0)};
        for (int c = 0; c < 3; ++c) { rgb[c] += spectrum[i] * weights[c]; norms[c] += weights[c]; }
    }
    for (int c = 0; c < 3; ++c) rgb[c] = norms[c] > 0 ? rgb[c] / norms[c] : 1.0;
    const double concentration = clamp01(pigmentMass / totalMass);
    const double binderRatio = clamp01(paint.binder / totalMass);
    const double thickness = std::max(0.0, filmMass) * (0.25 + concentration * 2.2)
        * (0.65 + clamp01(medium.viscosity) * 0.55)
        * (0.7 + clamp01(paint.consistency) * 0.45);
    const double mediumFactor = (0.18 + clamp01(medium.hidingPower) * 1.8)
        * (0.72 + binderRatio * clamp01(medium.binderResponse));
    const double substrateRetention = 0.55 + clamp01(surface.absorbency) * 0.8;
    const double alpha = 1.0 - std::exp(-thickness * mediumFactor * substrateRetention);
    out.straightArgb = packStraight(rgb[0], rgb[1], rgb[2], 1.0);
    out.alpha = clamp01(alpha);
    double weightedGranulation = 0;
    for (const auto& component : paint.pigments) {
        if (const auto* p = m_pigments.find(component.pigmentId))
            weightedGranulation += p->granulation * component.mass;
    }
    out.granulation = clamp01(weightedGranulation / pigmentMass
                              * (0.35 + surface.tooth * 0.65)
                              * (0.35 + medium.pigmentSuspension * 0.65));
    return out;
}

std::uint32_t MaterialEngine::composite(std::uint32_t destinationPremultiplied,
                                        const PaintBatch& paint,
                                        const MediumProfile& medium,
                                        const SurfaceProfile& surface,
                                        double coverage, double filmMass,
                                        double toothContact) const noexcept
{
    const auto optical = opticalResult(paint, medium, surface, filmMass);
    const double surfaceRetention = (1.0 - surface.paletteNonAbsorbency)
        * (0.55 + surface.absorbency * 0.45);
    const double contact = clamp01(finiteOrZero(toothContact));
    const double effectiveAlpha = optical.alpha * clamp01(coverage) * contact
        * (0.42 + surfaceRetention * 0.58);
    return blendPremultiplied(destinationPremultiplied, optical.straightArgb, effectiveAlpha);
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
    const double spread = 1.0 + clamp01(m_medium.waterSpread)
        * waterFraction * (0.15 + brush.waterSpread + brush.pigmentDiffusion * 0.5);
    const double rx = brush.size * 0.5 * spread;
    const double ry = rx / brush.aspect;
    const double bound = std::max(rx, ry) + 1.0;
    const auto left = static_cast<std::int64_t>(std::max(0.0, std::floor(centerX - bound)));
    const auto top = static_cast<std::int64_t>(std::max(0.0, std::floor(centerY - bound)));
    const auto right = static_cast<std::int64_t>(std::min(double(geometry.width()), std::ceil(centerX + bound)));
    const auto bottom = static_cast<std::int64_t>(std::min(double(geometry.height()), std::ceil(centerY + bound)));
    if (left >= right || top >= bottom) return Result::Unchanged;

    double contactWeight = 0.0;
    for (auto y = top; y < bottom; ++y) {
        for (auto x = left; x < right; ++x) {
            const double dx = double(x) + 0.5 - centerX;
            const double dy = double(y) + 0.5 - centerY;
            const double u = (ca * dx + sa * dy) / std::max(0.5, rx);
            const double v = (-sa * dx + ca * dy) / std::max(0.5, ry);
            const double radial = u * u + v * v;
            if (radial > 1.0) continue;
            double contact = clamp01(1.0 - radial);
            if (brush.tip == TipGeometry::Flat || brush.tip == TipGeometry::Spatula)
                contact = clamp01((1.0 - std::abs(v)) * 2.0) * clamp01((1.1 - std::abs(u)) * 2.0);
            else if (brush.tip == TipGeometry::Bristle && brush.bristleCount > 0) {
                const double spacing = 2.0 / double(brush.bristleCount);
                const double nearest = std::abs(std::remainder(u + 1.0, spacing));
                const double bristleWidth = std::max(0.02, spacing * (0.32 + brush.bristleSpread * 0.4));
                contact *= nearest < bristleWidth ? clamp01(1.0 - nearest / bristleWidth * 0.2) : 0.08;
            } else if (brush.tip == TipGeometry::Mop) {
                contact = std::sqrt(std::max(0.0, contact));
            }
            contactWeight += contact;
        }
    }
    if (contactWeight <= 0) return Result::Unchanged;
    const double normalizedArea = contactWeight / 64.0;
    const double requested = reservoir.transferredAmount(brush, input, normalizedArea);
    if (requested <= 0) return Result::Unchanged;
    PaintBatch transferred = reservoir.withdraw(requested);
    if (transferred.totalMass() <= 0) return Result::Unchanged;

    store.beginBatch();
    bool changed = false;
    const double particleGranulation = m_engine.opticalResult(transferred, m_medium, m_surface).granulation;
    for (auto y = top; y < bottom; ++y) {
        for (auto x = left; x < right; ++x) {
            const double dx = double(x) + 0.5 - centerX;
            const double dy = double(y) + 0.5 - centerY;
            const double u = (ca * dx + sa * dy) / std::max(0.5, rx);
            const double v = (-sa * dx + ca * dy) / std::max(0.5, ry);
            const double radial = u * u + v * v;
            if (radial > 1.0) continue;
            double contact = clamp01(1.0 - radial);
            if (brush.tip == TipGeometry::Flat || brush.tip == TipGeometry::Spatula)
                contact = clamp01((1.0 - std::abs(v)) * 2.0) * clamp01((1.1 - std::abs(u)) * 2.0);
            else if (brush.tip == TipGeometry::Bristle && brush.bristleCount > 0) {
                const double spacing = 2.0 / double(brush.bristleCount);
                const double nearest = std::abs(std::remainder(u + 1.0, spacing));
                const double bristleWidth = std::max(0.02, spacing * (0.32 + brush.bristleSpread * 0.4));
                contact *= nearest < bristleWidth ? clamp01(1.0 - nearest / bristleWidth * 0.2) : 0.08;
            } else if (brush.tip == TipGeometry::Mop) {
                contact = std::sqrt(std::max(0.0, contact));
            }
            if (contact <= 0) continue;
            // Tooth acts through a deterministic surface profile. Granulation
            // magnitude is calculated from pigment particle data, not noise.
            const double spatialTooth = 0.84 + 0.16 * std::sin((double(x) + 0.5) * 1.73 + (double(y) + 0.5) * 2.11);
            const double grainStructure = 0.92 + 0.08 * std::sin((double(x) + 0.5) * 0.071
                                                   + (double(y) + 0.5) * 0.053);
            const double grain = brush.granulationResponse * particleGranulation * m_surface.tooth;
            const double tooth = (1.0 - brush.toothResponse * m_surface.tooth * (1.0 - spatialTooth))
                * (1.0 - grain * (1.0 - grainStructure) * 0.35);
            const double edge = 1.0 + brush.edgeAccumulation * std::exp(-std::pow((1.0 - radial) * brush.size, 2.0) / 3.0);
            const double diffusion = 1.0 + brush.pigmentDiffusion * m_medium.waterSpread * waterFraction;
            // Brush material units are normalized batch quantities. Convert
            // the local amount into a sub-pixel dry-film optical depth; this
            // keeps sparse high-resolution pixels responsive without filling
            // the whole canvas with a simulated fluid grid.
            constexpr double FilmDepthPerBatchUnit = 64.0;
            const double filmMass = FilmDepthPerBatchUnit * transferred.totalMass()
                * contact * edge * diffusion / contactWeight;
            const auto old = store.pixel({x, y}).value_or(0U);
            const auto next = m_engine.composite(old, transferred, m_medium, m_surface,
                                                 contact * edge, filmMass * (1.0 + particleGranulation * 0.08), tooth);
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
    const double spread = 1.0 + clamp01(m_medium.waterSpread)
        * waterFraction * (0.15 + brush.waterSpread + brush.pigmentDiffusion * 0.5);
    const double rx = brush.size * 0.5 * spread;
    const double ry = rx / brush.aspect;
    const double bound = std::max(rx, ry) + 1.0;
    const auto left = static_cast<std::int64_t>(std::max(0.0, std::floor(centerX - bound)));
    const auto top = static_cast<std::int64_t>(std::max(0.0, std::floor(centerY - bound)));
    const auto right = static_cast<std::int64_t>(std::min(double(geometry.width()), std::ceil(centerX + bound)));
    const auto bottom = static_cast<std::int64_t>(std::min(double(geometry.height()), std::ceil(centerY + bound)));
    if (left >= right || top >= bottom) return Result::Unchanged;

    const auto contactAt = [&](std::int64_t x, std::int64_t y) {
        const double dx = double(x) + 0.5 - centerX;
        const double dy = double(y) + 0.5 - centerY;
        const double u = (ca * dx + sa * dy) / std::max(0.5, rx);
        const double v = (-sa * dx + ca * dy) / std::max(0.5, ry);
        const double radial = u * u + v * v;
        if (radial > 1.0) return std::pair<double, double>{0.0, radial};
        double contact = clamp01(1.0 - radial);
        if (brush.tip == TipGeometry::Flat || brush.tip == TipGeometry::Spatula)
            contact = clamp01((1.0 - std::abs(v)) * 2.0) * clamp01((1.1 - std::abs(u)) * 2.0);
        else if (brush.tip == TipGeometry::Bristle && brush.bristleCount > 0) {
            const double bristleSpacing = 2.0 / double(brush.bristleCount);
            const double nearest = std::abs(std::remainder(u + 1.0, bristleSpacing));
            const double bristleWidth = std::max(0.02, bristleSpacing * (0.32 + brush.bristleSpread * 0.4));
            contact *= nearest < bristleWidth ? clamp01(1.0 - nearest / bristleWidth * 0.2) : 0.08;
        } else if (brush.tip == TipGeometry::Mop) {
            contact = std::sqrt(std::max(0.0, contact));
        }
        if (brush.hardness >= 1.0) contact = contact > 0.0 ? 1.0 : 0.0;
        else contact = clamp01((contact - brush.hardness) / (1.0 - brush.hardness));
        return std::pair<double, double>{contact, radial};
    };

    double contactWeight = 0.0;
    for (auto y = top; y < bottom; ++y)
        for (auto x = left; x < right; ++x) contactWeight += contactAt(x, y).first;
    if (contactWeight <= 0.0) return Result::Unchanged;
    const double requested = reservoir.transferredAmount(brush, input, contactWeight / 64.0);
    if (requested <= 0.0) return Result::Unchanged;
    PaintBatch transferred = reservoir.withdraw(requested);
    const double transferredMass = transferred.totalMass();
    if (transferredMass <= 0.0) return Result::Unchanged;

    const double particleGranulation = m_engine.opticalResult(transferred, m_medium, m_surface).granulation;
    const auto weightAt = [&](std::int64_t x, std::int64_t y, double contact, double radial) {
        if (contact <= 0.0) return 0.0;
        const double spatialTooth = 0.84 + 0.16 * std::sin((double(x) + 0.5) * 1.73 + (double(y) + 0.5) * 2.11);
        const double grainStructure = 0.92 + 0.08 * std::sin((double(x) + 0.5) * 0.071
                                               + (double(y) + 0.5) * 0.053);
        const double grain = brush.granulationResponse * particleGranulation * m_surface.tooth;
        const double tooth = (1.0 - brush.toothResponse * m_surface.tooth * (1.0 - spatialTooth))
            * (1.0 - grain * (1.0 - grainStructure) * 0.35);
        const double edge = 1.0 + brush.edgeAccumulation
            * std::exp(-std::pow((1.0 - radial) * brush.size, 2.0) / 3.0);
        const double diffusion = 1.0 + brush.pigmentDiffusion * m_medium.waterSpread * waterFraction;
        return std::max(0.0, contact * tooth * edge * diffusion);
    };
    double totalWeight = 0.0;
    for (auto y = top; y < bottom; ++y) for (auto x = left; x < right; ++x) {
        const auto [contact, radial] = contactAt(x, y);
        totalWeight += weightAt(x, y, contact, radial);
    }
    if (!(totalWeight > 0.0) || !std::isfinite(totalWeight)) {
        reservoir.pickup(transferred, transferredMass);
        return Result::InvalidPixel;
    }

    store.beginBatch();
    struct BatchEnd { MaterialTileStore& store; ~BatchEnd() { store.endBatch(); } } batchEnd{store};
    bool changed = false;
    Result failure = Result::Unchanged;
    for (auto y = top; y < bottom; ++y) {
        for (auto x = left; x < right; ++x) {
            const auto [contact, radial] = contactAt(x, y);
            const double weight = weightAt(x, y, contact, radial);
            if (weight <= 0.0) continue;
            PaintBatch part = transferred;
            part.scale(weight / totalWeight);
            const auto result = store.deposit({x, y}, part);
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
            const double dx = double(x) + 0.5 - sample.x;
            const double dy = double(y) + 0.5 - sample.y;
            const double u = (ca * dx + sa * dy) / std::max(0.5, rx);
            const double v = (-sa * dx + ca * dy) / std::max(0.5, ry);
            const double radial = u * u + v * v;
            if (radial > 1.0) continue;
            double contact = clamp01(1.0 - radial);
            if (m_brush.tip == TipGeometry::Flat || m_brush.tip == TipGeometry::Spatula)
                contact = clamp01((1.0 - std::abs(v)) * 2.0) * clamp01((1.1 - std::abs(u)) * 2.0);
            else if (m_brush.tip == TipGeometry::Bristle && m_brush.bristleCount > 0) {
                const double spacing = 2.0 / double(m_brush.bristleCount);
                const double nearest = std::abs(std::remainder(u + 1.0, spacing));
                const double bristleWidth = std::max(0.02, spacing * (0.32 + m_brush.bristleSpread * 0.4));
                contact *= nearest < bristleWidth ? clamp01(1.0 - nearest / bristleWidth * 0.2) : 0.08;
            } else if (m_brush.tip == TipGeometry::Mop) {
                contact = std::sqrt(std::max(0.0, contact));
            }
            if (m_brush.hardness >= 1.0) contact = contact > 0.0 ? 1.0 : 0.0;
            else contact = clamp01((contact - m_brush.hardness) / (1.0 - m_brush.hardness));
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
    if (m_started || m_failed || !stamp(sample)) return false;
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
    double distance = firstDistance;
    while (distance <= lastDistance) {
        const double t = distance / length;
        if (!stamp(interpolate(m_previous, sample, t))) return false;
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
