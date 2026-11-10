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
    std::cout << "Brush engine tests passed\n";
}
