#include <QCoreApplication>
#include "history/UndoStack.h"
#include "layers/Layer.h"
#include <vector>
#include <cstdlib>
#include <iostream>
#include <new>

// Isolated process: inject failures into C++ allocations without affecting
// unrelated test executables. Arm only around the operation under test.
static int failAfter = -1;
void* operator new(std::size_t n) {
    if (failAfter == 0) { failAfter = -1; throw std::bad_alloc(); }
    if (failAfter > 0) --failAfter;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#define CHECK(...) do { if (!(__VA_ARGS__)) { failAfter = -1; std::cerr << "FAIL line " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::exit(1); } } while (false)
using namespace komapaint;
static std::vector<std::uint32_t> copyTile(const SparseTileStore& store, TilePoint p) {
    const auto pixels = store.tilePixels(p); return {pixels.begin(), pixels.end()};
}
// Run each fail point twice on the same history, then retry with allocation
// enabled. Commands span several tiles and two layers; each command is a
// single-layer stroke, as required by the current application model.
static void replayFailures(bool redo) {
    using R = SparseTileStore::WriteResult;
    int failures = 0;
    for (int n = 0; n < 40; ++n) {
        auto budget = std::make_shared<DocumentMemoryBudget>();
        Layer a, b; UndoStack history;
        auto& x = a.tiles(); auto& y = b.tiles();
        CHECK(x.setDocumentBudget(budget)); CHECK(y.setDocumentBudget(budget));
        CHECK(x.setPixel({1, 1}, 0x80402010U) == R::Changed);
        CHECK(x.setPixel({257, 1}, 0xff123456U) == R::Changed);
        CHECK(y.setPixel({1, 1}, 0x80604020U) == R::Changed);
        CHECK(history.beginStroke(&a));
        CHECK(x.setPixel({1, 1}, 0xff102030U) == R::Changed);
        CHECK(x.setPixel({257, 1}, 0U) == R::Changed);
        CHECK(x.setPixel({513, 1}, 0x80800000U) == R::Changed);
        CHECK(history.commitStroke());
        CHECK(history.beginStroke(&b));
        CHECK(y.setPixel({1, 1}, 0xffabcdefU) == R::Changed);
        CHECK(history.commitStroke());
        history.undo(); // target next undo is the multi-tile command on a
        if (redo) history.undo(); // redo will recreate/reclaim tiles on a
        const auto t0 = copyTile(x, {0, 0});
        const auto t1 = copyTile(x, {1, 0});
        const auto t2 = copyTile(x, {2, 0});
        const auto other = copyTile(y, {0, 0});
        const auto dirtyX = x.dirtyRegions();
        const auto dirtyY = y.dirtyRegions();
        const auto versionX = x.dirtyVersion();
        const auto versionY = y.dirtyVersion();
        const auto bytes = budget->usedBytes();
        const auto tileCount = x.allocatedTiles();
        int errors = 0;
        QObject::connect(&history, &UndoStack::errorOccurred, &history, [&errors](const QString&) { ++errors; });
        auto attempt = [&] { if (redo) history.redo(); else history.undo(); };
        failAfter = n;
        attempt();
        failAfter = -1;
        if (errors == 0) continue; // this index is beyond all allocations
        ++failures;
        CHECK(copyTile(x, {0, 0}) == t0 && copyTile(x, {1, 0}) == t1 && copyTile(x, {2, 0}) == t2);
        CHECK(copyTile(y, {0, 0}) == other);
        CHECK(x.allocatedTiles() == tileCount);
        CHECK(x.dirtyRegions() == dirtyX && x.dirtyVersion() == versionX);
        CHECK(y.dirtyRegions() == dirtyY && y.dirtyVersion() == versionY);
        CHECK(budget->usedBytes() == bytes && budget->stagingBytes() == 0);
        CHECK(redo ? history.canRedo() : history.canUndo());
        CHECK(history.canRedo()); // undo failure must never destroy redo
        failAfter = n;
        attempt();
        failAfter = -1;
        CHECK(errors == 2);
        CHECK(copyTile(x, {0, 0}) == t0 && copyTile(x, {1, 0}) == t1 && copyTile(x, {2, 0}) == t2);
        CHECK(budget->usedBytes() == bytes && budget->stagingBytes() == 0);
        attempt();
        CHECK(errors == 2);
        CHECK(x.pixel({1, 1}) == (redo ? 0xff102030U : 0x80402010U));
        CHECK(x.pixel({257, 1}) == (redo ? 0U : 0xff123456U));
        CHECK(x.pixel({513, 1}) == (redo ? 0x80800000U : 0U));
        CHECK(copyTile(y, {0, 0}) == other);
        CHECK(budget->stagingBytes() == 0);
    }
    CHECK(failures >= 6);
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using R = SparseTileStore::WriteResult;
    int startFailures = 0;
    for (int n = 0; n < 8; ++n) {
        Layer layer; UndoStack history;
        failAfter = n;
        const bool begun = history.beginStroke(&layer);
        failAfter = -1;
        if (!begun) { ++startFailures; CHECK(!history.strokeActive()); }
        else CHECK(history.cancelStroke());
        CHECK(layer.tiles().allocatedTiles() == 0);
    }
    CHECK(startFailures >= 2);
    int writeFailures = 0;
    for (int n = 0; n < 24; ++n) {
        Layer layer; UndoStack history;
        auto budget = std::make_shared<DocumentMemoryBudget>();
        auto& store = layer.tiles();
        CHECK(store.setDocumentBudget(budget));
        CHECK(store.setPixel({1, 1}, 0x80402010U) == R::Changed);
        CHECK(history.beginStroke(&layer));
        CHECK(store.setPixel({1, 1}, 0U) == R::Changed);
        // Already-erased first tile must survive failures in creation, history
        // capture, vector growth, or dirty tracking of the second tile.
        failAfter = n;
        const auto result = store.setPixel({257, 1}, 0xff123456U);
        failAfter = -1;
        if (result != R::Changed) ++writeFailures;
        failAfter = 0;
        CHECK(history.cancelStroke());
        CHECK(failAfter == 0); // no C++ allocation in this rollback path
        failAfter = -1;
        CHECK(store.pixel({1, 1}) == 0x80402010U);
        CHECK(store.pixel({257, 1}) == 0U);
        CHECK(store.allocatedTiles() == 1);
        CHECK(budget->artworkBytes() == store.allocatedPixelBytes());
        CHECK(budget->historyBytes() == 0);
        CHECK(!history.canUndo() && !history.canRedo());
    }
    CHECK(writeFailures >= 4);
    {
        Layer layer; UndoStack history;
        auto& store = layer.tiles();
        CHECK(history.beginStroke(&layer));
        CHECK(store.setPixel({1, 1}, 0x80402010U) == R::Changed);
        // All command/vector storage was reserved before mutation.
        failAfter = 0;
        CHECK(history.commitStroke());
        CHECK(failAfter == 0);
        failAfter = -1;
        CHECK(history.canUndo());
        history.undo(); CHECK(store.pixel({1, 1}) == 0U);
        history.redo(); CHECK(store.pixel({1, 1}) == 0x80402010U);
    }
    replayFailures(false);
    replayFailures(true);
    std::cout << "History allocation-failure tests passed\n";
}
