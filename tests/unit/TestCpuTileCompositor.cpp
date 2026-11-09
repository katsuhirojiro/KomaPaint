#include "canvas/CpuTileCompositor.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace komapaint;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL line " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::exit(1); } } while (false)
int main() {
    using W = SparseTileStore::WriteResult;
    SparseTileStore bottom(5, 5, 4), top(5, 5, 4);
    TileGeometry geometry(5, 5, 4);
    CHECK(bottom.setPixel({0, 0}, 0xff0000ffU) == W::Changed);
    CHECK(bottom.setPixel({1, 0}, 0xffffffffU) == W::Changed);
    CHECK(top.setPixel({0, 0}, 0x80800000U) == W::Changed);
    CHECK(top.setPixel({4, 4}, 0xff00ff00U) == W::Changed);
    std::vector<CompositeLayer> layers{{&top, true, 1}, {&bottom, true, 1}};
    auto composite = CpuTileCompositor::compose(geometry, {0, 0}, layers, 0);
    CHECK(composite && composite->width == 4 && composite->height == 4);
    CHECK(composite->pixels[0] == 0xff80007fU);
    CHECK(composite->pixels[1] == 0xffffffffU);
    CHECK(composite->pixels[2] == 0U);
    CHECK(top.pixel({0,0}) == 0x80800000U && bottom.pixel({0,0}) == 0xff0000ffU);
    CHECK(top.allocatedTiles() == 2 && bottom.allocatedTiles() == 1);
    auto edge = CpuTileCompositor::compose(geometry, {1, 1}, layers, 0);
    CHECK(edge && edge->width == 1 && edge->height == 1 && edge->pixels[0] == 0xff00ff00U);
    CHECK(bottom.allocatedTiles() == 1);
    auto blank = CpuTileCompositor::compose(geometry, {1, 0}, layers, 0xff112233U);
    CHECK(blank && blank->pixels.size() == 4 && blank->pixels[0] == 0xff112233U);
    CHECK(bottom.allocatedTiles() == 1 && top.allocatedTiles() == 2);
    layers[0].visible = false;
    CHECK(CpuTileCompositor::compose(geometry, {0, 0}, layers, 0)->pixels[0] == 0xff0000ffU);
    layers[0].visible = true; layers[0].opacity = 0.5;
    CHECK(CpuTileCompositor::compose(geometry, {0, 0}, layers, 0)->pixels[0] == 0xff4000bfU);
    layers[0].opacity = 1;
    std::swap(layers[0], layers[1]);
    CHECK(CpuTileCompositor::compose(geometry, {0, 0}, layers, 0)->pixels[0] == 0xff0000ffU);
    std::vector<CompositeLayer> one{{&top, true, 1}};
    CHECK(CpuTileCompositor::compose(geometry, {0, 0}, one, 0)->pixels[0] == 0x80800000U);
    CHECK(CpuTileCompositor::compose(geometry, {0, 0}, {}, 0)->pixels[0] == 0U);
    CHECK(!CpuTileCompositor::compose(geometry, {2, 0}, layers, 0));
    CHECK(!CpuTileCompositor::compose(geometry, {0, 0}, layers, 0x00ffffffU));
    layers[0].opacity = std::numeric_limits<double>::quiet_NaN();
    CHECK(!CpuTileCompositor::compose(geometry, {0, 0}, layers, 0));
    SparseTileStore different(6, 5, 4);
    CHECK(!CpuTileCompositor::compose(geometry, {0, 0}, {{&different, true, 1}}, 0));

    layers = {{&top, true, 1}, {&bottom, true, 1}};
    CompositeInvalidation queue;
    auto plan = queue.prepare(layers, 0);
    CHECK(plan.invalidateAll && plan.tiles.size() == 2);
    CHECK(!queue.acknowledge(plan, {0, 0}, layers, 0));
    CHECK(queue.accept(plan, layers, 0)); // consumer discarded its cache
    CHECK(CpuTileCompositor::compose(geometry, {0, 0}, layers, 0).has_value());
    CHECK(queue.acknowledge(plan, {0, 0}, layers, 0));
    CHECK(bottom.dirtyRegions().empty());
    CHECK(top.dirtyRegions().size() == 1); // edge tile remains pending
    CHECK(!queue.prepare(layers, 0).invalidateAll);
    auto old = queue.prepare(layers, 0);
    CHECK(top.setPixel({4, 4}, 0xff0000ffU) == W::Changed);
    CHECK(!queue.acknowledge(old, {1, 1}, layers, 0));
    CHECK(top.dirtyRegions().size() == 1);
    auto current = queue.prepare(layers, 0);
    CHECK(queue.acknowledge(current, {1, 1}, layers, 0));
    CHECK(top.dirtyRegions().empty());
    CHECK(queue.prepare(layers, 0).tiles.empty());
    auto previous = queue.prepare(layers, 0);
    layers[0].visible = false;
    CHECK(!queue.accept(previous, layers, 0));
    CHECK(queue.prepare(layers, 0).invalidateAll);
    auto changed = queue.prepare(layers, 0);
    CHECK(queue.accept(changed, layers, 0));
    CHECK(!queue.prepare(layers, 0).invalidateAll);
    layers[0].opacity = 0.25; CHECK(queue.prepare(layers, 0).invalidateAll);
    layers[0].opacity = 1; std::swap(layers[0], layers[1]); CHECK(queue.prepare(layers, 0).invalidateAll);
    CHECK(queue.prepare(layers, 0xff000000U).invalidateAll);
    CHECK(queue.prepare({layers[0]}, 0).invalidateAll); // layer removed
    queue.reset(); CHECK(queue.prepare(layers, 0).invalidateAll);
    // A revision is never reused after empty-document resize.
    SparseTileStore resized(2, 2, 2);
    CHECK(resized.setPixel({0, 0}, 0xffffffffU) == W::Changed);
    const auto oldVersion = resized.dirtyVersion();
    CHECK(resized.acknowledgeDirty({0, 0}, oldVersion));
    CHECK(resized.setPixel({0, 0}, 0) == W::Changed);
    CHECK(resized.acknowledgeDirty({0, 0}, resized.dirtyVersion()));
    CHECK(resized.resizeEmpty(3, 3));
    CHECK(resized.setPixel({0, 0}, 0xffffffffU) == W::Changed);
    CHECK(!resized.acknowledgeDirty({0, 0}, oldVersion));
    CHECK(!resized.dirtyRegions().empty());
    std::cout << "CPU compositing tests passed\n";
}
