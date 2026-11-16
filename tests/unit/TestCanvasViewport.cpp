#include <QtTest/QtTest>
#include "app/CanvasViewport.h"
#include "app/MixerSurfaceItem.h"
#include "app/WorkspaceState.h"
#include "app/DockModel.h"
#include "app/PanelManager.h"
#include "core/ApplicationConfig.h"
#include "core/Settings.h"
#include "core/NewProjectSettings.h"
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <algorithm>
#include "canvas/CanvasDocument.h"
#include "layers/Layer.h"
#include <cmath>

using namespace komapaint;
namespace {

// Panels live inside the docking workspace, which creates them from Loaders, so
// they are found through the visual tree rather than through QObject parents.
QQuickItem* findVisual(QQuickItem* from, const QString& name)
{
    if (!from) return nullptr;
    if (from->objectName() == name) return from;
    for (auto* child : from->childItems())
        if (auto* found = findVisual(child, name)) return found;
    return nullptr;
}

}  // namespace

class TestCanvasViewport : public QObject {
    Q_OBJECT
private slots:
    void navigationAndVisibleTiles();
    void dirtyAndLayers();
    void blendModeChangesInvalidateComposite();
    void brushInputAndLayerRules();
    void traditionalMediaStrokeRendersAndUsesMaterialHistory();
    void watercolourCleanWaterAndPaintLiftingInViewport();
    void strokeFailureDiagnostics();
    void compositeCacheAccounting();
    void qmlMouseStroke();
    void actualWindowShortcuts();
};
void TestCanvasViewport::navigationAndVisibleTiles()
{
    CanvasDocument doc;
    doc.setSize(QSize(6000, 4000));
    doc.setTransparentBackground(false);
    CanvasViewport view;
    view.setWidth(512); view.setHeight(400); view.setDocument(&doc);
    view.refreshNow();
    auto first = view.compositeCount();
    QVERIFY(first > 0 && first < 20); // visible tiles only, not the whole document
    QCOMPARE(view.frameSnapshot().size(), QSize(512, 400));
    auto point = view.viewportToDocument(100, 70);
    auto outside = view.viewportToDocument(-1000, -1000);
    QVERIFY(outside.x() < point.x() && outside.y() < point.y());
    auto roundtrip = view.documentToViewport(point.x(), point.y());
    QVERIFY(std::abs(roundtrip.x() - 100) < 0.001);
    QVERIFY(std::abs(roundtrip.y() - 70) < 0.001);
    view.zoomAt(100, 70, 1.5);
    auto afterZoom = view.viewportToDocument(100, 70);
    QVERIFY(std::abs(afterZoom.x() - point.x()) < 0.001);
    QVERIFY(std::abs(afterZoom.y() - point.y()) < 0.001);
    view.refreshNow();
    auto count = view.compositeCount();
    view.panBy(5, 5); view.refreshNow();
    QVERIFY(view.compositeCount() - count < 10); // cached tiles survive navigation
    auto origin = view.documentToViewport(0, 0);
    view.panBy(10 - origin.x(), 10 - origin.y()); view.refreshNow();
    auto frame = view.frameSnapshot();
    QCOMPARE(frame.pixelColor(11, 11), QColor(Qt::white));
    QCOMPARE(frame.pixelColor(0, 0), QColor("#4a4d52"));
    view.fit(); view.refreshNow();
    QVERIFY(view.zoom() >= 6.25 && view.zoom() < 10);
    QVERIFY(!view.frameSnapshot().isNull());
}
void TestCanvasViewport::dirtyAndLayers()
{
    CanvasDocument doc;
    doc.setSize(QSize(600, 400));
    doc.setTransparentBackground(false);
    CanvasViewport view;
    view.setWidth(320); view.setHeight(240); view.setDocument(&doc);
    view.refreshNow();
    auto* lower = doc.layers()->layerAt(0);
    auto where = view.documentToViewport(300, 200);
    QCOMPARE(lower->tiles().setPixel({300, 200}, 0xffff0000U), SparseTileStore::WriteResult::Changed);
    auto count = view.compositeCount();
    view.refreshNow();
    QCOMPARE(view.compositeCount() - count, std::size_t(1));
    QCOMPARE(view.frameSnapshot().pixelColor(qRound(where.x()), qRound(where.y())), QColor(Qt::red));
    count = view.compositeCount();
    view.refreshNow(); QCOMPARE(view.compositeCount(), count);
    // The newest row is displayed below row 0, matching the panel order.
    doc.layers()->addLayer();
    auto* upper = doc.layers()->layerAt(0);
    auto* bottom = doc.layers()->layerAt(1);
    QCOMPARE(bottom->tiles().setPixel({300, 200}, 0xff0000ffU), SparseTileStore::WriteResult::Changed);
    view.refreshNow();
    QCOMPARE(view.frameSnapshot().pixelColor(qRound(where.x()), qRound(where.y())), QColor(Qt::red));
    upper->setVisible(false); view.refreshNow();
    QCOMPARE(view.frameSnapshot().pixelColor(qRound(where.x()), qRound(where.y())), QColor(Qt::blue));
    upper->setVisible(true); upper->setOpacity(0.5); view.refreshNow();
    auto mixed = view.frameSnapshot().pixelColor(qRound(where.x()), qRound(where.y()));
    QVERIFY(mixed.red() >= 127 && mixed.blue() >= 126);
    doc.layers()->moveLayer(0, 1); view.refreshNow();
    QCOMPARE(view.frameSnapshot().pixelColor(qRound(where.x()), qRound(where.y())), QColor(Qt::blue));
    doc.setBackgroundColor(Qt::green); view.refreshNow();
    auto blank = view.documentToViewport(280, 180);
    QCOMPARE(view.frameSnapshot().pixelColor(qRound(blank.x()), qRound(blank.y())), QColor(Qt::green));
    doc.setTransparentBackground(true); view.refreshNow();
    auto check1 = view.documentToViewport(280, 180);
    auto check2 = view.documentToViewport(288, 180);
    const auto frame = view.frameSnapshot();
    QVERIFY(frame.pixelColor(qRound(check1.x()), qRound(check1.y())) !=
            frame.pixelColor(qRound(check2.x()), qRound(check2.y())));
    // Move the entire first tile offscreen before editing it.
    view.panBy(-140, 0); view.refreshNow();
    // Offscreen edits stay dirty until brought into view.
    QCOMPARE(bottom->tiles().setPixel({0, 0}, 0xff00ff00U), SparseTileStore::WriteResult::Changed);
    view.refreshNow(); QVERIFY(!bottom->tiles().dirtyRegions().empty());
    auto origin = view.documentToViewport(0, 0);
    view.panBy(10 - origin.x(), 10 - origin.y()); view.refreshNow();
    QVERIFY(bottom->tiles().dirtyRegions().empty());
    QCOMPARE(view.frameSnapshot().pixelColor(10, 10), QColor(Qt::green));
}
void TestCanvasViewport::blendModeChangesInvalidateComposite()
{
    CanvasDocument doc;
    doc.setSize(QSize(64, 64));
    doc.setTransparentBackground(true);
    doc.layers()->addLayer();
    Layer* top = doc.layers()->layerAt(0);
    Layer* bottom = doc.layers()->layerAt(1);
    QCOMPARE(top->tiles().setPixel({32, 32}, 0xffff0000U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(bottom->tiles().setPixel({32, 32}, 0xff0000ffU), SparseTileStore::WriteResult::Changed);
    CanvasViewport view;
    view.setWidth(128); view.setHeight(128); view.setDocument(&doc);
    view.refreshNow();
    const QPointF point = view.documentToViewport(32, 32);
    const int x = qRound(point.x()), y = qRound(point.y());
    QCOMPARE(view.frameSnapshot().pixelColor(x, y), QColor(Qt::red));
    const auto count = view.compositeCount();
    top->setBlendMode(1); // Multiply red and blue: black.
    QVERIFY(doc.isModified());
    view.refreshNow();
    QVERIFY(view.compositeCount() > count);
    QCOMPARE(view.frameSnapshot().pixelColor(x, y), QColor(Qt::black));
    top->setBlendMode(2); // Screen red and blue: magenta.
    view.refreshNow();
    QCOMPARE(view.frameSnapshot().pixelColor(x, y), QColor(Qt::magenta));
}
void TestCanvasViewport::brushInputAndLayerRules()
{
    CanvasDocument doc; doc.setSize(QSize(600, 400));
    UndoStack history;
    CanvasViewport view; view.setHistory(&history); view.setWidth(320); view.setHeight(240); view.setDocument(&doc); view.refreshNow();
    BrushSettings brush; brush.setSize(8);
    auto* layer = doc.layers()->layerAt(0);
    auto point = view.documentToViewport(300, 200);
    QVERIFY(view.beginStroke(point.x(), point.y(), 1, &brush, Qt::red));
    view.continueStroke(point.x() + 50, point.y(), 1);
    view.endStroke(point.x() + 50, point.y(), 1);
    QVERIFY(doc.isModified());
    QVERIFY(layer->tiles().pixel({300, 200}).value() != 0);
    QVERIFY(layer->tiles().pixel({349, 200}).value() != 0);
    auto count = layer->tiles().allocatedTiles();
    layer->setLocked(true);
    QVERIFY(!view.beginStroke(point.x(), point.y(), 1, &brush, Qt::blue));
    QCOMPARE(layer->tiles().allocatedTiles(), count);
    layer->setLocked(false); layer->setVisible(false);
    QVERIFY(!view.beginStroke(point.x(), point.y(), 1, &brush, Qt::blue));
    layer->setVisible(true);
    view.zoomAt(point.x(), point.y(), 2);
    auto target = view.documentToViewport(310, 210);
    QVERIFY(view.beginStroke(target.x(), target.y(), 0.5, &brush, Qt::blue));
    view.endStroke(target.x(), target.y(), 0.5);
    QVERIFY((layer->tiles().pixel({310, 210}).value() >> 24) > 0);
    auto beforeCancel = layer->tiles().pixel({325, 220}).value();
    auto cancelPoint = view.documentToViewport(325, 220);
    QVERIFY(view.beginStroke(cancelPoint.x(), cancelPoint.y(), 1, &brush, Qt::green));
    view.cancelStroke();
    QCOMPARE(layer->tiles().pixel({325, 220}).value(), beforeCancel);
}
void TestCanvasViewport::traditionalMediaStrokeRendersAndUsesMaterialHistory()
{
    CanvasDocument document;
    document.setSize(QSize(256, 256));
    UndoStack history;
    CanvasViewport view;
    view.setWidth(320);
    view.setHeight(280);
    view.setHistory(&history);
    view.setDocument(&document);
    view.refreshNow();

    BrushSettings brush;
    brush.setTraditionalMedia(true);
    brush.setSize(18);
    const QPointF point = view.documentToViewport(100, 100);
    QVERIFY(view.beginStroke(point.x(), point.y(), 1.0, &brush, QColor(220, 35, 20)));
    view.endStroke(point.x(), point.y(), 1.0);
    QVERIFY(!history.strokeActive());
    QVERIFY(document.isModified());

    Layer* layer = document.layers()->layerAt(0);
    QVERIFY(layer);
    const auto cell = layer->materialTiles().cellAt({100, 100});
    QVERIFY(cell && !cell->empty());
    QCOMPARE(layer->tiles().pixel({100, 100}).value(), 0U);
    view.refreshNow();
    const auto sampled = view.colorAt(point.x(), point.y());
    QVERIFY(sampled.isValid());
    const QColor rendered = sampled.value<QColor>();
    QVERIFY(rendered.red() > rendered.green());
    QVERIFY(rendered.red() > rendered.blue());

    QVERIFY(history.canUndo());
    history.undo();
    const auto undone = layer->materialTiles().cellAt({100, 100});
    QVERIFY(!undone || undone->empty());
    QVERIFY(history.canRedo());
    history.redo();
    const auto redone = layer->materialTiles().cellAt({100, 100});
    QVERIFY(redone && !redone->empty());
    QCOMPARE(layer->tiles().pixel({100, 100}).value(), 0U);

    const auto materialBeforeErase = layer->materialTiles().batchAt({100, 100});
    QVERIFY(materialBeforeErase && materialBeforeErase->pigmentMass() > 0.0);
    brush.setErase(true);
    QVERIFY(view.beginStroke(point.x(), point.y(), 1.0, &brush, QColor(220, 35, 20)));
    view.endStroke(point.x(), point.y(), 1.0);
    const auto materialAfterErase = layer->materialTiles().batchAt({100, 100});
    QVERIFY(materialAfterErase && materialAfterErase->pigmentMass() < materialBeforeErase->pigmentMass());
    history.undo();
    const auto materialAfterUndo = layer->materialTiles().batchAt({100, 100});
    QVERIFY(materialAfterUndo);
    QVERIFY(std::abs(materialAfterUndo->pigmentMass() - materialBeforeErase->pigmentMass()) < 1e-6);

    BrushSettings mixerBrush;
    mixerBrush.setTraditionalMedia(true);
    QVariantList components;
    components.push_back(QVariantMap{{QStringLiteral("pigmentId"), QStringLiteral("PY3")},
                                     {QStringLiteral("mass"), 0.25}});
    components.push_back(QVariantMap{{QStringLiteral("pigmentId"), QStringLiteral("PB15")},
                                     {QStringLiteral("mass"), 0.75}});
    const QVariantMap mixture{{QStringLiteral("mediumId"), QStringLiteral("watercolour")},
                              {QStringLiteral("pigments"), components},
                              {QStringLiteral("water"), 2.0},
                              {QStringLiteral("binder"), 0.12},
                              {QStringLiteral("filler"), 0.0},
                              {QStringLiteral("drying"), 0.0},
                              {QStringLiteral("settling"), 0.0}};
    QVERIFY(mixerBrush.loadMaterialMixture(mixture));
    QCOMPARE(mixerBrush.materialMixture(), mixture);
    const QPointF mixturePoint = view.documentToViewport(160, 160);
    QVERIFY(view.beginStroke(mixturePoint.x(), mixturePoint.y(), 1.0, &mixerBrush, QColor(Qt::green)));
    view.endStroke(mixturePoint.x(), mixturePoint.y(), 1.0);
    const auto loadedPaint = layer->materialTiles().batchAt({160, 160});
    QVERIFY(loadedPaint);
    QCOMPARE(loadedPaint->mediumId, std::string("watercolour"));
    QVERIFY(loadedPaint->pigments.size() == 2);
    const auto namedA = std::find_if(loadedPaint->pigments.begin(), loadedPaint->pigments.end(),
        [](const auto& pigment) { return pigment.pigmentId == "PY3"; });
    const auto namedB = std::find_if(loadedPaint->pigments.begin(), loadedPaint->pigments.end(),
        [](const auto& pigment) { return pigment.pigmentId == "PB15"; });
    QVERIFY(namedA != loadedPaint->pigments.end() && namedB != loadedPaint->pigments.end());
    if (namedA != loadedPaint->pigments.end() && namedB != loadedPaint->pigments.end())
        QVERIFY(std::abs(namedA->mass / namedB->mass - 1.0 / 3.0) < 1e-5);

    // By default, persistentReservoir is false so each new stroke starts with
    // a clean, freshly reloaded mixture rather than carrying picked-up paint.
    QVERIFY(!mixerBrush.persistentReservoir());
    QVariantList prComponents;
    prComponents.push_back(QVariantMap{{QStringLiteral("pigmentId"), QStringLiteral("PR122")},
                                       {QStringLiteral("mass"), 1.0}});
    const QVariantMap prMixture{{QStringLiteral("mediumId"), QStringLiteral("watercolour")},
                                {QStringLiteral("pigments"), prComponents},
                                {QStringLiteral("water"), 2.0},
                                {QStringLiteral("binder"), 0.12},
                                {QStringLiteral("filler"), 0.0}};
    QVERIFY(mixerBrush.loadMaterialMixture(prMixture));
    const QPointF p1 = view.documentToViewport(160, 160);
    QVERIFY(view.beginStroke(p1.x(), p1.y(), 1.0, &mixerBrush, QColor(Qt::magenta)));
    view.endStroke(p1.x(), p1.y(), 1.0);
    const QPointF p2 = view.documentToViewport(240, 240);
    QVERIFY(view.beginStroke(p2.x(), p2.y(), 1.0, &mixerBrush, QColor(Qt::magenta)));
    view.endStroke(p2.x(), p2.y(), 1.0);
    const auto cleanSecondStroke = layer->materialTiles().batchAt({240, 240});
    QVERIFY(cleanSecondStroke && cleanSecondStroke->pigments.size() == 1);
    QCOMPARE(cleanSecondStroke->pigments.front().pigmentId, std::string("PR122"));

    // Gouache Round settings (concentration, water, paint body, filler) control
    // actual stored PaintBatch composition and cover opaquely without touching Digital tiles.
    BrushSettings gouacheSettings;
    gouacheSettings.setTraditionalMedia(true);
    gouacheSettings.setSize(18);
    gouacheSettings.setHardness(72);
    gouacheSettings.setPaintBody(85);
    gouacheSettings.setWaterAmount(15);
    gouacheSettings.setPigmentConcentration(90);
    const QPointF gStart = view.documentToViewport(40, 40);
    const QPointF gEnd = view.documentToViewport(80, 40);
    QVERIFY(view.beginStroke(gStart.x(), gStart.y(), 1.0, &gouacheSettings, QColor(215, 165, 25)));
    view.continueStroke(gEnd.x(), gEnd.y(), 1.0);
    view.endStroke(gEnd.x(), gEnd.y(), 1.0);
    const auto gouacheCell = layer->materialTiles().batchAt({60, 40});
    QVERIFY(gouacheCell && gouacheCell->mediumId == "gouache");
    QVERIFY(gouacheCell->filler > 0.0);
    QVERIFY(gouacheCell->pigmentMass() > gouacheCell->water);

    gouacheSettings.setWaterAmount(95);
    gouacheSettings.setPaintBody(10);
    const QPointF wPt = view.documentToViewport(40, 80);
    QVERIFY(view.beginStroke(wPt.x(), wPt.y(), 1.0, &gouacheSettings, QColor(215, 165, 25)));
    view.endStroke(wPt.x(), wPt.y(), 1.0);
    const auto dilutedCell = layer->materialTiles().batchAt({40, 80});
    QVERIFY(dilutedCell && dilutedCell->water / dilutedCell->totalMass() > gouacheCell->water / gouacheCell->totalMass());
}

void TestCanvasViewport::strokeFailureDiagnostics()
{
    CanvasDocument doc;
    doc.setSize(QSize(600, 400));
    doc.setModified(false); // setup is the saved baseline for rollback
    UndoStack history;
    CanvasViewport view;
    view.setWidth(500); view.setHeight(300);
    view.setHistory(&history); view.setDocument(&doc); view.refreshNow();
    BrushSettings brush;
    brush.setSize(8);
    QSignalSpy errors(&view, &CanvasViewport::strokeError);
    const auto first = view.documentToViewport(100, 100);
    const auto second = view.documentToViewport(300, 100);
    const auto originalFrame = view.frameSnapshot();
    auto budget = doc.layers()->documentBudget();
    QVERIFY(budget->setLimit(budget->usedBytes() + 2 * doc.layers()->layerAt(0)->tiles().tileBytes()));
    QVERIFY(view.beginStroke(first.x(), first.y(), 1, &brush, Qt::red));
    view.refreshNow(); // acknowledge dirties and populate cache during the stroke
    QVERIFY(view.frameSnapshot().pixelColor(qRound(first.x()), qRound(first.y())) !=
            originalFrame.pixelColor(qRound(first.x()), qRound(first.y())));
    view.continueStroke(second.x(), second.y(), 1);
    QCOMPARE(errors.size(), 1);
    const auto message = errors.takeFirst().at(0).toString();
    QVERIFY2(message.contains(QStringLiteral("available memory")), qPrintable(message));
    QVERIFY(!history.canUndo());
    QVERIFY(message.contains(QStringLiteral("rolled back")));
    auto& store = doc.layers()->layerAt(0)->tiles();
    QCOMPARE(store.pixel({100, 100}).value(), 0U);
    QCOMPARE(store.pixel({300, 100}).value(), 0U);
    QCOMPARE(store.allocatedTiles(), std::size_t(0));
    QVERIFY(!doc.isModified());
    view.refreshNow();
    QCOMPARE(view.frameSnapshot().pixelColor(qRound(first.x()), qRound(first.y())),
             originalFrame.pixelColor(qRound(first.x()), qRound(first.y())));
}

void TestCanvasViewport::compositeCacheAccounting()
{
    CanvasDocument doc;
    doc.setSize(QSize(600, 400));
    auto budget = doc.layers()->documentBudget();
    {
        CanvasViewport view;
        view.setWidth(320); view.setHeight(240); view.setDocument(&doc); view.refreshNow();
        QVERIFY(budget->cacheBytes() > 0);
        const auto before = budget->cacheBytes();
        QVERIFY(budget->usedBytes() >= before);
        view.setDocument(nullptr);
        QCOMPARE(budget->cacheBytes(), size_t(0));
        view.setDocument(&doc); view.refreshNow();
        QVERIFY(budget->cacheBytes() > 0);
    }
    QCOMPARE(budget->cacheBytes(), size_t(0));
}

void TestCanvasViewport::qmlMouseStroke()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
    qmlRegisterType<MixerSurfaceItem>("KomaPaint", 1, 0, "MixerSurfaceItem");
    WorkspaceState workspace;
    QVERIFY(workspace.newDocument(256, 256, false, QColor(Qt::white)));
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    engine.rootContext()->setContextProperty(QStringLiteral("Workspace"), &workspace);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/components/CanvasWorkspace.qml")));
    if (component.isError()) qWarning() << component.errors();
    QObject* root = component.create();
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* item = qobject_cast<QQuickItem*>(root);
    QVERIFY(item);
    QQuickWindow window; window.resize(320, 240);
    item->setParentItem(window.contentItem()); item->setWidth(320); item->setHeight(240);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* canvas = root->findChild<CanvasViewport*>(QStringLiteral("canvasViewport"));
    QVERIFY(canvas);
    QTest::qWait(30); canvas->refreshNow();
    auto point = canvas->viewportToDocument(100, 100);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseMove(&window, QPoint(150, 100), 20);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, 100));
    QCoreApplication::processEvents();
    QVERIFY(workspace.document()->isModified());
    auto& tiles = workspace.layers()->layerAt(0)->tiles();
    QVERIFY(tiles.pixel({static_cast<int>(point.x()), static_cast<int>(point.y())}).value() != 0);
    QVERIFY(tiles.pixel({static_cast<int>(point.x()) + 25, static_cast<int>(point.y())}).value() != 0);
    QVERIFY(tiles.pixel({static_cast<int>(point.x()) + 49, static_cast<int>(point.y())}).value() != 0);
    const auto after = tiles.pixel({static_cast<int>(point.x()) + 25, static_cast<int>(point.y())}).value();
    QVERIFY(workspace.history()->canUndo());
    workspace.history()->undo(); canvas->refreshNow();
    QCOMPARE(tiles.pixel({static_cast<int>(point.x()) + 25, static_cast<int>(point.y())}).value(), 0U);
    workspace.history()->redo(); canvas->refreshNow();
    QCOMPARE(tiles.pixel({static_cast<int>(point.x()) + 25, static_cast<int>(point.y())}).value(), after);
    delete root;
}

void TestCanvasViewport::actualWindowShortcuts()
{
    WorkspaceState workspace;
    // Nothing is open at start-up now, and the docks (including the canvas)
    // only become visible for an open project. Create the document the way the
    // New Project dialog does, before the window is built.
    QVERIFY(workspace.newDocument(1200, 900, false, QColor(Qt::white)));
    ApplicationConfig config;
    Settings settings(QStringLiteral("KomaPaintTests"), QStringLiteral("UndoWindow"));
    PanelManager panels;
    // The title field belongs to the Properties tab, which shares a dock group
    // with Color and is not the active tab in the default layout.
    panels.dockModel()->setActive(QStringLiteral("properties"));
    NewProjectSettings newProject;
    QQmlEngine engine; engine.addImportPath(QStringLiteral("qrc:/"));
    engine.rootContext()->setContextProperty(QStringLiteral("Workspace"), &workspace);
    engine.rootContext()->setContextProperty(QStringLiteral("Settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &config);
    engine.rootContext()->setContextProperty(QStringLiteral("Panels"), &panels);
    engine.rootContext()->setContextProperty(QStringLiteral("Dock"), panels.dockModel());
    engine.rootContext()->setContextProperty(QStringLiteral("NewProject"), &newProject);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/workspaces/MainWindow.qml")));
    QObject* object = component.create();
    QVERIFY2(object, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(object); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* canvas = object->findChild<CanvasViewport*>(QStringLiteral("canvasViewport")); QVERIFY(canvas);
    QTRY_VERIFY(canvas->document() == static_cast<QObject*>(workspace.document()));
    QTest::qWait(30); canvas->refreshNow();
    const auto location = canvas->mapToScene(QPointF(100, 100)).toPoint();
    const auto docPoint = canvas->viewportToDocument(100, 100);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, location);
    QTest::mouseMove(window, location + QPoint(25, 0), 10);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, location + QPoint(25, 0));
    QCoreApplication::processEvents();
    auto& tiles = workspace.layers()->layerAt(0)->tiles();
    const TilePoint pixel{static_cast<int>(docPoint.x()) + 10, static_cast<int>(docPoint.y())};
    const auto painted = tiles.pixel(pixel).value(); QVERIFY(painted != 0);
    canvas->refreshNow();
    const auto visiblePaint = canvas->frameSnapshot().pixelColor(110, 100);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(tiles.pixel(pixel).value(), 0U);
    canvas->refreshNow();
    QVERIFY(canvas->frameSnapshot().pixelColor(110, 100) != visiblePaint);
    QTest::keyClick(window, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(tiles.pixel(pixel).value(), painted);
    canvas->refreshNow();
    QCOMPARE(canvas->frameSnapshot().pixelColor(110, 100), visiblePaint);
    const auto secondLocation = location + QPoint(0, 30);
    const TilePoint secondPixel{pixel.x, pixel.y + 30};
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, secondLocation);
    QTest::mouseMove(window, secondLocation + QPoint(25, 0), 10);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, secondLocation + QPoint(25, 0));
    QCoreApplication::processEvents();
    QVERIFY(tiles.pixel(secondPixel).value() != 0);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(tiles.pixel(secondPixel).value(), 0U);
    QCOMPARE(tiles.pixel(pixel).value(), painted);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(tiles.pixel(pixel).value(), 0U);
    QTest::keyClick(window, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(tiles.pixel(pixel).value(), painted);
    QTest::keyClick(window, Qt::Key_Y, Qt::ControlModifier);
    QVERIFY(tiles.pixel(secondPixel).value() != 0);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    const auto thirdLocation = location + QPoint(0, 60);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, thirdLocation);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, thirdLocation);
    QCoreApplication::processEvents();
    QVERIFY(!workspace.history()->canRedo());
    QCOMPARE(tiles.pixel(secondPixel).value(), 0U);
    auto* title = findVisual(window->contentItem(), QStringLiteral("documentTitleField")); QVERIFY(title);
    title->forceActiveFocus(); QTest::qWait(20);
    QTest::keyClick(window, Qt::Key_X);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(tiles.pixel(pixel).value(), painted); // text undo must not undo artwork
    delete object;
}

void TestCanvasViewport::watercolourCleanWaterAndPaintLiftingInViewport()
{
    CanvasDocument doc;
    doc.setSize(QSize(256, 256));
    doc.setSurfaceId(QStringLiteral("cold-press"));
    UndoStack history;
    BrushSettings brush;
    brush.setTraditionalMedia(true);
    brush.setMediumId(QStringLiteral("watercolour"));
    brush.setMaterialPresetId(QStringLiteral("watercolour-round"));
    brush.setSize(20);
    brush.setWaterAmount(78);
    brush.setPigmentConcentration(50);

    CanvasViewport viewport;
    viewport.setWidth(256);
    viewport.setHeight(256);
    viewport.setHistory(&history);
    viewport.setDocument(&doc);
    viewport.refreshNow();

    const QPointF p0 = viewport.documentToViewport(60.0, 128.0);
    const QPointF p1 = viewport.documentToViewport(128.0, 128.0);
    const QPointF p2 = viewport.documentToViewport(196.0, 128.0);
    const QColor wcColor(QStringLiteral("#1f5da8"));
    QVERIFY(viewport.beginStroke(p0.x(), p0.y(), 0.85, &brush, wcColor));
    viewport.continueStroke(p1.x(), p1.y(), 0.85);
    viewport.continueStroke(p2.x(), p2.y(), 0.85);
    viewport.endStroke(p2.x(), p2.y(), 0.85);

    Layer* layer = doc.layers()->layerAt(0);
    QVERIFY(layer);
    const auto paintedBatch = layer->materialTiles().batchAt({128, 128});
    QVERIFY(paintedBatch && paintedBatch->mediumId == "watercolour");
    const double initialPigment = paintedBatch->pigmentMass();
    QVERIFY(initialPigment > 0.0);

    // Clean Water never introduces primary color pigment even when brush color is red.
    brush.setMaterialPresetId(QStringLiteral("clean-water"));
    brush.setSize(20);
    const QPointF w0 = viewport.documentToViewport(60.0, 60.0);
    QVERIFY(viewport.beginStroke(w0.x(), w0.y(), 0.8, &brush, QColor(QStringLiteral("#e6192e"))));
    viewport.endStroke(w0.x(), w0.y(), 0.8);
    const auto cleanSpot = layer->materialTiles().batchAt({60, 60});
    QVERIFY(cleanSpot && cleanSpot->water > 0.0);
    QCOMPARE(cleanSpot->pigmentMass(), 0.0);

    // Paint Lifting reduces existing pigment mass cleanly without erasing via digital plane.
    brush.setMaterialPresetId(QStringLiteral("paint-lifting"));
    brush.setPickup(72);
    const QPointF l0 = viewport.documentToViewport(128.0, 118.0);
    const QPointF l1 = viewport.documentToViewport(128.0, 138.0);
    QVERIFY(viewport.beginStroke(l0.x(), l0.y(), 0.85, &brush, wcColor));
    viewport.continueStroke(l1.x(), l1.y(), 0.85);
    viewport.endStroke(l1.x(), l1.y(), 0.85);
    const auto liftedSpot = layer->materialTiles().batchAt({128, 128});
    QVERIFY(liftedSpot && liftedSpot->pigmentMass() < initialPigment);
    QCOMPARE(layer->tiles().allocatedTiles(), std::size_t(0));
}

QTEST_MAIN(TestCanvasViewport)
#include "TestCanvasViewport.moc"
