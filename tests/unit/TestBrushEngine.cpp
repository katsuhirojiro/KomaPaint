#include "drawing/BrushEngine.h"
#include <cstdlib>
#include <iostream>
using namespace komapaint;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL line " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::exit(1); } } while (false)
static void countCallback(void* ctx, TilePoint) noexcept { ++*static_cast<int*>(ctx); }
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
    std::cout << "Brush engine tests passed\n";
}
