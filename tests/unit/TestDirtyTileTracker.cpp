#include "canvas/DirtyTileTracker.h"
#include "canvas/SparseTileStore.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace komapaint;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL line " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::exit(1); } } while (false)
int main()
{
    using R = DirtyTileTracker::Result;
    DirtyTileTracker tracker(TileGeometry(10, 9, 4));
    CHECK(tracker.mark({3, 3, 6, 6}) == R::Marked);
    CHECK(tracker.regions().size() == 4);
    CHECK((tracker.regions().at({0, 0}) == TileRect{3, 3, 4, 4}));
    CHECK((tracker.regions().at({1, 0}) == TileRect{0, 3, 2, 4}));
    CHECK((tracker.regions().at({0, 1}) == TileRect{3, 0, 4, 2}));
    CHECK((tracker.regions().at({1, 1}) == TileRect{0, 0, 2, 2}));
    CHECK(tracker.mark({4, 4, 8, 8}) == R::Marked);
    CHECK((tracker.regions().at({1, 1}) == TileRect{0, 0, 4, 4}));
    CHECK(tracker.regions().size() == 4);
    CHECK(tracker.mark({4, 4, 8, 8}) == R::Marked); CHECK(tracker.regions().size() == 4);
    auto previous = tracker.regions();
    CHECK(tracker.mark({-10, -10, 0, 4}) == R::NoIntersection);
    CHECK(tracker.mark({10, 0, 20, 4}) == R::NoIntersection);
    CHECK(tracker.mark({0, 0, 0, 4}) == R::NoIntersection);
    CHECK(tracker.regions() == previous);
    tracker.clear();
    CHECK(tracker.mark({-5, -5, 4, 4}) == R::Marked);
    CHECK(tracker.regions().size() == 1);
    CHECK((tracker.regions().at({0, 0}) == TileRect{0, 0, 4, 4}));
    CHECK(tracker.mark({8, 8, 20, 20}) == R::Marked);
    CHECK((tracker.regions().at({2, 2}) == TileRect{0, 0, 2, 1}));
    DirtyTileTracker bounded(TileGeometry(16, 16, 4), 2);
    CHECK(bounded.mark({0, 0, 4, 4}) == R::Marked);
    auto before = bounded.regions();
    CHECK(bounded.mark({4, 0, 12, 4}) == R::LimitExceeded);
    CHECK(bounded.regions() == before);
    CHECK(bounded.mark({0, 0, 16, 16}) == R::LimitExceeded);
    CHECK(bounded.regions() == before);
    const auto max = std::numeric_limits<std::int64_t>::max();
    DirtyTileTracker huge(TileGeometry(max, max, 1), 10);
    CHECK(huge.mark({0, 0, max, max}) == R::LimitExceeded);
    SparseTileStore store(8, 8, 4);
    using W = SparseTileStore::WriteResult;
    CHECK(store.setPixel({3, 3}, 0xffffffffU) == W::Changed);
    CHECK(store.setPixel({4, 3}, 0xff000000U) == W::Changed);
    CHECK(store.dirtyRegions().size() == 2);
    store.clearDirtyRegions();
    CHECK(store.setPixel({3, 3}, 0xffffffffU) == W::Unchanged);
    CHECK(store.setPixel({-1, 0}, 0xffffffffU) == W::OutOfBounds);
    CHECK(store.setPixel({0, 0}, 0x00ffffffU) == W::InvalidPixel);
    CHECK(store.dirtyRegions().empty());
    CHECK(store.setPixel({3, 3}, 0) == W::Changed);
    CHECK(store.pixel({3, 3}) == 0U);
    CHECK((store.dirtyRegions().at({0, 0}) == TileRect{3, 3, 4, 4}));
    SparseTileStore limited(8, 8, 4, 0);
    CHECK(limited.setPixel({0, 0}, 0xffffffffU) == W::MemoryLimit);
    CHECK(limited.dirtyRegions().empty());
    std::cout << "Dirty tile tracking tests passed\n";
}
