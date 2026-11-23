#include "drawing/BrushEngine.h"
#include "brush/BrushTypes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using namespace komapaint;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL line " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::exit(1); } } while (false)
static void countCallback(void* ctx, TilePoint) noexcept { ++*static_cast<int*>(ctx); }

static int alphaAt(const SparseTileStore& store, int x, int y)
{
    return static_cast<int>(store.pixel({x, y}).value_or(0U) >> 24);
}

// Longest run of unpainted centreline pixels in [x0,x1]. A continuous stroke
// has a run of zero; a chain of dabs does not.
static int longestGap(const SparseTileStore& store, int y, int x0, int x1)
{
    int run = 0, worst = 0;
    for (int x = x0; x <= x1; ++x) {
        if (alphaAt(store, x, y) == 0) worst = std::max(worst, ++run);
        else run = 0;
    }
    return worst;
}

static int inkSpan(const SparseTileStore& store, int x, int y0, int y1)
{
    int count = 0;
    for (int y = y0; y <= y1; ++y) count += alphaAt(store, x, y) > 0 ? 1 : 0;
    return count;
}

static long long inkSum(const SparseTileStore& store, int x0, int x1, int y0, int y1)
{
    long long sum = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) sum += alphaAt(store, x, y);
    return sum;
}

// Optional Windows-CI proof sheet: the production stroke engine, rendered to
// a plain 24-bit BMP so it can be inspected without Qt or an image library.
static void writeProofBmp(const std::string& path, const SparseTileStore& store,
                          int width, int height)
{
    const int rowBytes = (width * 3 + 3) & ~3;
    std::vector<unsigned char> row(static_cast<std::size_t>(rowBytes), 0);
    std::ofstream out(path, std::ios::binary);
    if (!out) return;
    const auto u16 = [&](unsigned v) { out.put(char(v & 255)); out.put(char((v >> 8) & 255)); };
    const auto u32 = [&](unsigned v) { u16(v & 0xffff); u16((v >> 16) & 0xffff); };
    out.put('B'); out.put('M');
    u32(static_cast<unsigned>(54 + rowBytes * height));
    u16(0); u16(0); u32(54);
    u32(40); u32(static_cast<unsigned>(width)); u32(static_cast<unsigned>(height));
    u16(1); u16(24); u32(0);
    u32(static_cast<unsigned>(rowBytes * height)); u32(2835); u32(2835);
    u32(0); u32(0);
    for (int y = height - 1; y >= 0; --y) {
        std::fill(row.begin(), row.end(), 0);
        for (int x = 0; x < width; ++x) {
            const std::uint32_t px = store.pixel({x, y}).value_or(0U);
            const unsigned a = (px >> 24) & 255U;
            const unsigned r = (px >> 16) & 255U;
            const unsigned g = (px >> 8) & 255U;
            const unsigned b = px & 255U;
            // Premultiplied ARGB over white paper.
            row[static_cast<std::size_t>(x) * 3 + 0] = static_cast<unsigned char>((b + 255U * (255U - a)) / 255U);
            row[static_cast<std::size_t>(x) * 3 + 1] = static_cast<unsigned char>((g + 255U * (255U - a)) / 255U);
            row[static_cast<std::size_t>(x) * 3 + 2] = static_cast<unsigned char>((r + 255U * (255U - a)) / 255U);
        }
        out.write(reinterpret_cast<const char*>(row.data()), rowBytes);
    }
}

static BrushDefinition continuityBrush(int size, double spacing, double pressureSize)
{
    BrushDefinition b;
    b.size = size;
    b.spacing = spacing;
    b.hardness = 1.0;
    b.opacity = 1.0;
    b.flow = 1.0;
    b.pressureSize = pressureSize;
    b.pressureOpacity = 0.0;
    b.pressureFlow = 0.0;
    b.minSizeRatio = 0.15;
    b.color = 0xff000000;
    return b;
}

static void writeContinuityProof()
{
    const char* directory = std::getenv("KOMAPAINT_BRUSH_PROOF");
    if (!directory || !*directory) return;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return;
    constexpr int width = 700, height = 380;
    SparseTileStore sheet(width, height, 32);
    auto paint = [&](int y, int size, double spacing, bool gradual) {
        BrushStroke line(sheet, continuityBrush(size, spacing, 1.0));
        CHECK(line.begin({40, double(y), 0.2}));
        for (int x = 41; x <= 660; ++x) {
            double pressure = 0.2;
            if (gradual) {
                const double t = double(x - 40) / 620.0;
                pressure = 0.2 + 0.8 * (1.0 - std::abs(2.0 * t - 1.0));
            }
            CHECK(line.extend({double(x), double(y), pressure}));
        }
        CHECK(line.end({660, double(y), 0.2}));
    };
    paint(60, 32, 0.50, false);
    paint(150, 32, 0.50, true);
    paint(250, 128, 0.50, false);
    paint(340, 8, 0.50, false);
    writeProofBmp((std::filesystem::path(directory) / "brush-continuity.bmp").string(),
                  sheet, width, height);
}

int main() {
    using W = SparseTileStore::WriteResult;
    BrushDefinition brush; brush.size = 8; brush.spacing = 0.25; brush.color = 0xffff0000;
    SparseTileStore store(128, 40, 16);
    int callbacks = 0; store.setChangeCallback(countCallback, &callbacks);
    BrushStroke stroke(store, brush);
    CHECK(stroke.begin({2, 20, 1})); CHECK(stroke.extend({110, 20, 1})); CHECK(stroke.end({110, 20, 1}));
    for (int x = 2; x < 110; ++x) CHECK((store.pixel({x, 20}).value() >> 24) > 0);
    CHECK(store.allocatedTiles() >= 7); CHECK(callbacks < 120); // coalesced per dab, not per pixel
    CHECK(store.pixel({50, 20}).value() == 0xffff0000U);
    CHECK(store.pixel({50, 5}).value() == 0U);
    CHECK(store.dirtyRegions().size() >= 7);
    SparseTileStore slow(128, 40, 16);
    BrushStroke fragmented(slow, brush);
    CHECK(fragmented.begin({2, 20, 1}));
    for (int x = 3; x <= 110; ++x) CHECK(fragmented.extend({static_cast<double>(x), 20, 1}));
    CHECK(fragmented.end({110, 20, 1}));
    for (int x = 2; x < 110; ++x) CHECK((slow.pixel({x, 20}).value() >> 24) > 0);
    SparseTileStore clipped(32, 32, 8);
    BrushStroke cross(clipped, brush);
    CHECK(cross.begin({-1000000, 16, 1})); CHECK(cross.end({1000000, 16, 1}));
    for (int x = 0; x < 32; ++x) CHECK((clipped.pixel({x, 16}).value() >> 24) > 0);
    SparseTileStore alpha(16, 16, 8);
    brush.opacity = 0.5; brush.flow = 0.5;
    CHECK(BrushRasterizer::dab(alpha, brush, {4, 4, 0.5}) == W::Changed);
    auto a = alpha.pixel({4, 4}).value() >> 24;
    CHECK(a >= 30 && a <= 33);
    SparseTileStore zero(16, 16, 8);
    CHECK(BrushRasterizer::dab(zero, brush, {4, 4, 0}) == W::Unchanged);
    CHECK(zero.allocatedTiles() == 0);
    SparseTileStore grain(16, 16, 8), round(16, 16, 8);
    brush.opacity = brush.flow = 1; brush.tip = BrushTip::Textured;
    CHECK(BrushRasterizer::dab(grain, brush, {8, 8, 1}) == W::Changed);
    brush.tip = BrushTip::Round;
    CHECK(BrushRasterizer::dab(round, brush, {8, 8, 1}) == W::Changed);
    CHECK(grain.pixel({8, 8}) != round.pixel({8, 8}));
    SparseTileStore limited(32, 32, 8, 8 * 8 * 4);
    BrushStroke failed(limited, brush);
    CHECK(!failed.begin({8, 8, 1})); CHECK(failed.failed());
    CHECK(failed.failureReason() == W::MemoryLimit);
    CHECK(limited.allocatedTiles() <= 1);
    // ---- Hardness: 1 keeps the crisp tip, lower values fade towards the rim.
    {
        BrushDefinition tip; tip.size = 40; tip.color = 0xff000000;
        auto alphaAt = [&](double hardness, int x) {
            SparseTileStore s(64, 64, 16);
            tip.hardness = hardness;
            CHECK(BrushRasterizer::dab(s, tip, {32, 32, 1}) == W::Changed);
            return static_cast<int>(s.pixel({x, 32}).value() >> 24);
        };
        // x = 32 is the centre, 42 half-way out, 50 close to the rim (radius 20).
        CHECK(alphaAt(1.0, 32) == 255); CHECK(alphaAt(1.0, 42) == 255); CHECK(alphaAt(1.0, 50) == 255);
        CHECK(alphaAt(0.0, 32) >= 250);
        const int softHalf = alphaAt(0.0, 42), softRim = alphaAt(0.0, 50);
        CHECK(softHalf > 90 && softHalf < 170);
        CHECK(softRim > 0 && softRim < 30);
        // A half-hard tip is solid over the inner half of its radius.
        const int midCore = alphaAt(0.5, 40), midRim = alphaAt(0.5, 50);
        CHECK(midCore == 255);
        CHECK(midRim > softRim && midRim < 255);
        // Outside the tip nothing is painted, whatever the hardness.
        CHECK(alphaAt(0.0, 54) == 0); CHECK(alphaAt(1.0, 54) == 0);
        SparseTileStore rejected(16, 16, 8);
        tip.size = 8; tip.hardness = 1.5;
        CHECK(BrushRasterizer::dab(rejected, tip, {8, 8, 1}) != W::Changed);
        tip.hardness = -0.1;
        CHECK(BrushRasterizer::dab(rejected, tip, {8, 8, 1}) != W::Changed);
        CHECK(rejected.allocatedTiles() == 0);
    }
    // ---- Erase: removes paint instead of adding it.
    {
        BrushDefinition paint; paint.size = 12; paint.color = 0xff2060c0;
        SparseTileStore s(32, 32, 8);
        CHECK(BrushRasterizer::dab(s, paint, {16, 16, 1}) == W::Changed);
        CHECK(s.pixel({16, 16}).value() == 0xff2060c0U);
        BrushDefinition eraser = paint; eraser.erase = true; eraser.size = 4;
        CHECK(BrushRasterizer::dab(s, eraser, {16, 16, 1}) == W::Changed);
        CHECK(s.pixel({16, 16}).value() == 0U);                 // fully erased
        CHECK(s.pixel({11, 16}).value() == 0xff2060c0U);        // outside the eraser tip: untouched
        // Half opacity leaves about half of the paint, still premultiplied.
        eraser.opacity = 0.5;
        CHECK(BrushRasterizer::dab(s, eraser, {11, 16, 1}) == W::Changed);
        const auto half = s.pixel({11, 16}).value();
        const unsigned ha = half >> 24, hr = (half >> 16) & 255U, hg = (half >> 8) & 255U, hb = half & 255U;
        CHECK(ha >= 126 && ha <= 129);
        CHECK(hr <= ha && hg <= ha && hb <= ha);
        CHECK(hb >= 93 && hb <= 98);                            // 0xc0 * 0.5
        // Erasing where nothing was painted changes nothing and allocates nothing.
        SparseTileStore empty(32, 32, 8);
        eraser.opacity = 1;
        CHECK(BrushRasterizer::dab(empty, eraser, {16, 16, 1}) == W::Unchanged);
        CHECK(empty.allocatedTiles() == 0);
        // The eraser ignores the brush colour.
        SparseTileStore other(32, 32, 8);
        CHECK(BrushRasterizer::dab(other, paint, {16, 16, 1}) == W::Changed);
        eraser.color = 0xffffffff;
        CHECK(BrushRasterizer::dab(other, eraser, {16, 16, 1}) == W::Changed);
        CHECK(other.pixel({16, 16}).value() == 0U);
        // A whole eraser stroke clears a painted line.
        SparseTileStore line(96, 32, 16);
        paint.size = 10;
        BrushStroke draw(line, paint);
        CHECK(draw.begin({8, 16, 1})); CHECK(draw.end({88, 16, 1}));
        eraser.size = 16;
        BrushStroke rub(line, eraser);
        CHECK(rub.begin({30, 16, 1})); CHECK(rub.end({60, 16, 1}));
        CHECK(line.pixel({45, 16}).value() == 0U);
        CHECK(line.pixel({12, 16}).value() == 0xff2060c0U);
        CHECK(line.pixel({80, 16}).value() == 0xff2060c0U);
    }
    // ---- Digital tip geometry, pressure dynamics, and blending/smudging.
    {
        // Elliptical flat brush (aspectRatio > 1) stays narrower along minor axis.
        SparseTileStore flatStore(64, 64, 16);
        BrushDefinition flat;
        flat.size = 24; flat.aspectRatio = 2.5; flat.angleRadians = 0.0; flat.color = 0xff000000;
        CHECK(BrushRasterizer::dab(flatStore, flat, {32, 32, 1.0}) == W::Changed);
        CHECK((flatStore.pixel({22, 32}).value() >> 24) > 0);
        CHECK((flatStore.pixel({32, 22}).value() >> 24) == 0);

        // Bristle, Graphite, and Stipple produce distinct non-uniform coverage profiles.
        auto dabCenterSum = [&](BrushTip tip) {
            SparseTileStore s(48, 48, 16);
            BrushDefinition d;
            d.size = 28; d.color = 0xff102030; d.tip = tip; d.hardness = 0.8;
            CHECK(BrushRasterizer::dab(s, d, {24, 24, 1.0}) == W::Changed);
            std::uint64_t sum = 0;
            for (int y = 12; y <= 36; ++y)
                for (int x = 12; x <= 36; ++x) sum += (s.pixel({x, y}).value() >> 24);
            return sum;
        };
        const auto sumRound = dabCenterSum(BrushTip::Round);
        const auto sumBristle = dabCenterSum(BrushTip::Bristle);
        const auto sumGraphite = dabCenterSum(BrushTip::Graphite);
        const auto sumStipple = dabCenterSum(BrushTip::Stipple);
        CHECK(sumBristle > 0 && sumBristle < sumRound);
        CHECK(sumGraphite > 0 && sumGraphite < sumRound);
        CHECK(sumStipple > 0 && sumStipple < sumRound);

        // Pressure-to-size tapers diameter cleanly on light pressure.
        SparseTileStore lightPress(64, 64, 16), fullPress(64, 64, 16);
        BrushDefinition tapered;
        tapered.size = 30; tapered.pressureSize = 0.85; tapered.minSizeRatio = 0.15; tapered.pressureOpacity = 0.0;
        CHECK(BrushRasterizer::dab(lightPress, tapered, {32, 32, 0.1}) == W::Changed);
        CHECK(BrushRasterizer::dab(fullPress, tapered, {32, 32, 1.0}) == W::Changed);
        CHECK((lightPress.pixel({43, 32}).value() >> 24) == 0);
        CHECK((fullPress.pixel({43, 32}).value() >> 24) > 0);

        // Blending / smudging pulls existing paint across into empty canvas without physical simulation.
        SparseTileStore smudgeStore(96, 48, 16);
        BrushDefinition redPaint;
        redPaint.size = 16; redPaint.color = 0xffff2020;
        BrushStroke drawRed(smudgeStore, redPaint);
        CHECK(drawRed.begin({20, 24, 1.0})); CHECK(drawRed.end({28, 24, 1.0}));
        CHECK(smudgeStore.pixel({56, 24}).value() == 0U);

        BrushDefinition smudge;
        smudge.size = 16; smudge.spacing = 0.15; smudge.opacity = 0.9; smudge.flow = 0.9;
        smudge.hardness = 0.4; smudge.blendStrength = 0.9; smudge.colorMix = 0.0;
        BrushStroke smear(smudgeStore, smudge);
        CHECK(smear.begin({24, 24, 1.0}));
        CHECK(smear.end({58, 24, 1.0}));
        const auto smeared = smudgeStore.pixel({48, 24}).value();
        CHECK((smeared >> 24) > 0);
        CHECK(((smeared >> 16) & 255U) > (smeared & 255U));
    }
    // Brush size is not capped: wide washes and background blocking need tips
    // far larger than the old 256 px ceiling, which used to reject the stroke.
    {
        SparseTileStore wide(600, 600, 64);
        BrushDefinition big;
        big.size = 400; big.hardness = 1.0; big.color = 0xff00ff00; big.spacing = 0.15;
        BrushStroke wideStroke(wide, big);
        CHECK(wideStroke.begin({300, 300, 1.0}));
        CHECK(!wideStroke.failed());
        CHECK(wideStroke.end({300, 300, 1.0}));
        CHECK((wide.pixel({300, 300}).value() >> 24) == 0xffU);
        // The tip is round, so the rim of the square bounding box stays clear.
        CHECK(wide.pixel({300, 60}).value() == 0U);
        CHECK((wide.pixel({300, 140}).value() >> 24) > 0U);

        BrushDefinition huge;
        huge.size = 2000; huge.color = 0xff0000ff;
        BrushStroke hugeStroke(wide, huge);
        CHECK(hugeStroke.begin({300, 300, 1.0}));
        CHECK(!hugeStroke.failed());
        CHECK(hugeStroke.end({300, 300, 1.0}));
        CHECK((wide.pixel({5, 300}).value() >> 24) > 0U); // clipped to the canvas, still painted
    }

    {
        // Check a point beyond the old cap's radius without allocating a square
        // canvas or a diameter-sized buffer.
        SparseTileStore strip(12000, 5, 32);
        BrushDefinition big;
        big.size = 12000;
        big.hardness = 1.0;
        big.color = 0xff2468ac;
        BrushStroke wideStripStroke(strip, big);
        CHECK(wideStripStroke.begin({6100, 2, 1.0}));
        CHECK(wideStripStroke.end({6100, 2, 1.0}));
        CHECK(strip.pixel({500, 2}).value() == big.color);
        CHECK(strip.pixel({0, 2}).value() == 0U);

        SparseTileStore small(9, 7, 8);
        big.size = brush::MaxBrushDiameter;
        big.tiltSize = 1.0;
        big.blendStrength = 0.5;
        BrushStroke largest(small, big);
        CHECK(largest.begin({1e100, 2, 1.0})); // out of canvas, before rounding pickup coordinates
        CHECK(small.allocatedTiles() == 0);
        CHECK(largest.end({4, 2, 1.0, 90, 0, 0, 0, true}));
        CHECK(small.pixel({8, 6}).value() != 0U);
        big.size = double(brush::MaxBrushDiameter) + 1.0;
        BrushStroke unrepresentable(small, big);
        CHECK(unrepresentable.failed());
        // A component transform may produce an effective size beyond the base
        // diameter range. Only unrepresentable math is rejected here.
        CHECK(BrushRasterizer::dab(small, big, {4, 2, 1.0}) != W::InvalidPixel);
        big.size = std::numeric_limits<double>::max();
        CHECK(BrushRasterizer::dab(small, big, {4, 2, 1.0}) == W::InvalidPixel);
    }

    // ---- Spacing follows the dab that pressure and tilt actually produce.
    // The matrix renders real pixels and scans the centreline; it does not
    // check the spacing formula in isolation.
    {
        constexpr int y = 160, x0 = 100, x1 = 600;
        for (double pressureSize : {0.0, 1.0}) {
            for (int size : {8, 32, 128}) {
                for (double spacing : {0.05, 0.15, 0.30, 0.50}) {
                    for (double pressure : {0.2, 0.6, 1.0}) {
                        SparseTileStore canvas(700, 320, 32);
                        BrushStroke line(canvas, continuityBrush(size, spacing, pressureSize));
                        CHECK(line.begin({double(x0), double(y), pressure}));
                        for (int x = x0 + 1; x <= x1; ++x)
                            CHECK(line.extend({double(x), double(y), pressure}));
                        CHECK(line.end({double(x1), double(y), pressure}));
                        CHECK(longestGap(canvas, y, x0, x1) == 0);
                        CHECK(alphaAt(canvas, x0, y) > 0);
                        CHECK(alphaAt(canvas, x1, y) > 0);
                    }
                }
            }
        }
    }

    // ---- Pressure changing continuously along one stroke.
    {
        constexpr int y = 160, x0 = 100, x1 = 600;
        SparseTileStore canvas(700, 320, 32);
        BrushStroke line(canvas, continuityBrush(32, 0.50, 1.0));
        CHECK(line.begin({double(x0), double(y), 0.2}));
        for (int x = x0 + 1; x <= x1; ++x) {
            const double t = double(x - x0) / double(x1 - x0);
            const double pressure = 0.2 + 0.8 * (1.0 - std::abs(2.0 * t - 1.0));
            CHECK(line.extend({double(x), double(y), pressure}));
        }
        CHECK(line.end({double(x1), double(y), 0.2}));
        CHECK(longestGap(canvas, y, x0, x1) == 0);
        // The dab really does follow pressure: the crest is wider than the
        // light-pressure start, and the end tapers back down.
        CHECK(inkSpan(canvas, 350, 100, 220) > inkSpan(canvas, 120, 100, 220));
        CHECK(inkSpan(canvas, 580, 100, 220) < inkSpan(canvas, 350, 100, 220));
    }

    // ---- Tilt broadening changes the dab and therefore the step.
    {
        SparseTileStore canvas(700, 320, 32);
        BrushDefinition b = continuityBrush(32, 0.50, 0.0);
        b.tiltSize = 1.0;
        BrushStroke line(canvas, b);
        CHECK(line.begin({100, 160, 1.0, 0, 0, 0, 0, true}));
        for (int x = 101; x <= 600; ++x) {
            const double tilt = 90.0 * double(x - 100) / 500.0;
            CHECK(line.extend({double(x), 160, 1.0, tilt, 0, 0, 0, true}));
        }
        CHECK(line.end({600, 160, 1.0, 90, 0, 0, 0, true}));
        CHECK(longestGap(canvas, 160, 100, 600) == 0);
        CHECK(inkSpan(canvas, 550, 80, 240) > inkSpan(canvas, 150, 80, 240));

        // A one-sample tilt jump must not open a gap either.
        SparseTileStore jump(700, 320, 32);
        BrushStroke abrupt(jump, b);
        CHECK(abrupt.begin({100, 160, 1.0, 0, 0, 0, 0, true}));
        CHECK(abrupt.extend({101, 160, 1.0, 90, 0, 0, 0, true}));
        CHECK(abrupt.extend({600, 160, 1.0, 90, 0, 0, 0, true}));
        CHECK(abrupt.end({600, 160, 1.0, 90, 0, 0, 0, true}));
        CHECK(longestGap(jump, 160, 100, 600) == 0);
    }

    // ---- Abrupt pressure changes between adjacent input samples.
    {
        constexpr int y = 160, x0 = 100, x1 = 600;
        SparseTileStore canvas(700, 320, 32);
        BrushStroke line(canvas, continuityBrush(32, 0.50, 1.0));
        CHECK(line.begin({double(x0), double(y), 1.0}));
        for (int x = x0 + 1; x <= x1; ++x) {
            const double pressure = (x % 2) ? 0.2 : 1.0;
            CHECK(line.extend({double(x), double(y), pressure}));
        }
        CHECK(line.end({double(x1), double(y), 0.2}));
        CHECK(longestGap(canvas, y, x0, x1) == 0);

        // One-sample drop and recovery, at the maximum allowed spacing.
        SparseTileStore jump(700, 320, 32);
        BrushStroke abrupt(jump, continuityBrush(64, 0.50, 1.0));
        CHECK(abrupt.begin({100, 160, 1.0}));
        CHECK(abrupt.extend({101, 160, 0.2}));
        CHECK(abrupt.extend({500, 160, 0.2}));
        CHECK(abrupt.extend({501, 160, 1.0}));
        CHECK(abrupt.end({600, 160, 1.0}));
        CHECK(longestGap(jump, 160, 100, 600) == 0);
    }

    // ---- Very short strokes, including shorter than one spacing step.
    {
        for (double length : {0.5, 1.0, 2.5, 8.0}) {
            SparseTileStore canvas(400, 320, 32);
            BrushStroke line(canvas, continuityBrush(128, 0.50, 1.0));
            CHECK(line.begin({150, 160, 0.2}));
            CHECK(line.end({150 + length, 160, 0.2}));
            CHECK(alphaAt(canvas, 150, 160) > 0);
            CHECK(alphaAt(canvas, int(std::floor(150 + length)), 160) > 0);
            CHECK(longestGap(canvas, 160, 150, int(std::floor(150 + length))) == 0);
        }
    }

    // ---- Stationary contact is a dab, not a failed stroke or a spaced line.
    {
        SparseTileStore canvas(400, 320, 32);
        BrushStroke line(canvas, continuityBrush(32, 0.15, 1.0));
        CHECK(line.begin({200, 160, 1.0}));
        CHECK(line.extend({200, 160, 0.4}));
        CHECK(line.end({200, 160, 0.2}));
        CHECK(!line.failed());
        CHECK(alphaAt(canvas, 200, 160) > 0);
    }

    // ---- Sample density must not change the rendered stroke. Constant
    // pressure means the same stamp positions regardless of how the pointer
    // was sampled; continuity must hold at every density.
    {
        auto paint = [](double sampleStep) {
            auto canvasPtr = std::make_unique<SparseTileStore>(700, 320, 32);
            BrushStroke line(*canvasPtr, continuityBrush(32, 0.30, 1.0));
            CHECK(line.begin({100, 160, 0.35}));
            for (double x = 100 + sampleStep; x < 600; x += sampleStep)
                CHECK(line.extend({x, 160, 0.35}));
            CHECK(line.end({600, 160, 0.35}));
            CHECK(longestGap(*canvasPtr, 160, 100, 600) == 0);
            const long long sum = inkSum(*canvasPtr, 100, 600, 130, 190);
            return std::make_pair(std::move(canvasPtr), sum);
        };
        auto sparse = paint(50.0);
        auto medium = paint(5.0);
        auto dense = paint(0.5);
        const long long reference = dense.second;
        CHECK(std::llabs(sparse.second - reference) <= reference / 100);
        CHECK(std::llabs(medium.second - reference) <= reference / 100);
    }

    // ---- Varying pressure at sparse and dense input rates stays continuous.
    {
        auto paint = [](double sampleStep) {
            auto canvasPtr = std::make_unique<SparseTileStore>(700, 320, 32);
            BrushStroke line(*canvasPtr, continuityBrush(32, 0.50, 1.0));
            CHECK(line.begin({100, 160, 0.2}));
            for (double x = 100 + sampleStep; x < 600; x += sampleStep) {
                const double t = (x - 100.0) / 500.0;
                const double pressure = 0.2 + 0.8 * (1.0 - std::abs(2.0 * t - 1.0));
                CHECK(line.extend({x, 160, pressure}));
            }
            CHECK(line.end({600, 160, 0.2}));
            CHECK(longestGap(*canvasPtr, 160, 100, 600) == 0);
        };
        paint(0.5);
        paint(5.0);
        paint(50.0);
    }

    writeContinuityProof();

    std::cout << "Brush engine tests passed\n";
}
