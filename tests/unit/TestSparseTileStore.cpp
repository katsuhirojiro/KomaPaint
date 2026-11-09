#include "canvas/SparseTileStore.h"
#include <cstdlib>
#include <iostream>
using namespace komapaint;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL line " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::exit(1); } } while (false)
int main()
{
    using R = SparseTileStore::WriteResult;
    SparseTileStore store(9, 5, 4, 128); // two 4x4 tiles
    CHECK(store.isValid()); CHECK(store.allocatedTiles() == 0);
    CHECK(store.pixel({8, 4}) == 0U); CHECK(!store.pixel({9, 4}));
    CHECK(store.setPixel({0, 0}, 0) == R::Unchanged); CHECK(store.allocatedTiles() == 0);
    CHECK(store.setPixel({-1, 0}, 0xff000000U) == R::OutOfBounds);
    CHECK(store.setPixel({0, 0}, 0x00112233U) == R::InvalidPixel);
    CHECK(store.setPixel({0, 0}, 0x80800000U) == R::Changed);
    CHECK(store.pixel({0, 0}) == 0x80800000U);
    CHECK(store.setPixel({0, 0}, 0x80800000U) == R::Unchanged);
    CHECK(store.setPixel({3, 3}, 0xff010203U) == R::Changed);
    CHECK(store.setPixel({3, 3}, 0xffaabbccU) == R::Changed);
    CHECK(store.setPixel({0, 0}, 0xff000000U) == R::Changed);
    CHECK(store.pixel({3, 3}) == 0xffaabbccU);
    CHECK(store.setPixel({4, 3}, 0xffffffffU) == R::Changed);
    CHECK(store.allocatedTiles() == 2); CHECK(store.allocatedPixelBytes() == 128);
    CHECK(store.setPixel({8, 4}, 0xffffffffU) == R::MemoryLimit);
    CHECK(store.pixel({8, 4}) == 0U); CHECK(!store.resizeEmpty(3, 3));
    CHECK(store.setPixel({0, 0}, 0) == R::Changed); CHECK(store.allocatedTiles() == 2);
    CHECK(store.pixel({3, 3}) == 0xffaabbccU);
    CHECK(store.setPixel({3, 3}, 0) == R::Changed); CHECK(store.allocatedTiles() == 1);
    CHECK(store.setPixel({8, 4}, 0xffffffffU) == R::Changed);
    CHECK(store.pixel({4, 3}) == 0xffffffffU);
    CHECK(store.setPixel({4, 3}, 0) == R::Changed);
    CHECK(store.setPixel({8, 4}, 0) == R::Changed);
    CHECK(store.allocatedTiles() == 0); CHECK(!store.resizeEmpty(2, 3));
    store.clearDirtyRegions(); CHECK(store.resizeEmpty(2, 3));
    CHECK(!store.pixel({2, 0})); CHECK(store.pixel({1, 2}) == 0U);
    CHECK(!SparseTileStore(1, 1, 0).isValid());
    CHECK(!SparseTileStore(1, 1, 4097).isValid());
    SparseTileStore a(4, 4, 4), b(4, 4, 4);
    CHECK(a.setPixel({0, 0}, 0xffffffffU) == R::Changed); CHECK(b.pixel({0, 0}) == 0U);
    std::cout << "Sparse tile storage tests passed\n";
}
