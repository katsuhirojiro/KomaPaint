#include "canvas/SparseTileStore.h"
#include <cstdlib>
#include <new>
#include <iostream>

// Fail individual allocations deterministically, including tile pixels, map
// nodes/buckets and the dirty-region node. This test runs in its own process.
static int failAfter = -1;
void* operator new(std::size_t n) {
    if (failAfter == 0) { failAfter = -1; throw std::bad_alloc(); }
    if (failAfter > 0) --failAfter;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL line " << __LINE__ << '\n'; std::exit(1); } } while (false)
using namespace komapaint;
int main() {
    int failures = 0;
    for (int n = 0; n < 8; ++n) {
        SparseTileStore store(8, 8, 4);
        failAfter = n;
        auto result = store.setPixel({0, 0}, 0xffffffffU);
        failAfter = -1;
        if (result == SparseTileStore::WriteResult::AllocationFailed) {
            ++failures;
            CHECK(store.allocatedTiles() == 0);
            CHECK(store.pixel({0, 0}) == 0U);
            CHECK(store.dirtyRegions().empty());
        } else CHECK(result == SparseTileStore::WriteResult::Changed);
    }
    CHECK(failures >= 3);
    SparseTileStore store(8, 8, 4);
    CHECK(store.setPixel({0, 0}, 0xffffffffU) == SparseTileStore::WriteResult::Changed);
    while (!store.dirtyRegions().empty()) CHECK(store.acknowledgeDirty(store.dirtyRegions().begin()->first, store.dirtyVersion()));
    failAfter = 0;
    auto result = store.setPixel({1, 0}, 0xff112233U);
    failAfter = -1;
    CHECK(result == SparseTileStore::WriteResult::AllocationFailed);
    CHECK(store.pixel({0, 0}) == 0xffffffffU);
    CHECK(store.pixel({1, 0}) == 0U);
    CHECK(store.dirtyRegions().empty());
    std::cout << "Allocation failure tests passed\n";
}
