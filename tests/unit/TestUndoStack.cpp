#include <QtTest/QtTest>
#include "history/UndoStack.h"
#include "layers/Layer.h"
#include "layers/LayerModel.h"
#include "canvas/CanvasDocument.h"
#include "drawing/BrushEngine.h"
#include <limits>
using namespace komapaint;
static bool simulatedPressure = false;
static bool testMemoryCheck(std::size_t) noexcept { return !simulatedPressure; }
class TestUndoStack : public QObject {
    Q_OBJECT
private slots:
    void exactStrokesAndLayers();
    void cancellationAndLimits();
    void failedStrokePreservesRedoAndTileState();
    void sharedHistoryBudget();
    void replayBudgetFailureAndRetry();
    void replayRejectsReentry();
    void compactRejectsIncompatibleReplay();
    void pressureFailureAndRecovery();
    void strokeRejectsNotificationReentry();
    void layerRemovedDuringReplay();
    void ownerDestroyedDuringCallbacks();
    void accountingLifetimes();
    void unchangedTilesReleaseHistoryCharge();
};
static std::vector<std::uint32_t> snapshot(const SparseTileStore& s, TilePoint p) {
    auto pixels = s.tilePixels(p); return {pixels.begin(), pixels.end()};
}
static bool draw(UndoStack& history, Layer& layer, std::uint32_t color, double x, double y) {
    if (!history.beginStroke(&layer)) return false;
    BrushDefinition def; def.size = 12; def.opacity = 0.5; def.color = color;
    BrushStroke stroke(layer.tiles(), def);
    if (!stroke.begin({x, y, 1}) || !stroke.end({x + 20, y, 1})) { history.commitStroke(); return false; }
    return history.commitStroke();
}
void TestUndoStack::exactStrokesAndLayers() {
    UndoStack history; Layer a, b;
    QVERIFY(a.tiles().resizeEmpty(520, 300)); QVERIFY(b.tiles().resizeEmpty(520, 300));
    // Include transparent, partially transparent and unrelated artwork.
    QCOMPARE(a.tiles().setPixel({255, 3}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(a.tiles().setPixel({400, 40}, 0xff123456U), SparseTileStore::WriteResult::Changed);
    auto initial0 = snapshot(a.tiles(), {0, 0});
    auto initial1 = snapshot(a.tiles(), {1, 0});
    auto unrelated = snapshot(a.tiles(), {1, 0});
    QVERIFY(draw(history, a, 0xffff0000U, 250, 3));
    auto first0 = snapshot(a.tiles(), {0, 0}); auto first1 = snapshot(a.tiles(), {1, 0});
    QVERIFY(draw(history, a, 0xff0000ffU, 250, 3));
    auto second0 = snapshot(a.tiles(), {0, 0}); auto second1 = snapshot(a.tiles(), {1, 0});
    QVERIFY(draw(history, b, 0xff00ff00U, 250, 3));
    auto bpaint = snapshot(b.tiles(), {0, 0});
    history.undo(); QVERIFY(snapshot(b.tiles(), {0, 0}).empty());
    QVERIFY(snapshot(a.tiles(), {0, 0}) == second0);
    history.undo(); QVERIFY(snapshot(a.tiles(), {0, 0}) == first0); QVERIFY(snapshot(a.tiles(), {1, 0}) == first1);
    history.undo(); QVERIFY(snapshot(a.tiles(), {0, 0}) == initial0); QVERIFY(snapshot(a.tiles(), {1, 0}) == initial1);
    QVERIFY(!history.canUndo());
    history.redo(); QVERIFY(snapshot(a.tiles(), {0, 0}) == first0);
    history.redo(); QVERIFY(snapshot(a.tiles(), {0, 0}) == second0); QVERIFY(snapshot(a.tiles(), {1, 0}) == second1);
    history.redo(); QVERIFY(snapshot(b.tiles(), {0, 0}) == bpaint);
    QVERIFY(!history.canRedo());
    history.undo(); QVERIFY(history.canRedo());
    QVERIFY(draw(history, a, 0xff00ff00U, 250, 3)); QVERIFY(!history.canRedo());
    QCOMPARE(a.tiles().pixel({400, 40}).value(), 0xff123456U);
    QVERIFY(!unrelated.empty());
}
void TestUndoStack::cancellationAndLimits() {
    UndoStack history; Layer layer;
    auto budget = std::make_shared<DocumentMemoryBudget>();
    QVERIFY(layer.tiles().setDocumentBudget(budget));
    QVERIFY(history.beginStroke(&layer));
    QVERIFY(history.commitStroke()); QVERIFY(!history.canUndo());
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(layer.tiles().setPixel({0, 0}, 0x80800000U), SparseTileStore::WriteResult::Changed);
    QVERIFY(history.cancelStroke());
    QCOMPARE(layer.tiles().pixel({0, 0}).value(), 0U);
    QCOMPARE(layer.tiles().allocatedTiles(), size_t(0));
    QVERIFY(!history.canUndo());
    QVERIFY(budget->setLimit(1));
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(layer.tiles().setPixel({1, 1}, 0xffffffffU), SparseTileStore::WriteResult::DocumentMemoryLimit);
    QCOMPARE(layer.tiles().allocatedTiles(), size_t(0));
    QVERIFY(!history.commitStroke()); QVERIFY(!history.canUndo());
    QCOMPARE(history.memoryBytes(), qulonglong(0));
    QVERIFY(budget->setLimit(2 * layer.tiles().tileBytes()));
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(layer.tiles().setPixel({1, 1}, 0x80800000U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(layer.tiles().setPixel({257, 1}, 0xffffffffU), SparseTileStore::WriteResult::DocumentMemoryLimit);
    QVERIFY(!history.commitStroke()); QVERIFY(!history.canUndo());
    QCOMPARE(layer.tiles().pixel({1, 1}).value(), 0U);
    QCOMPARE(layer.tiles().allocatedTiles(), size_t(0));
    QVERIFY(budget->setLimit(std::numeric_limits<std::size_t>::max()));
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(layer.tiles().setPixel({1, 1}, 0x80800000U), SparseTileStore::WriteResult::Changed);
    QVERIFY(history.commitStroke()); QVERIFY(history.canUndo());
    history.undo(); QCOMPARE(layer.tiles().pixel({1, 1}).value(), 0U);
    history.redo(); QCOMPARE(layer.tiles().pixel({1, 1}).value(), 0x80800000U);
}
void TestUndoStack::failedStrokePreservesRedoAndTileState()
{
    UndoStack history;
    Layer layer;
    auto& store = layer.tiles();
    auto budget = std::make_shared<DocumentMemoryBudget>();
    QVERIFY(store.setDocumentBudget(budget));
    QCOMPARE(store.setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    const auto initial = snapshot(store, {0, 0});
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(store.setPixel({1, 1}, 0xffaabbccU), SparseTileStore::WriteResult::Changed);
    QVERIFY(history.commitStroke());
    const auto painted = snapshot(store, {0, 0});
    history.undo();
    QVERIFY(history.canRedo());
    QVERIFY(snapshot(store, {0, 0}) == initial);
    const auto dirtyBefore = store.dirtyRegions();
    const auto versionBefore = store.dirtyVersion();
    QVERIFY(budget->setLimit(budget->usedBytes() + 2 * store.tileBytes()));
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(store.setPixel({1, 1}, 0U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(store.setPixel({257, 1}, 0xff000000U), SparseTileStore::WriteResult::DocumentMemoryLimit);
    QVERIFY(!history.commitStroke());
    QVERIFY(snapshot(store, {0, 0}) == initial);
    QVERIFY(snapshot(store, {1, 0}).empty());
    QCOMPARE(store.allocatedTiles(), size_t(1));
    QVERIFY(store.dirtyRegions() == dirtyBefore);
    QCOMPARE(store.dirtyVersion(), versionBefore);
    QVERIFY(history.canRedo());
    QVERIFY(budget->setLimit(std::numeric_limits<std::size_t>::max()));
    history.redo();
    QVERIFY(snapshot(store, {0, 0}) == painted);
    history.undo();
    QVERIFY(snapshot(store, {0, 0}) == initial);
}
void TestUndoStack::sharedHistoryBudget()
{
    UndoStack history;
    Layer layer;
    auto budget = std::make_shared<DocumentMemoryBudget>();
    auto& store = layer.tiles();
    QVERIFY(store.setDocumentBudget(budget));
    const auto tile = store.tileBytes();
    QCOMPARE(store.setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    QVERIFY(budget->setLimit(2 * tile));
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(store.setPixel({1, 1}, 0xff123456U), SparseTileStore::WriteResult::DocumentMemoryLimit);
    QVERIFY(!history.commitStroke());
    QCOMPARE(store.pixel({1, 1}).value(), 0x80402010U);
    QCOMPARE(budget->historyBytes(), size_t(0));
    QVERIFY(budget->setLimit(3 * tile));
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(store.setPixel({1, 1}, 0xff123456U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(budget->historyBytes(), 2 * tile);
    QVERIFY(history.commitStroke());
    QCOMPARE(budget->historyBytes(), 2 * tile);
    history.clear();
    QCOMPARE(budget->historyBytes(), size_t(0));
    QCOMPARE(budget->artworkBytes(), tile);
}

void TestUndoStack::replayBudgetFailureAndRetry()
{
    UndoStack history;
    Layer a, b;
    auto budget = std::make_shared<DocumentMemoryBudget>();
    auto& store = a.tiles();
    QVERIFY(store.setDocumentBudget(budget));
    QVERIFY(b.tiles().setDocumentBudget(budget));
    QCOMPARE(store.setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(store.setPixel({257, 1}, 0xff123456U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(b.tiles().setPixel({1, 1}, 0xffabcdefU), SparseTileStore::WriteResult::Changed);
    const auto initial0 = snapshot(store, {0, 0});
    const auto initial1 = snapshot(store, {1, 0});
    const auto other = snapshot(b.tiles(), {0, 0});
    QVERIFY(history.beginStroke(&a));
    QCOMPARE(store.setPixel({1, 1}, 0xff010203U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(store.setPixel({257, 1}, 0U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(store.setPixel({513, 1}, 0x80800000U), SparseTileStore::WriteResult::Changed);
    QVERIFY(history.commitStroke());
    const auto painted0 = snapshot(store, {0, 0});
    const auto painted2 = snapshot(store, {2, 0});
    const auto used = budget->usedBytes();
    QVERIFY(budget->setLimit(used));
    QSignalSpy errors(&history, &UndoStack::errorOccurred);
    history.undo(); history.undo();
    QCOMPARE(errors.size(), 2);
    QVERIFY(history.canUndo()); QVERIFY(!history.canRedo());
    QVERIFY(snapshot(store, {0, 0}) == painted0);
    QVERIFY(snapshot(store, {1, 0}).empty());
    QVERIFY(snapshot(store, {2, 0}) == painted2);
    QVERIFY(snapshot(b.tiles(), {0, 0}) == other);
    QCOMPARE(budget->usedBytes(), used); QCOMPARE(budget->stagingBytes(), size_t(0));
    QVERIFY(budget->setLimit(used + 8 * store.tileBytes()));
    history.undo();
    QVERIFY(!history.canUndo()); QVERIFY(history.canRedo());
    QVERIFY(snapshot(store, {0, 0}) == initial0);
    QVERIFY(snapshot(store, {1, 0}) == initial1);
    QVERIFY(snapshot(store, {2, 0}).empty());
    QVERIFY(snapshot(b.tiles(), {0, 0}) == other);
    const auto undoUsed = budget->usedBytes();
    QVERIFY(budget->setLimit(undoUsed));
    history.redo(); history.redo();
    QCOMPARE(errors.size(), 4);
    QVERIFY(!history.canUndo()); QVERIFY(history.canRedo());
    QVERIFY(snapshot(store, {0, 0}) == initial0);
    QVERIFY(snapshot(store, {1, 0}) == initial1);
    QVERIFY(snapshot(store, {2, 0}).empty());
    QCOMPARE(budget->usedBytes(), undoUsed); QCOMPARE(budget->stagingBytes(), size_t(0));
    QVERIFY(budget->setLimit(undoUsed + 8 * store.tileBytes()));
    history.redo();
    QVERIFY(history.canUndo()); QVERIFY(!history.canRedo());
    QVERIFY(snapshot(store, {0, 0}) == painted0);
    QVERIFY(snapshot(store, {1, 0}).empty());
    QVERIFY(snapshot(store, {2, 0}) == painted2);
    QVERIFY(snapshot(b.tiles(), {0, 0}) == other);
}
void TestUndoStack::replayRejectsReentry()
{
    UndoStack history;
    Layer layer;
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(layer.tiles().setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    QVERIFY(history.commitStroke());
    int callbacks = 0;
    connect(&layer, &Layer::artworkChanged, &history, [&] {
        ++callbacks;
        history.undo();
        history.redo();
        history.clear();
        QVERIFY(!history.beginStroke(&layer));
    });
    history.undo();
    QCOMPARE(callbacks, 1);
    QVERIFY(!history.canUndo()); QVERIFY(history.canRedo());
    QCOMPARE(layer.tiles().pixel({1, 1}).value(), 0U);
    history.redo();
    QCOMPARE(callbacks, 2);
    QVERIFY(history.canUndo()); QVERIFY(!history.canRedo());
    QCOMPARE(layer.tiles().pixel({1, 1}).value(), 0x80402010U);
}





void TestUndoStack::pressureFailureAndRecovery()
{
    auto budget = std::make_shared<DocumentMemoryBudget>(std::numeric_limits<std::size_t>::max(), testMemoryCheck);
    Layer layer; UndoStack history;
    auto& store = layer.tiles();
    QVERIFY(store.setDocumentBudget(budget));
    QCOMPARE(store.setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(store.setPixel({1, 1}, 0xff123456U), SparseTileStore::WriteResult::Changed);
    QVERIFY(history.commitStroke());
    history.undo();
    QVERIFY(history.canRedo());
    const auto dirty = store.dirtyRegions();
    const auto version = store.dirtyVersion();
    const auto used = budget->usedBytes();
    for (int i = 0; i < 2; ++i) {
        QVERIFY(history.beginStroke(&layer));
        QCOMPARE(store.setPixel({1, 1}, 0U), SparseTileStore::WriteResult::Changed);
        simulatedPressure = true;
        const auto result = store.setPixel({257, 1}, 0xffabcdefU);
        simulatedPressure = false;
        QCOMPARE(result, SparseTileStore::WriteResult::DocumentMemoryLimit);
        QVERIFY(!history.commitStroke());
        QCOMPARE(store.pixel({1, 1}).value(), 0x80402010U);
        QCOMPARE(store.allocatedTiles(), size_t(1));
        QVERIFY(store.dirtyRegions() == dirty); QCOMPARE(store.dirtyVersion(), version);
        QCOMPARE(budget->usedBytes(), used); QVERIFY(history.canRedo());
    }
    simulatedPressure = true;
    history.redo();
    simulatedPressure = false;
    QVERIFY(history.canRedo()); QCOMPARE(store.pixel({1, 1}).value(), 0x80402010U);
    QCOMPARE(budget->usedBytes(), used); QCOMPARE(budget->stagingBytes(), size_t(0));
    history.redo();
    QVERIFY(history.canUndo()); QVERIFY(!history.canRedo());
    QCOMPARE(store.pixel({1, 1}).value(), 0xff123456U);
    // If pressure appears only at optional compaction, keep the fully
    // admitted raw record, rather than rejecting or losing the stroke.
    history.clear();
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(store.setPixel({1, 1}, 0x80800000U), SparseTileStore::WriteResult::Changed);
    simulatedPressure = true;
    const bool committed = history.commitStroke();
    simulatedPressure = false;
    QVERIFY(committed); QVERIFY(history.canUndo());
    QCOMPARE(budget->historyBytes(), 2 * store.tileBytes());
    history.undo(); QCOMPARE(store.pixel({1, 1}).value(), 0xff123456U);
    history.redo(); QCOMPARE(store.pixel({1, 1}).value(), 0x80800000U);
}

void TestUndoStack::compactRejectsIncompatibleReplay()
{
    Layer layer; UndoStack history;
    auto& store = layer.tiles();
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(store.setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    QVERIFY(history.commitStroke());
    QVERIFY(history.memoryBytes() < store.tileBytes()); // compact representation
    QCOMPARE(store.setPixel({1, 1}, 0xff123456U), SparseTileStore::WriteResult::Changed);
    const auto dirty = store.dirtyRegions();
    const auto version = store.dirtyVersion();
    const auto bytes = history.memoryBytes();
    QSignalSpy errors(&history, &UndoStack::errorOccurred);
    history.undo(); history.undo();
    QCOMPARE(errors.size(), 2);
    QCOMPARE(store.pixel({1, 1}).value(), 0xff123456U);
    QCOMPARE(store.allocatedTiles(), size_t(1));
    QVERIFY(store.dirtyRegions() == dirty); QCOMPARE(store.dirtyVersion(), version);
    QVERIFY(history.canUndo()); QVERIFY(!history.canRedo());
    QCOMPARE(history.memoryBytes(), bytes);
    QCOMPARE(store.setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    history.undo(); QVERIFY(history.canRedo());
    QCOMPARE(store.allocatedTiles(), size_t(0));
    history.redo(); QCOMPARE(store.pixel({1, 1}).value(), 0x80402010U);
}

void TestUndoStack::strokeRejectsNotificationReentry()
{
    Layer layer; UndoStack history;
    QVERIFY(history.beginStroke(&layer));
    int calls = 0;
    connect(&layer, &Layer::artworkChanged, &history, [&] {
        ++calls;
        QVERIFY(!history.cancelStroke());
        QVERIFY(!history.commitStroke());
        history.clear();
        QVERIFY(history.strokeActive());
    });
    QCOMPARE(layer.tiles().setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(calls, 1);
    QVERIFY(history.strokeActive());
    QVERIFY(history.commitStroke());
    QVERIFY(history.canUndo());
}

void TestUndoStack::layerRemovedDuringReplay()
{
    for (bool redo : {false, true}) {
        LayerModel model;
        model.addLayer();
        Layer* layer = model.layerAt(1);
        Layer* other = model.layerAt(0);
        auto budget = model.documentBudget();
        UndoStack history;
        QCOMPARE(other->tiles().setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
        QVERIFY(history.beginStroke(layer));
        QCOMPARE(layer->tiles().setPixel({1, 1}, 0xff123456U), SparseTileStore::WriteResult::Changed);
        QCOMPARE(layer->tiles().setPixel({257, 1}, 0x80800000U), SparseTileStore::WriteResult::Changed);
        QVERIFY(history.commitStroke());
        if (redo) history.undo();
        QPointer<Layer> observed = layer;
        int callbacks = 0;
        connect(layer, &Layer::artworkChanged, &history, [&] {
            ++callbacks;
            model.removeLayer(1);
        });
        if (redo) history.redo(); else history.undo();
        QVERIFY(!observed);
        QCOMPARE(callbacks, 1); // do not access the deleted store for the next tile
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(other->tiles().pixel({1, 1}).value(), 0x80402010U);
        QCOMPARE(budget->artworkBytes(), other->tiles().tileBytes());
        QCOMPARE(budget->stagingBytes(), size_t(0));
        const auto historyBytes = history.memoryBytes();
        QSignalSpy errors(&history, &UndoStack::errorOccurred);
        if (redo) { QVERIFY(history.canUndo()); history.undo(); }
        else { QVERIFY(history.canRedo()); history.redo(); }
        QCOMPARE(errors.size(), 1);
        QCOMPARE(history.memoryBytes(), historyBytes); // missing target does not erase history
        if (redo) QVERIFY(history.canUndo()); else QVERIFY(history.canRedo());
        history.clear();
        QCOMPARE(budget->historyBytes(), size_t(0));
    }
}


void TestUndoStack::ownerDestroyedDuringCallbacks()
{
    using R = SparseTileStore::WriteResult;
    for (bool redo : {false, true}) {
        Layer layer;
        auto* history = new UndoStack;
        QVERIFY(history->beginStroke(&layer));
        QCOMPARE(layer.tiles().setPixel({1, 1}, 0x80402010U), R::Changed);
        QCOMPARE(layer.tiles().setPixel({257, 1}, 0xff123456U), R::Changed);
        QVERIFY(history->commitStroke());
        if (redo) history->undo();
        QPointer<UndoStack> observed(history);
        connect(&layer, &Layer::artworkChanged, &layer, [&] { delete history; });
        if (redo) history->redo(); else history->undo();
        QVERIFY(!observed);
        QCOMPARE(layer.tiles().pixel({1, 1}).value(), redo ? 0x80402010U : 0U);
        QCOMPARE(layer.tiles().pixel({257, 1}).value(), redo ? 0xff123456U : 0U);
    }
    {
        auto* doc = new CanvasDocument;
        Layer* layer = doc->layers()->layerAt(0);
        UndoStack history;
        QVERIFY(history.beginStroke(layer));
        QCOMPARE(layer->tiles().setPixel({1, 1}, 0x80402010U), R::Changed);
        QCOMPARE(layer->tiles().setPixel({257, 1}, 0xff123456U), R::Changed);
        QVERIFY(history.commitStroke());
        QPointer<CanvasDocument> observed(doc);
        connect(layer, &Layer::artworkChanged, &history, [&] { delete doc; });
        history.undo();
        QVERIFY(!observed);
        QVERIFY(history.canRedo());
        QSignalSpy errors(&history, &UndoStack::errorOccurred);
        history.redo();
        QCOMPARE(errors.size(), 1);
        QVERIFY(history.canRedo());
    }
    {
        Layer layer;
        auto* history = new UndoStack;
        QVERIFY(history->beginStroke(&layer));
        QCOMPARE(layer.tiles().setPixel({1, 1}, 0x80402010U), R::Changed);
        QPointer<UndoStack> observed(history);
        connect(history, &UndoStack::strokeRolledBack, &layer, [history] { delete history; });
        history->clear();
        QVERIFY(!observed);
        QCOMPARE(layer.tiles().pixel({1, 1}).value(), 0U);
    }
}

void TestUndoStack::accountingLifetimes()
{
    auto budget = std::make_shared<DocumentMemoryBudget>();
    const std::size_t tile = 256U * 256U * 4U;
    {
        Layer layer;
        QVERIFY(layer.tiles().setDocumentBudget(budget));
        {
            UndoStack history;
            QVERIFY(history.beginStroke(&layer));
            QCOMPARE(layer.tiles().setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
            QVERIFY(history.commitStroke());
            QCOMPARE(budget->artworkBytes(), tile);
            QVERIFY(budget->historyBytes() > 0 && budget->historyBytes() < tile);
            const auto retained = budget->historyBytes();
            history.undo();
            QCOMPARE(budget->artworkBytes(), size_t(0));
            QCOMPARE(budget->historyBytes(), retained);
            history.redo();
            QCOMPARE(budget->artworkBytes(), tile);
            QCOMPARE(budget->stagingBytes(), size_t(0));
        }
        QCOMPARE(budget->historyBytes(), size_t(0));
        QCOMPARE(budget->artworkBytes(), tile);
    }
    QCOMPARE(budget->usedBytes(), size_t(0));
    QVERIFY(budget->peakBytes() >= 2 * tile);
}


void TestUndoStack::unchangedTilesReleaseHistoryCharge()
{
    Layer layer; UndoStack history;
    auto budget = std::make_shared<DocumentMemoryBudget>();
    auto& store = layer.tiles();
    QVERIFY(store.setDocumentBudget(budget));
    const auto tile = store.tileBytes();
    QCOMPARE(store.setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(store.setPixel({1, 1}, 0xff123456U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(store.setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(store.setPixel({257, 1}, 0x80800000U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(budget->historyBytes(), 3 * tile);
    QVERIFY(history.commitStroke());
    QVERIFY(budget->historyBytes() > 0 && budget->historyBytes() < tile);
    QVERIFY(history.memoryBytes() < qulonglong(2 * tile + 512));
    history.undo();
    QCOMPARE(store.pixel({1, 1}).value(), 0x80402010U);
    QCOMPARE(store.pixel({257, 1}).value(), 0U);
    history.redo();
    QCOMPARE(store.pixel({257, 1}).value(), 0x80800000U);
    history.clear();
    QCOMPARE(budget->historyBytes(), size_t(0));
}

QTEST_APPLESS_MAIN(TestUndoStack)
#include "TestUndoStack.moc"
