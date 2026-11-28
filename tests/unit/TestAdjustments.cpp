// Qt-free Levels maths: LUT construction, gamma/midpoint agreement, histogram
// weighting and both pixel formats. No document, no QImage, no Qt.
#include "adjustments/Levels.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace komapaint::adjustments;

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* expression, int line)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

std::uint32_t premul(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    auto p = [a](std::uint8_t c) {
        return (static_cast<std::uint32_t>(c) * a + 127U) / 255U;
    };
    return (static_cast<std::uint32_t>(a) << 24) | (p(r) << 16) | (p(g) << 8) | p(b);
}

std::uint64_t histogramTotal(const Histogram& histogram)
{
    std::uint64_t total = 0;
    for (auto value : histogram) total += value;
    return total;
}

void testIdentity()
{
    LevelsLut lut = LevelsLut::fromParameters({});
    CHECK(lut.identity);
    for (int i = 0; i < 256; ++i) CHECK(lut.map(static_cast<std::uint8_t>(i)) == i);
    CHECK(LevelsParameters{}.isIdentity());
    CHECK(!(LevelsParameters{.inputWhite = 254}).isIdentity());
}

void testInputRange()
{
    LevelsLut lut = LevelsLut::fromParameters({.inputBlack = 50, .inputWhite = 200});
    CHECK(!lut.identity);
    for (int i = 0; i <= 50; ++i) CHECK(lut.map(static_cast<std::uint8_t>(i)) == 0);
    for (int i = 200; i < 256; ++i) CHECK(lut.map(static_cast<std::uint8_t>(i)) == 255);
    CHECK(lut.map(125) > 115 && lut.map(125) < 135);
    // A reversed input range must not divide by zero or wrap.
    LevelsLut invalid = LevelsLut::fromParameters({.inputBlack = 220, .inputWhite = 40});
    CHECK(!invalid.identity);
    for (int i = 0; i < 256; ++i) CHECK(invalid.map(static_cast<std::uint8_t>(i)) == 0);
}

void testGammaAndMidpoint()
{
    CHECK(std::abs(midpointForGamma(1.0) - 0.5) < 0.001);
    CHECK(std::abs(gammaForMidpoint(0.5) - 1.0) < 0.001);
    for (double midpoint : {0.1, 0.25, 0.5, 0.75, 0.9}) {
        const double gamma = gammaForMidpoint(midpoint);
        CHECK(std::abs(midpointForGamma(gamma) - midpoint) < 0.002);
    }
    LevelsLut lighter = LevelsLut::fromParameters({.gamma = 2.0});
    LevelsLut darker = LevelsLut::fromParameters({.gamma = 0.5});
    CHECK(darker.map(128) < 110);
    CHECK(lighter.map(128) > 155);
    CHECK(darker.map(0) == 0 && darker.map(255) == 255);
}

void testOutputRange()
{
    LevelsLut inverted = LevelsLut::fromParameters({.outputBlack = 255, .outputWhite = 0});
    CHECK(inverted.map(0) == 255);
    CHECK(inverted.map(255) == 0);
    LevelsLut limited = LevelsLut::fromParameters({.outputBlack = 32, .outputWhite = 224});
    CHECK(limited.map(0) == 32);
    CHECK(limited.map(255) == 224);
}

void testPremultipliedApplication()
{
    const LevelsLut white = LevelsLut::fromParameters({
        .inputBlack = 0, .inputWhite = 128, .gamma = 1.0, .outputBlack = 0, .outputWhite = 255});
    const auto clear = applyPremultipliedArgb(0U, white);
    CHECK(clear == 0U);

    const auto source = premul(64, 0, 0, 128);
    const auto adjusted = applyPremultipliedArgb(source, white);
    CHECK((adjusted >> 24) == 128U);                 // alpha preserved exactly
    CHECK(((adjusted >> 16) & 255U) == 64U);         // 64 mapped to 128, then premultiplied by alpha 128
    CHECK(((adjusted >> 8) & 255U) == 0U);
    CHECK((adjusted & 255U) == 0U);

    const auto opaque = applyPremultipliedArgb(0xff000000U, white);
    CHECK(opaque == 0xff000000U);
    const auto opaqueWhite = applyPremultipliedArgb(0xffffffffU, white);
    CHECK(opaqueWhite == 0xffffffffU);

    LevelsLut identity = LevelsLut::fromParameters({});
    for (std::uint32_t value : {0U, 0x80804020U, 0xffffffffU, 0x01000000U})
        CHECK(applyPremultipliedArgb(value, identity) == value);
}

void testStraightApplication()
{
    const LevelsLut lut = LevelsLut::fromParameters({
        .inputBlack = 0, .inputWhite = 128, .gamma = 1.0, .outputBlack = 0, .outputWhite = 255});
    std::vector<std::uint8_t> pixels = {10, 20, 30, 40, 200, 255, 0, 255};
    applyStraightRgba8(pixels.data(), 2, lut);
    CHECK(pixels[0] == 20);
    CHECK(pixels[1] == 40);
    CHECK(pixels[2] == 60);
    CHECK(pixels[3] == 40);       // straight alpha untouched
    CHECK(pixels[4] == 255);
    CHECK(pixels[5] == 255);
    CHECK(pixels[6] == 0);
    CHECK(pixels[7] == 255);

    LevelsLut identity = LevelsLut::fromParameters({});
    std::vector<std::uint8_t> untouched = {1, 2, 3, 4};
    applyStraightRgba8(untouched.data(), 1, identity);
    CHECK(untouched == std::vector<std::uint8_t>({1, 2, 3, 4}));
}

void testHistograms()
{
    // Opaque black, opaque white, transparent; and a coverage mask that keeps
    // only the black pixel.
    const std::vector<std::uint32_t> pixels = {
        0xff000000U, 0xffffffffU, 0x00000000U, 0x80ffffffU};
    const auto full = histogramPremultipliedArgb(pixels);
    CHECK(histogramTotal(full) == 255U + 255U + 128U);
    CHECK(full[0] == 255U);
    CHECK(full[255] == 255U + 128U);

    const std::vector<std::uint8_t> coverage = {255, 0, 255, 0};
    const auto masked = histogramPremultipliedArgb(pixels, coverage);
    CHECK(histogramTotal(masked) == 255U);
    CHECK(masked[0] == 255U);
    CHECK(masked[255] == 0U);

    // Straight RGBA: one half-transparent mid-grey and one transparent red.
    const std::vector<std::uint8_t> rgba = {128, 128, 128, 128, 255, 0, 0, 0};
    const auto straight = histogramStraightRgba8(rgba);
    CHECK(histogramTotal(straight) == 128U);
    CHECK(straight[128] == 128U);

    LevelsLut identity = LevelsLut::fromParameters({});
    CHECK(identity.identity);
}

}  // namespace

int main()
{
    testIdentity();
    testInputRange();
    testGammaAndMidpoint();
    testOutputRange();
    testPremultipliedApplication();
    testStraightApplication();
    testHistograms();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
