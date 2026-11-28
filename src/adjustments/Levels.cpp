#include "Levels.h"

#include <algorithm>
#include <cmath>

namespace komapaint::adjustments {
namespace {

constexpr double kMinGamma = 0.1;
constexpr double kMaxGamma = 10.0;

int clampInt(int value, int lo, int hi) noexcept
{
    return std::max(lo, std::min(hi, value));
}

std::uint8_t clampByte(int value) noexcept
{
    return static_cast<std::uint8_t>(clampInt(value, 0, 255));
}

std::uint8_t unpremultiply(std::uint32_t channel, std::uint32_t alpha) noexcept
{
    if (alpha == 0U) return 0U;
    if (alpha == 255U) return static_cast<std::uint8_t>(channel);
    const auto value = (channel * 255U + alpha / 2U) / alpha;
    return static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, value));
}

std::uint32_t premultiply(std::uint8_t channel, std::uint32_t alpha) noexcept
{
    if (alpha == 0U) return 0U;
    if (alpha == 255U) return channel;
    return (static_cast<std::uint32_t>(channel) * alpha + 127U) / 255U;
}

std::uint8_t channelOf(std::uint32_t argb, int shift) noexcept
{
    return static_cast<std::uint8_t>((argb >> shift) & 255U);
}

std::uint8_t lumaOf(std::uint32_t argb) noexcept
{
    const auto alpha = argb >> 24;
    return luminanceOfRgb(unpremultiply(channelOf(argb, 16), alpha),
                          unpremultiply(channelOf(argb, 8), alpha),
                          unpremultiply(channelOf(argb, 0), alpha));
}

}  // namespace

bool LevelsParameters::isIdentity(double epsilon) const noexcept
{
    return inputBlack == 0 && inputWhite == 255 && outputBlack == 0 && outputWhite == 255
        && std::abs(gamma - 1.0) <= epsilon;
}

LevelsLut LevelsLut::fromParameters(const LevelsParameters& parameters) noexcept
{
    LevelsLut lut;
    const int inputBlack = clampInt(parameters.inputBlack, 0, 254);
    const int inputWhite = clampInt(parameters.inputWhite, 1, 255);
    if (inputWhite <= inputBlack) {
        // The controller prevents this crossing. If a caller still supplies it,
        // use the narrowest non-empty range instead of dividing by zero.
        lut.table.fill(0);
        lut.identity = false;
        return lut;
    }
    const double inBlack = static_cast<double>(inputBlack) / 255.0;
    const double inWhite = static_cast<double>(inputWhite) / 255.0;
    const double outBlack = static_cast<double>(clampInt(parameters.outputBlack, 0, 255)) / 255.0;
    const double outWhite = static_cast<double>(clampInt(parameters.outputWhite, 0, 255)) / 255.0;
    const double gamma = std::clamp(std::isfinite(parameters.gamma) ? parameters.gamma : 1.0,
                                    kMinGamma, kMaxGamma);
    const double exponent = 1.0 / gamma;

    bool identity = true;
    for (int i = 0; i < 256; ++i) {
        const double input = static_cast<double>(i) / 255.0;
        double t = (input - inBlack) / (inWhite - inBlack);
        t = std::clamp(t, 0.0, 1.0);
        t = std::pow(t, exponent);
        const double output = outBlack + t * (outWhite - outBlack);
        const auto value = clampByte(static_cast<int>(std::lround(output * 255.0)));
        lut.table[static_cast<std::size_t>(i)] = value;
        if (value != i) identity = false;
    }
    // Gamma values extremely close to 1 can still round to an identity table;
    // report the table that will actually run, not the parameters.
    lut.identity = identity;
    return lut;
}

double gammaForMidpoint(double midpoint) noexcept
{
    const double m = std::clamp(std::isfinite(midpoint) ? midpoint : 0.5, 0.01, 0.99);
    const double gamma = std::log(m) / std::log(0.5);
    if (!std::isfinite(gamma)) return 1.0;
    return std::clamp(gamma, kMinGamma, kMaxGamma);
}

double midpointForGamma(double gamma) noexcept
{
    const double g = std::clamp(std::isfinite(gamma) ? gamma : 1.0, kMinGamma, kMaxGamma);
    return std::clamp(std::pow(0.5, g), 0.01, 0.99);
}

std::uint32_t applyPremultipliedArgb(std::uint32_t argb, const LevelsLut& lut) noexcept
{
    const auto alpha = static_cast<std::uint32_t>(argb >> 24);
    if (alpha == 0U) return 0U;
    const auto red = unpremultiply((argb >> 16) & 255U, alpha);
    const auto green = unpremultiply((argb >> 8) & 255U, alpha);
    const auto blue = unpremultiply(argb & 255U, alpha);
    const auto mappedRed = lut.map(red);
    const auto mappedGreen = lut.map(green);
    const auto mappedBlue = lut.map(blue);
    return (alpha << 24)
        | (premultiply(mappedRed, alpha) << 16)
        | (premultiply(mappedGreen, alpha) << 8)
        | premultiply(mappedBlue, alpha);
}

void applyStraightRgba8(std::uint8_t* rgba, std::size_t pixelCount, const LevelsLut& lut) noexcept
{
    if (!rgba || lut.identity) return;
    for (std::size_t i = 0; i < pixelCount; ++i) {
        auto* pixel = rgba + i * 4u;
        pixel[0] = lut.map(pixel[0]);
        pixel[1] = lut.map(pixel[1]);
        pixel[2] = lut.map(pixel[2]);
        // pixel[3] is straight alpha and is intentionally untouched.
    }
}

std::uint8_t luminanceOfRgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept
{
    // Rec. 709 luma with integer weights that sum to 256.
    const auto value = 54U * red + 183U * green + 19U * blue + 128U;
    return static_cast<std::uint8_t>(value >> 8);
}

Histogram histogramPremultipliedArgb(std::span<const std::uint32_t> pixels,
                                     std::span<const std::uint8_t> coverage) noexcept
{
    Histogram histogram{};
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const auto alpha = static_cast<std::uint32_t>(pixels[i] >> 24);
        if (alpha == 0U) continue;
        std::uint32_t weight = alpha;
        if (!coverage.empty()) {
            const auto mask = i < coverage.size() ? coverage[i] : 0U;
            if (mask == 0U) continue;
            weight = (alpha * mask + 127U) / 255U;
            if (weight == 0U) continue;
        }
        histogram[lumaOf(pixels[i])] += weight;
    }
    return histogram;
}

Histogram histogramStraightRgba8(std::span<const std::uint8_t> rgba) noexcept
{
    Histogram histogram{};
    const auto pixelCount = rgba.size() / 4u;
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const auto* pixel = rgba.data() + i * 4u;
        const std::uint32_t alpha = pixel[3];
        if (alpha == 0U) continue;
        histogram[luminanceOfRgb(pixel[0], pixel[1], pixel[2])] += alpha;
    }
    return histogram;
}

}  // namespace komapaint::adjustments
