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
static SparseTileStore::CaptureResult rejectCapture(void*, TilePoint, bool, std::span<const std::uint32_t>) noexcept { return SparseTileStore::CaptureResult::Limit; }
static SparseTileStore::CaptureResult failCapture(void*, TilePoint, bool, std::span<const std::uint32_t>) noexcept { return SparseTileStore::CaptureResult::AllocationFailed; }
int main() {
    int failures = 0;
    for (int n = 0; n < 8; ++n) {
        SparseTileStore store(8, 8, 4);
        auto budget = std::make_shared<DocumentMemoryBudget>();
        CHECK(store.setDocumentBudget(budget));
        failAfter = n;
        auto result = store.setPixel({0, 0}, 0xffffffffU);
        failAfter = -1;
        if (result == SparseTileStore::WriteResult::AllocationFailed) {
            ++failures;
            CHECK(store.allocatedTiles() == 0);
            CHECK(store.pixel({0, 0}) == 0U);
            CHECK(store.dirtyRegions().empty());
        } else CHECK(result == SparseTileStore::WriteResult::Changed);
        CHECK(budget->artworkBytes() == store.allocatedPixelBytes());
        CHECK(budget->stagingBytes() == 0);
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
    SparseTileStore capture(8, 8, 4);
    auto captureBudget = std::make_shared<DocumentMemoryBudget>();
    CHECK(capture.setDocumentBudget(captureBudget));
    CHECK(capture.setPixel({0, 0}, 0xff123456U) == SparseTileStore::WriteResult::Changed);
    capture.setBeforeChange(rejectCapture, nullptr);
    CHECK(capture.setPixel({0, 0}, 0xff000000U) == SparseTileStore::WriteResult::HistoryLimit);
    CHECK(capture.pixel({0, 0}) == 0xff123456U);
    CHECK(capture.setPixel({4, 0}, 0xffffffffU) == SparseTileStore::WriteResult::HistoryLimit);
    CHECK(capture.allocatedTiles() == 1);
    CHECK(captureBudget->artworkBytes() == capture.allocatedPixelBytes());
    capture.setBeforeChange(failCapture, nullptr);
    CHECK(capture.setPixel({0, 0}, 0xff000000U) == SparseTileStore::WriteResult::HistoryAllocationFailed);
    CHECK(capture.pixel({0, 0}) == 0xff123456U);
    CHECK(capture.setPixel({4, 0}, 0xffffffffU) == SparseTileStore::WriteResult::HistoryAllocationFailed);
    CHECK(capture.allocatedTiles() == 1);
    CHECK(captureBudget->artworkBytes() == capture.allocatedPixelBytes());
    CHECK(captureBudget->stagingBytes() == 0);
    int restoreFailures = 0;
    for (int n = 0; n < 16; ++n) {
        SparseTileStore s(8, 8, 4);
        auto budget = std::make_shared<DocumentMemoryBudget>();
        CHECK(s.setDocumentBudget(budget));
        CHECK(s.setPixel({0, 0}, 0xffffffffU) == SparseTileStore::WriteResult::Changed);
        CHECK(s.setPixel({4, 0}, 0xff112233U) == SparseTileStore::WriteResult::Changed);
        std::vector<SparseTileStore::TileSnapshot> snapshots{{{0, 0}, {}}, {{1, 0}, std::vector<std::uint32_t>(16, 0xff00ff00U)}};
        failAfter = n;
        auto status = s.restoreTiles(snapshots);
        failAfter = -1;
        if (status != SparseTileStore::WriteResult::Changed) {
            ++restoreFailures;
            CHECK(s.pixel({0, 0}) == 0xffffffffU);
            CHECK(s.pixel({4, 0}) == 0xff112233U);
        } else {
            CHECK(s.pixel({0, 0}) == 0U);
            CHECK(s.pixel({4, 0}) == 0xff00ff00U);
        }
        CHECK(budget->artworkBytes() == s.allocatedPixelBytes());
        CHECK(budget->stagingBytes() == 0);
    }
    CHECK(restoreFailures >= 3);
    std::cout << "Allocation failure tests passed\n";
}
