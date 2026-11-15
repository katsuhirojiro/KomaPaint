#include <QtTest/QtTest>

#include "canvas/CanvasDocument.h"
#include "files/DocumentFile.h"
#include "history/UndoStack.h"
#include "layers/Layer.h"
#include "material/MaterialEngine.h"
#include "material/MaterialTileStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace komapaint;
using namespace komapaint::material;

class TestMaterialDocument : public QObject
{
    Q_OBJECT
private slots:
    void materialUndoRedoRetainsComposition();
    void nativeSaveOpenRetainsMaterialAndDigitalPlane();
    void reclamationRespectsUndoPinsAndDenseSaveAfterHoles();
};

void TestMaterialDocument::materialUndoRedoRetainsComposition()
{
    CanvasDocument document;
    document.setSize(QSize(96, 64));
    QCOMPARE(document.size(), QSize(96, 64));
    Layer* layer = document.layers()->layerAt(0);
    QVERIFY(layer);
    QCOMPARE(layer->tiles().setPixel({2, 2}, 0xff245b91U), SparseTileStore::WriteResult::Changed);

    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    auto definitions = defaultBrushDefinitions();
    auto it = std::find_if(definitions.begin(), definitions.end(), [](const BrushDefinition& brush) {
        return brush.id == "gouache-round";
    });
    QVERIFY(it != definitions.end());
    BrushDefinition brush = *it;
    brush.size = 5.0;
    BrushReservoir reservoir(brush.reservoirCapacity);
    QVERIFY(reservoir.load(brush.initialPaint, brush.reservoirCapacity * brush.initialLoad));

    UndoStack history;
    QVERIFY(history.beginMaterialStroke(layer));
    MaterialStroke stroke(layer->materialTiles(), brush, reservoir, engine,
                          gouacheProfile(), absorbentPaper());
    QVERIFY(stroke.begin({20.0, 20.0, {0.8, 0.2, 0.0, 0.0}}));
    QVERIFY(stroke.extend({27.0, 20.0, {0.7, 0.3, 0.0, 0.0}}));
    QVERIFY(stroke.end({31.0, 20.0, {0.7, 0.3, 0.0, 0.0}}));
    QVERIFY(history.commitStroke());

    auto painted = layer->materialTiles().batchAt({20, 20});
    QVERIFY(painted.has_value());
    QVERIFY(painted->pigmentMass() > 0.0);
    QCOMPARE(layer->tiles().pixel({2, 2}).value(), 0xff245b91U);
    QVERIFY(history.canUndo());
    history.undo();
    const auto undone = layer->materialTiles().batchAt({20, 20});
    QVERIFY(undone.has_value());
    QCOMPARE(undone->totalMass(), 0.0);
    QCOMPARE(layer->tiles().pixel({2, 2}).value(), 0xff245b91U);
    QVERIFY(history.canRedo());
    history.redo();
    const auto redone = layer->materialTiles().batchAt({20, 20});
    QVERIFY(redone.has_value());
    QVERIFY(redone->pigmentMass() > 0.0);
    QCOMPARE(redone->pigments.size(), painted->pigments.size());
    for (std::size_t i = 0; i < painted->pigments.size(); ++i) {
        QCOMPARE(redone->pigments[i].pigmentId, painted->pigments[i].pigmentId);
        QCOMPARE(redone->pigments[i].residualArgb, painted->pigments[i].residualArgb);
        QVERIFY(std::abs(redone->pigments[i].mass - painted->pigments[i].mass) < 1e-5);
    }
}

void TestMaterialDocument::nativeSaveOpenRetainsMaterialAndDigitalPlane()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("material.koma"));
    CanvasDocument document;
    document.setSize(QSize(600, 192));
    Layer* layer = document.layers()->layerAt(0);
    QCOMPARE(layer->tiles().setPixel({1, 1}, 0xff17385aU), SparseTileStore::WriteResult::Changed);

    PaintBatch first = makeResidualPaint(0xffa54b35U, 0.8, 0.3, 0.2, "gouache");
    first.drying = 0.37;
    first.settling = 0.61;
    PaintBatch second = first;
    second.drying = 0.82;
    second.settling = 0.13;
    second.pigments.front().mass -= 1e-8;
    QVERIFY(second.addPigment("PY3", 1e-8));
    QCOMPARE(layer->materialTiles().setBatch({25, 20}, first), MaterialTileStore::Result::Changed);
    QCOMPARE(layer->materialTiles().setBatch({26, 20}, second), MaterialTileStore::Result::Changed);
    // Later rows in the left tile must be serialized after all earlier rows in
    // neighboring tiles, not tile-major, so strict document-coordinate order
    // survives a multi-tile save/open round trip.
    QCOMPARE(layer->materialTiles().setBatch({25, 80}, first), MaterialTileStore::Result::Changed);
    QCOMPARE(layer->materialTiles().setBatch({300, 20}, second), MaterialTileStore::Result::Changed);
    const auto firstBefore = layer->materialTiles().cellAt({25, 20});
    const auto secondBefore = layer->materialTiles().cellAt({26, 20});
    QVERIFY(firstBefore && secondBefore);
    QVERIFY(firstBefore->compositionId != secondBefore->compositionId);

    QString error;
    QVERIFY2(DocumentFile::save(document, path, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    QVERIFY(bytes.size() > 12);
    QCOMPARE(static_cast<quint8>(bytes[9]), quint8(DocumentFile::FormatVersion));
    file.close();

    std::unique_ptr<CanvasDocument> reopened(DocumentFile::open(path, &error));
    QVERIFY2(reopened != nullptr, qPrintable(error));
    Layer* restored = reopened->layers()->layerAt(0);
    QVERIFY(restored);
    QCOMPARE(restored->tiles().pixel({1, 1}).value(), 0xff17385aU);
    const auto firstAfter = restored->materialTiles().cellAt({25, 20});
    const auto secondAfter = restored->materialTiles().cellAt({26, 20});
    const auto lowerRowAfter = restored->materialTiles().cellAt({25, 80});
    const auto neighboringTileAfter = restored->materialTiles().cellAt({300, 20});
    QVERIFY(firstAfter && secondAfter && lowerRowAfter && neighboringTileAfter);
    QVERIFY(firstAfter->compositionId != secondAfter->compositionId);
    QCOMPARE(firstAfter->compositionId, lowerRowAfter->compositionId);
    QCOMPARE(secondAfter->compositionId, neighboringTileAfter->compositionId);
    const auto firstBatch = restored->materialTiles().batchAt({25, 20});
    const auto secondBatch = restored->materialTiles().batchAt({26, 20});
    QVERIFY(firstBatch && secondBatch);
    QCOMPARE(firstBatch->pigments.size(), std::size_t(1));
    QCOMPARE(secondBatch->pigments.size(), std::size_t(2));
    QVERIFY(firstBatch->mediumId == "gouache");
    QVERIFY(std::abs(firstBatch->drying - first.drying) < 1e-6);
    QVERIFY(std::abs(firstBatch->settling - first.settling) < 1e-6);
    QVERIFY(std::abs(secondBatch->drying - second.drying) < 1e-6);
    QVERIFY(std::abs(secondBatch->settling - second.settling) < 1e-6);
    QVERIFY(secondBatch->pigments[1].pigmentId == "PY3");
    QVERIFY(!reopened->isModified());
}

void TestMaterialDocument::reclamationRespectsUndoPinsAndDenseSaveAfterHoles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("reclaimed.koma"));

    CanvasDocument document;
    document.setSize(QSize(128, 128));
    Layer* layer = document.layers()->layerAt(0);
    QVERIFY(layer);

    PaintBatch a;
    a.mediumId = "watercolour";
    QVERIFY(a.addPigment("PY3", 1.0));
    a.addWater(2.0);
    PaintBatch b;
    b.mediumId = "watercolour";
    QVERIFY(b.addPigment("PR122", 1.0));
    b.addWater(2.0);
    PaintBatch c;
    c.mediumId = "watercolour";
    QVERIFY(c.addPigment("PB15", 1.0));
    c.addWater(2.0);

    UndoStack history;
    QVERIFY(history.beginMaterialStroke(layer));
    QCOMPARE(layer->materialTiles().setBatch({10, 10}, a), MaterialTileStore::Result::Changed);
    QVERIFY(history.commitStroke());

    QVERIFY(history.beginMaterialStroke(layer));
    QCOMPARE(layer->materialTiles().setBatch({10, 10}, b), MaterialTileStore::Result::Changed);
    QVERIFY(history.commitStroke());

    QVERIFY(history.beginMaterialStroke(layer));
    QCOMPARE(layer->materialTiles().setBatch({10, 10}, c), MaterialTileStore::Result::Changed);
    QVERIFY(history.commitStroke());

    // Only composition c is live on canvas, while a and b are pinned in history.
    layer->materialTiles().reclaimUnreferenced();
    QVERIFY(layer->materialTiles().activeCompositionCount() >= 3);

    // Dense save skips dead/pinned-only compositions and writes only c cleanly.
    QString error;
    QVERIFY2(DocumentFile::save(document, path, &error), qPrintable(error));
    std::unique_ptr<CanvasDocument> reopened(DocumentFile::open(path, &error));
    QVERIFY2(reopened != nullptr, qPrintable(error));
    const auto reopenedBatch = reopened->layers()->layerAt(0)->materialTiles().batchAt({10, 10});
    QVERIFY(reopenedBatch && reopenedBatch->pigments.size() == 1);
    QCOMPARE(reopenedBatch->pigments.front().pigmentId, std::string("PB15"));

    // Undoing still restores pinned historical compositions b then a.
    history.undo();
    const auto restoredB = layer->materialTiles().batchAt({10, 10});
    QVERIFY(restoredB && restoredB->pigments.size() == 1);
    QCOMPARE(restoredB->pigments.front().pigmentId, std::string("PR122"));
    history.undo();
    const auto restoredA = layer->materialTiles().batchAt({10, 10});
    QVERIFY(restoredA && restoredA->pigments.size() == 1);
    QCOMPARE(restoredA->pigments.front().pigmentId, std::string("PY3"));

    // Clearing history unpins b and c and reclaims them.
    history.clear();
    layer->materialTiles().reclaimUnreferenced();
    QCOMPARE(layer->materialTiles().activeCompositionCount(), std::size_t(1));
}

QTEST_GUILESS_MAIN(TestMaterialDocument)
#include "TestMaterialDocument.moc"
