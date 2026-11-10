#include <QCoreApplication>
#include "history/UndoStack.h"
#include "layers/Layer.h"
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
    std::cout << "History allocation-failure tests passed\n";
}
