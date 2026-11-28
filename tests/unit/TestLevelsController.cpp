// Levels through the real controller: document preview/apply/cancel, selection
// clipping, undo/redo text, and the in-memory brush-tip image target.
#include <QtTest/QtTest>

#include "app/LevelsController.h"
#include "canvas/CanvasDocument.h"
#include "history/UndoStack.h"
#include "layers/Layer.h"
#include "layers/LayerModel.h"
#include "region/SelectionMask.h"

#include <QBuffer>
#include <QImage>

using namespace komapaint;

class TestLevelsController : public QObject
{
    Q_OBJECT
private slots:
    void documentPreviewCancelAndApply();
    void selectionLimitsTheEdit();
    void lockedAndEmptyLayersAreRejected();
    void imageTargetKeepsAlphaAndDoesNotTouchTheDocument();
    void gammaAndMidpointStayInStep();
};

namespace {

Layer* firstLayer(CanvasDocument& document)
{
    return document.layers()->layerAt(0);
}

QByteArray pngBytes(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

}  // namespace

void TestLevelsController::documentPreviewCancelAndApply()
{
    CanvasDocument document;
    auto* layer = firstLayer(document);
    QVERIFY(layer);
    QCOMPARE(layer->tiles().setPixel({0, 0}, 0xff404040U), SparseTileStore::WriteResult::Changed);
    UndoStack history;
    LevelsController levels;

    const bool wasModified = document.isModified();
    QVERIFY(levels.beginDocument(&document, layer, &history));
    QCOMPARE(levels.targetKind(), QStringLiteral("document"));
    QVERIFY(levels.histogram().size() == 256);
    QCOMPARE(document.isModified(), wasModified);

    levels.setInputWhite(128);
    levels.previewNow();
    QCOMPARE(layer->tiles().pixel({0, 0}).value(), 0xff808080U);
    QVERIFY(document.isModified());

    levels.setShowOriginal(true);
    QCOMPARE(layer->tiles().pixel({0, 0}).value(), 0xff404040U);
    levels.setShowOriginal(false);
    QCOMPARE(layer->tiles().pixel({0, 0}).value(), 0xff808080U);

    levels.cancel();
    QCOMPARE(layer->tiles().pixel({0, 0}).value(), 0xff404040U);
    QCOMPARE(document.isModified(), wasModified);
    QVERIFY(!history.canUndo());

    QVERIFY(levels.beginDocument(&document, layer, &history));
    levels.setInputWhite(128);
    levels.previewNow();
    QVERIFY(levels.apply());
    QCOMPARE(layer->tiles().pixel({0, 0}).value(), 0xff808080U);
    QVERIFY(document.isModified());
    QVERIFY(history.canUndo());
    QCOMPARE(history.undoText(), QStringLiteral("Levels"));

    history.undo();
    QCOMPARE(layer->tiles().pixel({0, 0}).value(), 0xff404040U);
    history.redo();
    QCOMPARE(layer->tiles().pixel({0, 0}).value(), 0xff808080U);
    QCOMPARE(history.redoText(), QString());
}

void TestLevelsController::selectionLimitsTheEdit()
{
    CanvasDocument document;
    auto* layer = firstLayer(document);
    QVERIFY(layer);
    QCOMPARE(layer->tiles().setPixel({2, 2}, 0xff404040U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(layer->tiles().setPixel({30, 30}, 0xff404040U), SparseTileStore::WriteResult::Changed);
    document.selection().applyShape(region::SelectionShape::Rectangle, 0, 0, 8, 8,
                                    false, 0, region::SelectionOp::Replace);
    QVERIFY(document.hasSelection());

    UndoStack history;
    LevelsController levels;
    QVERIFY(levels.beginDocument(&document, layer, &history));
    QVERIFY(levels.scopeText().contains(QStringLiteral("Selected area")));
    levels.setInputWhite(128);
    levels.previewNow();
    // Inside the selection: adjusted. Outside: untouched, including the same tile.
    QCOMPARE(layer->tiles().pixel({2, 2}).value(), 0xff808080U);
    QCOMPARE(layer->tiles().pixel({30, 30}).value(), 0xff404040U);
    QVERIFY(levels.apply());
    QCOMPARE(layer->tiles().pixel({2, 2}).value(), 0xff808080U);
    QCOMPARE(layer->tiles().pixel({30, 30}).value(), 0xff404040U);
}

void TestLevelsController::lockedAndEmptyLayersAreRejected()
{
    CanvasDocument document;
    auto* layer = firstLayer(document);
    QVERIFY(layer);
    UndoStack history;
    LevelsController levels;

    QVERIFY(!levels.beginDocument(&document, layer, &history));
    QVERIFY(!levels.error().isEmpty());
    QVERIFY(!levels.isActive());

    QCOMPARE(layer->tiles().setPixel({0, 0}, 0xff404040U), SparseTileStore::WriteResult::Changed);
    layer->setLocked(true);
    QVERIFY(!levels.beginDocument(&document, layer, &history));
    QVERIFY(levels.error().contains(QStringLiteral("locked"), Qt::CaseInsensitive));

    layer->setLocked(false);
    QVERIFY(levels.beginDocument(&document, layer, &history));
    levels.cancel();
}

void TestLevelsController::imageTargetKeepsAlphaAndDoesNotTouchTheDocument()
{
    QImage source(2, 1, QImage::Format_RGBA8888);
    source.setPixel(0, 0, qRgba(64, 64, 64, 255));
    source.setPixel(1, 0, qRgba(64, 64, 64, 128));

    LevelsController levels;
    QVERIFY(levels.beginImage(pngBytes(source), QStringLiteral("Test tip")));
    QCOMPARE(levels.targetKind(), QStringLiteral("image"));
    QVERIFY(levels.hasImagePreview());
    levels.setInputWhite(128);
    levels.previewNow();
    QVERIFY(levels.apply());
    QVERIFY(!levels.resultPngBase64().isEmpty());
    QCOMPARE(levels.resultName(), QStringLiteral("Test tip"));

    const QImage result = QImage::fromData(QByteArray::fromBase64(levels.resultPngBase64().toLatin1()), "PNG");
    QVERIFY(!result.isNull());
    const auto opaque = result.pixelColor(0, 0);
    const auto translucent = result.pixelColor(1, 0);
    QCOMPARE(opaque.red(), 128);
    QCOMPARE(opaque.alpha(), 255);
    QCOMPARE(translucent.red(), 128);
    QCOMPARE(translucent.alpha(), 128);

    LevelsController cancelled;
    QVERIFY(cancelled.beginImage(pngBytes(source), QString()));
    cancelled.setInputWhite(128);
    cancelled.previewNow();
    cancelled.cancel();
    QVERIFY(cancelled.resultPngBase64().isEmpty());
}

void TestLevelsController::gammaAndMidpointStayInStep()
{
    CanvasDocument document;
    auto* layer = firstLayer(document);
    QCOMPARE(layer->tiles().setPixel({0, 0}, 0xff808080U), SparseTileStore::WriteResult::Changed);
    UndoStack history;
    LevelsController levels;
    QVERIFY(levels.beginDocument(&document, layer, &history));

    levels.setMidpointPercent(25);
    QVERIFY(qAbs(levels.gamma() - 2.0) < 0.02);
    QCOMPARE(levels.gammaHundredths(), 200);
    levels.setGamma(0.5);
    QCOMPARE(levels.midpointPercent(), 71);
    levels.cancel();
}

QTEST_MAIN(TestLevelsController)
#include "TestLevelsController.moc"
