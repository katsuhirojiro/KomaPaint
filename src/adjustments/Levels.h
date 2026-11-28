#pragma once

// Colour-adjustment core: Levels parameters, a 256-entry lookup table, LUT
// application and histogram accumulation.
//
// Deliberately Qt-free and independent of tiles, documents and QImage. The
// document controller and the in-memory image path both call this same code,
// so a brush tip and a canvas layer cannot drift apart.
//
// Geometric transforms do not belong here. See docs/architecture/adjustments.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace komapaint::adjustments {

struct LevelsParameters {
    int inputBlack = 0;       // 0..254
    int inputWhite = 255;     // 1..255
    double gamma = 1.0;       // 0.1..10.0
    int outputBlack = 0;      // 0..255
    int outputWhite = 255;    // 0..255

    bool isIdentity(double epsilon = 1e-9) const noexcept;
};

struct LevelsLut {
    std::array<std::uint8_t, 256> table{};
    bool identity = true;

    static LevelsLut fromParameters(const LevelsParameters& parameters) noexcept;
    std::uint8_t map(std::uint8_t value) const noexcept { return table[value]; }
};

// The simple "midpoint" control is a view of gamma, not a second curve.
// midpoint is the input position (0..1) that maps to output 0.5.
double gammaForMidpoint(double midpoint) noexcept;
double midpointForGamma(double gamma) noexcept;

// Premultiplied ARGB, the storage format of document tiles. Alpha is preserved
// exactly; RGB is unpremultiplied, mapped and premultiplied again.
std::uint32_t applyPremultipliedArgb(std::uint32_t argb, const LevelsLut& lut) noexcept;

// Straight (non-premultiplied) RGBA8888, the format used by QImage tiles and
// brush-tip extraction. Only the colour bytes are mapped.
void applyStraightRgba8(std::uint8_t* rgba, std::size_t pixelCount, const LevelsLut& lut) noexcept;

using Histogram = std::array<std::uint64_t, 256>;

// Luma histogram weighted by alpha and, when supplied, by per-pixel selection
// coverage (0..255). Premultiplied input is unpremultiplied for the luma value.
Histogram histogramPremultipliedArgb(std::span<const std::uint32_t> pixels,
                                     std::span<const std::uint8_t> coverage = {}) noexcept;
Histogram histogramStraightRgba8(std::span<const std::uint8_t> rgba) noexcept;

std::uint8_t luminanceOfRgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept;

}  // namespace komapaint::adjustments
