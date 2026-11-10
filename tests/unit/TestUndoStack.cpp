#include <QtTest/QtTest>
#include "history/UndoStack.h"
#include "layers/Layer.h"
#include "drawing/BrushEngine.h"
using namespace komapaint;
class TestUndoStack : public QObject {
    Q_OBJECT
private slots:
    void exactStrokesAndLayers();
    void cancellationAndLimits();
    void failedStrokePreservesRedoAndTileState();
    void sharedHistoryBudget();
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
    QVERIFY(history.beginStroke(&layer));
    QVERIFY(history.commitStroke()); QVERIFY(!history.canUndo());
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(layer.tiles().setPixel({0, 0}, 0x80800000U), SparseTileStore::WriteResult::Changed);
    QVERIFY(history.cancelStroke());
    QCOMPARE(layer.tiles().pixel({0, 0}).value(), 0U);
    QCOMPARE(layer.tiles().allocatedTiles(), size_t(0));
    QVERIFY(!history.canUndo());
    QVERIFY(history.setByteLimit(1));
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(layer.tiles().setPixel({1, 1}, 0xffffffffU), SparseTileStore::WriteResult::HistoryLimit);
    QCOMPARE(layer.tiles().allocatedTiles(), size_t(0));
    QVERIFY(!history.commitStroke()); QVERIFY(!history.canUndo());
    QCOMPARE(history.memoryBytes(), qulonglong(0));
    QVERIFY(history.setByteLimit(2 * layer.tiles().tileBytes() + 512));
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(layer.tiles().setPixel({1, 1}, 0x80800000U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(layer.tiles().setPixel({257, 1}, 0xffffffffU), SparseTileStore::WriteResult::HistoryLimit);
    QVERIFY(!history.commitStroke()); QVERIFY(!history.canUndo());
    QCOMPARE(layer.tiles().pixel({1, 1}).value(), 0U);
    QCOMPARE(layer.tiles().allocatedTiles(), size_t(0));
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
    QCOMPARE(store.setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    const auto initial = snapshot(store, {0, 0});
    const auto charge = 2 * store.tileBytes() + 512;
    QVERIFY(history.setByteLimit(charge * 2));
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(store.setPixel({1, 1}, 0xffaabbccU), SparseTileStore::WriteResult::Changed);
    QVERIFY(history.commitStroke());
    const auto painted = snapshot(store, {0, 0});
    history.undo();
    QVERIFY(history.canRedo());
    QVERIFY(snapshot(store, {0, 0}) == initial);
    QVERIFY(history.beginStroke(&layer));
    QCOMPARE(store.setPixel({1, 1}, 0U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(store.setPixel({257, 1}, 0xff000000U), SparseTileStore::WriteResult::HistoryLimit);
    QVERIFY(!history.commitStroke());
    QVERIFY(snapshot(store, {0, 0}) == initial);
    QVERIFY(snapshot(store, {1, 0}).empty());
    QCOMPARE(store.allocatedTiles(), size_t(1));
    QVERIFY(history.canRedo());
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

QTEST_APPLESS_MAIN(TestUndoStack)
#include "TestUndoStack.moc"
