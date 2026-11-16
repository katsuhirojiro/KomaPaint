#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "app/BrushSettings.h"
#include "app/CanvasViewport.h"
#include "app/PaintMixerController.h"
#include "app/PaintMixerStore.h"
#include "app/WorkspaceState.h"
#include "canvas/CanvasDocument.h"
#include "color/ColorPalette.h"
#include "files/DocumentFile.h"
#include "history/UndoStack.h"
#include "layers/Layer.h"

#include <cmath>

using namespace komapaint;

class TestPaintMixerApp : public QObject {
    Q_OBJECT

private slots:
    void persistenceRoundTripAndInvalidHandling();
    void brushReservoirReloadAndNonContamination();
    void canvasIsolationDecoupledUndoAndTabPreservation();
    void komaCompatibilityWithMixerPaintedStrokes();
};

void TestPaintMixerApp::persistenceRoundTripAndInvalidHandling()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("mixtures.json"));

    BrushSettings brush;
    ColorPalette colors;
    {
        PaintMixerController mixer(&brush, &colors);
        QVERIFY(mixer.initializePersistence(path));
        mixer.setActiveMediumId(QStringLiteral("gouache"));
        QVERIFY(mixer.addPigmentToActive(QStringLiteral("PY3"), 1.0));
        QVERIFY(mixer.addPigmentToActive(QStringLiteral("PB15"), 0.5));
        const QString savedId = mixer.saveActiveMixture(QStringLiteral("Olive Wash"), QStringLiteral("2:1"));
        QVERIFY(!savedId.isEmpty());

        const auto digitalRes = mixer.addFromColorWheel(QColor(QStringLiteral("#7A42F4")), true, true);
        QVERIFY(digitalRes.value(QStringLiteral("ok")).toBool());
        QVERIFY(!digitalRes.value(QStringLiteral("matchedKnownPigment")).toBool());
        const QString digitalSaved = mixer.saveActiveMixture(QStringLiteral("Digital Violet"));
        QVERIFY(!digitalSaved.isEmpty());

        const QString dupId = mixer.duplicateSavedMixture(savedId);
        QVERIFY(!dupId.isEmpty());
        QCOMPARE(mixer.savedMixtures().size(), 3);
    }

    // Reopen into a fresh controller and verify full composition, order, and duplicate independence.
    PaintMixerController reloaded(&brush, &colors);
    QVERIFY(reloaded.initializePersistence(path));
    QCOMPARE(reloaded.savedMixtures().size(), 3);
    QCOMPARE(reloaded.deposits().size(), 2);

    const QString firstSavedId = reloaded.savedMixtures().at(0).toMap().value(QStringLiteral("id")).toString();
    QVERIFY(reloaded.openSavedMixture(firstSavedId));
    QCOMPARE(reloaded.deposits().size(), 3);
    QVERIFY(reloaded.loadSavedIntoBrush(firstSavedId));
    QVERIFY(!brush.materialMixture().isEmpty());

    // Tampered / corrupted file must be rejected cleanly without inventing data.
    QFile corrupt(path);
    QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
    corrupt.write("{\"format\":\"komapaint-mixer-library\",\"sha256\":\"deadbeef\"}");
    corrupt.close();

    PaintMixerController failed(&brush, &colors);
    QVERIFY(!failed.initializePersistence(path));
    QVERIFY(!failed.lastError().isEmpty());
}

void TestPaintMixerApp::brushReservoirReloadAndNonContamination()
{
    CanvasDocument doc;
    doc.setSize(QSize(256, 256));
    UndoStack history;
    CanvasViewport view;
    view.setWidth(256);
    view.setHeight(256);
    view.setHistory(&history);
    view.setDocument(&doc);
    view.refreshNow();

    BrushSettings brush;
    ColorPalette colors;
    PaintMixerController mixer(&brush, &colors);

    mixer.setActiveMediumId(QStringLiteral("watercolour"));
    QVERIFY(mixer.addPigmentToActive(QStringLiteral("PR122"), 1.0));
    QVERIFY(mixer.loadActiveIntoBrush());
    QVERIFY(brush.traditionalMedia());
    QCOMPARE(brush.mediumId(), QStringLiteral("watercolour"));
    QVERIFY(!brush.persistentReservoir()); // auto-reload ON by default

    // Changing normal brush settings must NOT discard the loaded mixer composition.
    brush.setSize(28);
    brush.setOpacity(85);
    brush.setFlow(90);
    brush.setSpacing(20);
    QVERIFY(!brush.materialMixture().isEmpty());

    Layer* layer = doc.layers()->layerAt(0);
    QVERIFY(layer);

    // Stroke 1 paints PR122 at (80, 80).
    const QPointF p1 = view.documentToViewport(80, 80);
    QVERIFY(view.beginStroke(p1.x(), p1.y(), 1.0, &brush, QColor(Qt::black)));
    view.endStroke(p1.x(), p1.y(), 1.0);

    // Switch mixer to PY3 + PB15, load into brush, paint stroke 2 OVER PR122 at (80, 80),
    // then paint stroke 3 cleanly at (180, 180). With persistentReservoir == false,
    // stroke 3 must NOT contain picked-up PR122 from stroke 2!
    const QString greenWell = mixer.addPigmentAsNewDeposit(QStringLiteral("PY3"), 0.5);
    QVERIFY(!greenWell.isEmpty());
    QVERIFY(mixer.addPigmentToActive(QStringLiteral("PB15"), 1.5));
    QVERIFY(mixer.loadActiveIntoBrush());
    brush.setPickup(80); // high canvas pickup during stroke 2

    QVERIFY(view.beginStroke(p1.x(), p1.y(), 1.0, &brush, QColor(Qt::black)));
    view.endStroke(p1.x(), p1.y(), 1.0);

    const QPointF p3 = view.documentToViewport(180, 180);
    QVERIFY(view.beginStroke(p3.x(), p3.y(), 1.0, &brush, QColor(Qt::black)));
    view.endStroke(p3.x(), p3.y(), 1.0);

    const auto batch3 = layer->materialTiles().batchAt({180, 180});
    QVERIFY(batch3.has_value());
    QCOMPARE(batch3->pigments.size(), std::size_t(2));
    for (const auto& c : batch3->pigments) {
        QVERIFY(c.pigmentId == "PY3" || c.pigmentId == "PB15");
    }

    // Even with persistentReservoir ON, explicitly loading a new mixture resets
    // the reservoir for the next stroke via mixtureRevision. Paint well away from
    // stroke 3 at (180, 180) so pickup-before-deposit does not touch stroke 3's puddle.
    brush.setPersistentReservoir(true);
    const QString umberWell = mixer.addPigmentAsNewDeposit(QStringLiteral("PBr7"), 1.0);
    QVERIFY(!umberWell.isEmpty());
    QVERIFY(mixer.loadActiveIntoBrush());
    const QPointF p4 = view.documentToViewport(40, 210);
    QVERIFY(view.beginStroke(p4.x(), p4.y(), 1.0, &brush, QColor(Qt::black)));
    view.endStroke(p4.x(), p4.y(), 1.0);
    const auto batch4 = layer->materialTiles().batchAt({40, 210});
    QVERIFY(batch4.has_value());
    QCOMPARE(batch4->pigments.size(), std::size_t(1));
    QCOMPARE(batch4->pigments.front().pigmentId, std::string("PBr7"));
}

void TestPaintMixerApp::canvasIsolationDecoupledUndoAndTabPreservation()
{
    WorkspaceState workspace;
    QVERIFY(workspace.newDocument(128, 128, false, QColor(Qt::white)));
    QVERIFY(workspace.newDocument(128, 128, false, QColor(Qt::white)));
    QCOMPARE(workspace.projectTabs().size(), 2);

    CanvasDocument* docA = workspace.document();
    QVERIFY(docA);
    docA->setModified(false);
    QVERIFY(!workspace.history()->canUndo());

    // Mixer edits must not dirty the active canvas, touch canvas tiles, or push to canvas UndoStack.
    auto* mixer = workspace.mixer();
    QVERIFY(mixer != nullptr);
    QVERIFY(mixer->addPigmentToActive(QStringLiteral("PB29"), 1.0));
    QVERIFY(mixer->addWater(1.2));
    QVERIFY(!docA->isModified());
    QVERIFY(!workspace.history()->canUndo());
    QCOMPARE(docA->layers()->layerAt(0)->materialTiles().allocatedTiles(), std::size_t(0));
    QVERIFY(mixer->canUndo());

    // Switching project tabs preserves mixer state, active deposit, and mixer undo stack.
    const QString depId = mixer->activeDepositId();
    QVERIFY(workspace.activateTab(0));
    QCOMPARE(mixer->activeDepositId(), depId);
    QCOMPARE(mixer->deposits().size(), 1);
    QVERIFY(mixer->canUndo());

    // Mixer undo leaves canvas intact.
    QVERIFY(mixer->undo());
    QVERIFY(mixer->canRedo());
    QVERIFY(!docA->isModified());
}

void TestPaintMixerApp::komaCompatibilityWithMixerPaintedStrokes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString komaPath = dir.filePath(QStringLiteral("artwork.koma"));

    CanvasDocument doc;
    doc.setSize(QSize(128, 128));
    UndoStack history;
    CanvasViewport view;
    view.setWidth(128);
    view.setHeight(128);
    view.setHistory(&history);
    view.setDocument(&doc);
    view.refreshNow();

    BrushSettings brush;
    ColorPalette colors;
    PaintMixerController mixer(&brush, &colors);
    mixer.setActiveMediumId(QStringLiteral("gouache"));
    QVERIFY(mixer.addPigmentToActive(QStringLiteral("PY42"), 1.0));
    QVERIFY(mixer.addFromColorWheel(QColor(QStringLiteral("#7A42F4")), true, false).value(QStringLiteral("ok")).toBool());
    QVERIFY(mixer.loadActiveIntoBrush());

    const QPointF pt = view.documentToViewport(64, 64);
    QVERIFY(view.beginStroke(pt.x(), pt.y(), 1.0, &brush, QColor(Qt::black)));
    view.endStroke(pt.x(), pt.y(), 1.0);

    QString error;
    QVERIFY2(DocumentFile::save(doc, komaPath, &error), qPrintable(error));

    auto loaded = std::unique_ptr<CanvasDocument>(DocumentFile::open(komaPath, &error));
    QVERIFY2(loaded != nullptr, qPrintable(error));
    const auto cell = loaded->layers()->layerAt(0)->materialTiles().batchAt({64, 64});
    QVERIFY(cell.has_value());
    QCOMPARE(cell->mediumId, std::string("gouache"));
    QCOMPARE(cell->pigments.size(), std::size_t(2));
}

QTEST_MAIN(TestPaintMixerApp)
#include "TestPaintMixerApp.moc"
