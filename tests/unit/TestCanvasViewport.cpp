#include <QtTest/QtTest>
#include "app/CanvasViewport.h"
#include "app/WorkspaceState.h"
#include "app/PanelManager.h"
#include "core/ApplicationConfig.h"
#include "core/Settings.h"
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include "canvas/CanvasDocument.h"
#include "layers/Layer.h"
#include <cmath>

using namespace komapaint;
class TestCanvasViewport : public QObject {
    Q_OBJECT
private slots:
    void navigationAndVisibleTiles();
    void dirtyAndLayers();
    void brushInputAndLayerRules();
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
}
void TestCanvasViewport::qmlMouseStroke()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
    WorkspaceState workspace;
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
    ApplicationConfig config;
    Settings settings(QStringLiteral("KomaPaintTests"), QStringLiteral("UndoWindow"));
    PanelManager panels;
    QQmlEngine engine; engine.addImportPath(QStringLiteral("qrc:/"));
    engine.rootContext()->setContextProperty(QStringLiteral("Workspace"), &workspace);
    engine.rootContext()->setContextProperty(QStringLiteral("Settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &config);
    engine.rootContext()->setContextProperty(QStringLiteral("Panels"), &panels);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/workspaces/MainWindow.qml")));
    QObject* object = component.create();
    QVERIFY2(object, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(object); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* canvas = object->findChild<CanvasViewport*>(QStringLiteral("canvasViewport")); QVERIFY(canvas);
    QTest::qWait(30); canvas->refreshNow();
    const auto location = canvas->mapToScene(QPointF(100, 100)).toPoint();
    const auto docPoint = canvas->viewportToDocument(100, 100);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, location);
    QTest::mouseMove(window, location + QPoint(25, 0), 10);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, location + QPoint(25, 0));
    auto& tiles = workspace.layers()->layerAt(0)->tiles();
    const TilePoint pixel{static_cast<int>(docPoint.x()) + 10, static_cast<int>(docPoint.y())};
    const auto painted = tiles.pixel(pixel).value(); QVERIFY(painted != 0);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(tiles.pixel(pixel).value(), 0U);
    QTest::keyClick(window, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(tiles.pixel(pixel).value(), painted);
    auto* title = object->findChild<QQuickItem*>(QStringLiteral("documentTitleField")); QVERIFY(title);
    title->forceActiveFocus(); QTest::qWait(20);
    QTest::keyClick(window, Qt::Key_X);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(tiles.pixel(pixel).value(), painted); // text undo must not undo artwork
    delete object;
}

QTEST_MAIN(TestCanvasViewport)
#include "TestCanvasViewport.moc"
