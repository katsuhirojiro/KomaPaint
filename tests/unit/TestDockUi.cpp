// The docking workspace through the real window: dragging tabs into another
// group, dropping on an edge to split, dragging a panel out to a real floating
// window and back, closing and reopening panels, collapsing a group, the drop
// preview, and recovering a layout that mentions panels this build does not
// have. Runs on the offscreen platform with the software renderer.
#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QWindow>

#include <memory>

#include "app/DockModel.h"
#include "app/CanvasViewport.h"
#include "app/PanelManager.h"
#include "app/WorkspaceState.h"
#include "core/ApplicationConfig.h"
#include "core/NewProjectSettings.h"
#include "core/Settings.h"

using namespace komapaint;

namespace {

struct Running {
    WorkspaceState workspace;
    NewProjectSettings project;
    ApplicationConfig config;
    // Declared before the panels (and the layout that saves into it) so it is
    // destroyed after them: tearing the dock down writes preferences.
    std::unique_ptr<Settings> settings;
    QString storedLayout;
    PanelManager panels;
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window = nullptr;
    QStringList messages;
};

QQuickItem* findVisual(QQuickItem* from, const QString& name)
{
    if (!from) return nullptr;
    if (from->objectName() == name) return from;
    for (auto* child : from->childItems())
        if (auto* found = findVisual(child, name)) return found;
    return nullptr;
}

QQuickItem* item(Running& app, const QString& name)
{
    return findVisual(app.window->contentItem(), name);
}

QPoint centre(QQuickItem* target)
{
    return target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
}

bool start(Running& app, bool keepStoredLayout = false)
{
    app.settings = std::make_unique<Settings>(QStringLiteral("KomaPaintTests"), QStringLiteral("DockUi"));
    if (!keepStoredLayout) app.settings->setValue(QStringLiteral("workspace/layout"), QString());
    app.settings->setValue(QStringLiteral("workspace/presets"), QString());
    app.settings->setValue(QStringLiteral("recovery/configured"), true);
    app.settings->setWindowMaximized(false);
    app.settings->setWindowSize(QSize(1200, 800));
    if (keepStoredLayout && !app.storedLayout.isEmpty())
        app.settings->setValue(QStringLiteral("workspace/layout"), app.storedLayout);
    app.panels.dockModel()->setSettings(app.settings.get());
    QObject::connect(&app.workspace, &WorkspaceState::fileErrorOccurred, &app.workspace,
                     [&app](const QString& message) { app.messages << message; });
    app.engine.addImportPath(QStringLiteral("qrc:/"));
    auto* context = app.engine.rootContext();
    context->setContextProperty(QStringLiteral("Settings"), app.settings.get());
    context->setContextProperty(QStringLiteral("Workspace"), &app.workspace);
    context->setContextProperty(QStringLiteral("NewProject"), &app.project);
    context->setContextProperty(QStringLiteral("App"), &app.config);
    context->setContextProperty(QStringLiteral("Panels"), &app.panels);
    context->setContextProperty(QStringLiteral("Dock"), app.panels.dockModel());
    QQmlComponent component(&app.engine, QUrl(QStringLiteral("qrc:/workspaces/MainWindow.qml")));
    app.root.reset(component.create());
    if (!app.root) { qWarning() << component.errorString(); return false; }
    app.window = qobject_cast<QQuickWindow*>(app.root.get());
    if (!app.window) return false;
    app.window->show();
    if (!QTest::qWaitForWindowExposed(app.window)) return false;
    QTest::qWait(60);
    if (!app.workspace.newProject(QStringLiteral("Dock"), 320, 240, true, QStringLiteral("white"), 72))
        return false;
    // The docking workspace is only shown for an open project, so its items get
    // their geometry on the first frame after the project appears. A drag that
    // starts before that lands on a zero-size tab and reaches nothing.
    for (int attempt = 0; attempt < 40; ++attempt) {
        auto* tab = item(app, QStringLiteral("dockTab_layers"));
        if (tab && tab->isVisible() && tab->width() > 0 && tab->height() > 0) return true;
        QTest::qWait(25);
    }
    qWarning() << "dock tabs never received geometry after the project opened";
    return false;
}

QStringList floatingWindowNames()
{
    QStringList names;
    for (QWindow* window : QGuiApplication::topLevelWindows())
        if (window->objectName().startsWith(QStringLiteral("dockFloatingWindow")))
            names << window->objectName();
    return names;
}

void dragTo(Running& app, QPoint from, QPoint to, int steps = 6)
{
    QTest::mouseMove(app.window, from);
    QTest::mousePress(app.window, Qt::LeftButton, Qt::NoModifier, from);
    for (int i = 1; i <= steps; ++i)
        QTest::mouseMove(app.window, from + (to - from) * i / steps, 8);
    QTest::mouseMove(app.window, to, 8);
    QTest::mouseRelease(app.window, Qt::LeftButton, Qt::NoModifier, to);
    QCoreApplication::processEvents();
    QTest::qWait(50);
}

}  // namespace

class TestDockUi : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void draggingATabIntoAnotherGroupMergesTheTabs();
    void draggingOntoAnEdgeSplitsTheGroup();
    void draggingOutOfTheWorkspaceFloatsAndClosingDocksBack();
    void dropPreviewShowsWhereThePanelWillLand();
    void tabCloseButtonHidesAndTheMenuBringsThePanelBack();
    void collapsingAGroupKeepsItsHeader();
    void resetLayoutRestoresTheDefaultWorkspace();
    void aStoredLayoutWithUnknownPanelsStillShowsEveryPanel();
    void collapsingTheLeftAreaLeavesAStripAndKeepsThePanels();
    void collapsingTheBottomAreaKeepsTheTimelineLoaded();
    void areaStateAndHiddenPanelsSurviveARestart();
    void everyDockAreaCollapsesAndComesBack();
};

void TestDockUi::initTestCase()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
}

void TestDockUi::draggingATabIntoAnotherGroupMergesTheTabs()
{
    Running app;
    QVERIFY(start(app));
    auto* dock = app.panels.dockModel();
    const int layersGroup = dock->groupOf(QStringLiteral("layers"));
    const int colorGroup = dock->groupOf(QStringLiteral("color"));
    QVERIFY(layersGroup > 0 && colorGroup > 0 && layersGroup != colorGroup);

    auto* tab = item(app, QStringLiteral("dockTab_layers"));
    QVERIFY(tab);
    auto* targetBody = item(app, QStringLiteral("dockBody%1").arg(colorGroup));
    QVERIFY(targetBody);
    dragTo(app, centre(tab), centre(targetBody));

    // The two panels are now tabs of one group.
    QTRY_COMPARE(dock->groupOf(QStringLiteral("layers")), colorGroup);
    QVERIFY(dock->nodeById(colorGroup).value(QStringLiteral("panels")).toStringList()
                .contains(QStringLiteral("layers")));
    QVERIFY(item(app, QStringLiteral("dockTab_layers")));
    QVERIFY(item(app, QStringLiteral("dockTab_color")));
    QVERIFY2(app.messages.isEmpty(), qPrintable(app.messages.join(QLatin1Char('\n'))));
}

void TestDockUi::draggingOntoAnEdgeSplitsTheGroup()
{
    Running app;
    QVERIFY(start(app));
    auto* dock = app.panels.dockModel();
    const int layersGroup = dock->groupOf(QStringLiteral("layers"));
    auto* tab = item(app, QStringLiteral("dockTab_layers"));
    QVERIFY(tab);
    const int propGroup = dock->groupOf(QStringLiteral("properties"));
    auto* propBody = item(app, QStringLiteral("dockBody%1").arg(propGroup));
    QVERIFY(propBody);
    const QRectF propRect(propBody->mapToScene(QPointF(0, 0)), QSizeF(propBody->width(), propBody->height()));
    // A point just inside the bottom edge of the Properties group: the panel
    // should be split off underneath it, not merged as a tab.
    const QPoint target(int(propRect.center().x()), int(propRect.bottom() - 6));
    dragTo(app, centre(tab), target);

    QTRY_VERIFY(dock->groupOf(QStringLiteral("layers")) != layersGroup);
    const int newGroup = dock->groupOf(QStringLiteral("layers"));
    QVERIFY(newGroup > 0);
    // Properties stayed in its own group; Layers now has one of its own, so the
    // old empty group is gone and both panels are still reachable.
    QCOMPARE(dock->groupOf(QStringLiteral("properties")), propGroup);
    QVERIFY(newGroup != propGroup);
    QVERIFY(dock->nodeById(newGroup).value(QStringLiteral("panels")).toStringList()
                == QStringList{QStringLiteral("layers")});
    QVERIFY(item(app, QStringLiteral("dockGroup%1").arg(newGroup)));
    QVERIFY(item(app, QStringLiteral("canvasViewport")));
}

void TestDockUi::draggingOutOfTheWorkspaceFloatsAndClosingDocksBack()
{
    Running app;
    QVERIFY(start(app));
    auto* dock = app.panels.dockModel();
    auto* tab = item(app, QStringLiteral("dockTab_layers"));
    QVERIFY(tab);
    QVERIFY(floatingWindowNames().isEmpty());
    // Over the tool palette: outside the docking workspace.
    auto* palette = item(app, QStringLiteral("toolPalette"));
    QVERIFY(palette);
    dragTo(app, centre(tab), centre(palette));

    QTRY_VERIFY(dock->isFloating(QStringLiteral("layers")));
    QCOMPARE(dock->floating().size(), 1);
    QTRY_COMPARE(floatingWindowNames().size(), 1);
    // The panel is no longer part of the main window's tree.
    QVERIFY(item(app, QStringLiteral("dockTab_layers")) == nullptr);

    // Closing the floating window docks the panel back instead of losing it.
    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        if (!window->objectName().startsWith(QStringLiteral("dockFloatingWindow"))) continue;
        window->close();
    }
    QTRY_VERIFY(!dock->isFloating(QStringLiteral("layers")));
    QVERIFY(dock->isVisible(QStringLiteral("layers")));
    QTRY_VERIFY(floatingWindowNames().isEmpty());
    QTRY_VERIFY(item(app, QStringLiteral("dockTab_layers")));
}

void TestDockUi::dropPreviewShowsWhereThePanelWillLand()
{
    Running app;
    QVERIFY(start(app));
    auto* dock = app.panels.dockModel();
    auto* preview = item(app, QStringLiteral("dockDropPreview"));
    QVERIFY(preview);
    QVERIFY(!preview->isVisible());

    auto* tab = item(app, QStringLiteral("dockTab_layers"));
    auto* propBody = item(app, QStringLiteral("dockBody%1").arg(dock->groupOf(QStringLiteral("properties"))));
    QVERIFY(tab && propBody);
    const QRectF propRect(propBody->mapToScene(QPointF(0, 0)), QSizeF(propBody->width(), propBody->height()));

    // Press, then move without releasing: the preview is up and covers the
    // half of the target the panel would take.
    const QPoint from = centre(tab);
    const QPoint to(int(propRect.left() + 8), int(propRect.center().y()));
    QTest::mouseMove(app.window, from);
    QTest::mousePress(app.window, Qt::LeftButton, Qt::NoModifier, from);
    for (int i = 1; i <= 5; ++i) QTest::mouseMove(app.window, from + (to - from) * i / 5, 8);
    QTest::mouseMove(app.window, to, 8);
    QCoreApplication::processEvents();
    QTRY_VERIFY(preview->isVisible());
    const QRectF shown(QPointF(preview->x(), preview->y()), QSizeF(preview->width(), preview->height()));
    // Left half of the target group, in workspace coordinates.
    QVERIFY(shown.height() > propRect.height() * 0.9);
    QVERIFY(qAbs(shown.width() - propRect.width() / 2) < 3.0);
    // The model is untouched until the drop happens.
    const int before = dock->groupOf(QStringLiteral("layers"));
    QTest::mouseRelease(app.window, Qt::LeftButton, Qt::NoModifier, to);
    QCoreApplication::processEvents();
    QTRY_VERIFY(!preview->isVisible());
    QVERIFY(dock->groupOf(QStringLiteral("layers")) != before);
    QVERIFY2(app.messages.isEmpty(), qPrintable(app.messages.join(QLatin1Char('\n'))));
}

void TestDockUi::tabCloseButtonHidesAndTheMenuBringsThePanelBack()
{
    Running app;
    QVERIFY(start(app));
    auto* dock = app.panels.dockModel();
    auto* close = item(app, QStringLiteral("dockTabClose_layers"));
    QVERIFY(close);
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, centre(close));
    QTRY_VERIFY(!dock->isVisible(QStringLiteral("layers")));
    QVERIFY(dock->hiddenPanels().contains(QStringLiteral("layers")));
    QTRY_VERIFY(item(app, QStringLiteral("dockTab_layers")) == nullptr);

    // The same model drives the View > Panels entries and the keyboard-free
    // path the menu uses.
    app.panels.toggle(QStringLiteral("layers"));
    QTRY_VERIFY(dock->isVisible(QStringLiteral("layers")));
    QTRY_VERIFY(item(app, QStringLiteral("dockTab_layers")) != nullptr);
    QVERIFY(!dock->hiddenPanels().contains(QStringLiteral("layers")));
}

void TestDockUi::collapsingAGroupKeepsItsHeader()
{
    Running app;
    QVERIFY(start(app));
    auto* dock = app.panels.dockModel();
    const int group = dock->groupOf(QStringLiteral("layers"));
    auto* collapse = item(app, QStringLiteral("dockCollapse%1").arg(group));
    auto* body = item(app, QStringLiteral("dockBody%1").arg(group));
    QVERIFY(collapse && body);
    QVERIFY(body->isVisible());
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, centre(collapse));
    QTRY_VERIFY(dock->isCollapsed(QStringLiteral("layers")));
    QTRY_VERIFY(!item(app, QStringLiteral("dockBody%1").arg(group))->isVisible());
    // The header and its tab stay reachable, so the group can be expanded again.
    QVERIFY(item(app, QStringLiteral("dockTab_layers")));
    auto* collapseAgain = item(app, QStringLiteral("dockCollapse%1").arg(group));
    QVERIFY(collapseAgain);
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, centre(collapseAgain));
    QTRY_VERIFY(!dock->isCollapsed(QStringLiteral("layers")));
    QTRY_VERIFY(item(app, QStringLiteral("dockBody%1").arg(group))->isVisible());
}

void TestDockUi::resetLayoutRestoresTheDefaultWorkspace()
{
    Running app;
    QVERIFY(start(app));
    auto* dock = app.panels.dockModel();
    QVariantMap initial = dock->serialise();
    QVERIFY(dock->floatPanel(QStringLiteral("color"), 40, 40, 320, 240));
    dock->hidePanel(QStringLiteral("properties"));
    QVERIFY(dock->setCollapsed(QStringLiteral("layers"), true));
    QTRY_VERIFY(!dock->isVisible(QStringLiteral("properties")));

    dock->resetToDefault();
    QCOMPARE(dock->serialise(), initial);
    QTRY_VERIFY(item(app, QStringLiteral("dockTab_properties")));
    QTRY_VERIFY(item(app, QStringLiteral("dockTab_color")));
    QVERIFY(!dock->isCollapsed(QStringLiteral("layers")));
    QTRY_VERIFY(floatingWindowNames().isEmpty());
}

void TestDockUi::aStoredLayoutWithUnknownPanelsStillShowsEveryPanel()
{
    Running app;
    // A layout saved by another build: it has a panel this one does not know
    // ("ghostpanel" instead of Properties) and Color was closed on purpose.
    QVariantMap stored;
    {
        DockModel scratch;
        scratch.resetToDefault();
        scratch.hidePanel(QStringLiteral("color"));
        stored = scratch.serialise();
    }
    QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(stored)).toJson(QJsonDocument::Compact);
    json.replace("\"properties\"", "\"ghostpanel\"");
    app.storedLayout = QString::fromUtf8(json);

    QVERIFY(start(app, true));
    auto* dock = app.panels.dockModel();
    for (const auto& entry : DockModel::panelTable()) {
        if (!entry.visibleByDefault) continue;
        if (entry.id == QStringLiteral("color")) continue;   // closed in the stored layout
        QVERIFY2(dock->isVisible(entry.id), qPrintable(entry.id));
    }
    QVERIFY(!dock->isKnown(QStringLiteral("ghostpanel")));
    QVERIFY(!dock->isVisible(QStringLiteral("color")));   // it was closed on purpose
    QTRY_VERIFY(item(app, QStringLiteral("canvasViewport")));
    QVERIFY(item(app, QStringLiteral("dockTab_layers")));
    QVERIFY(item(app, QStringLiteral("dockTab_properties")));
}

void TestDockUi::collapsingTheLeftAreaLeavesAStripAndKeepsThePanels()
{
    Running app;
    QVERIFY(start(app));
    auto* dock = app.panels.dockModel();
    auto* canvas = item(app, QStringLiteral("canvasViewport"));
    QVERIFY(canvas);
    const qreal wide = canvas->width();
    const int rootId = dock->tree().value(QStringLiteral("id")).toInt();
    const QVariantList fractionsBefore = dock->fractionsOf(rootId);
    QVERIFY(!dock->isAreaCollapsed(QStringLiteral("left")));

    auto* control = item(app, QStringLiteral("dockAreaControl_left"));
    QVERIFY(control);
    QVERIFY(control->isVisible());
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, centre(control));
    QTRY_VERIFY(dock->isAreaCollapsed(QStringLiteral("left")));
    auto stripVisible = [&app](const QString& name) {
        auto* strip = item(app, name);
        return strip && strip->isVisible();
    };
    QTRY_VERIFY(stripVisible(QStringLiteral("dockAreaStrip_left")));
    // The band is gone and the canvas takes the room.
    QTRY_VERIFY(canvas->width() > wide + 40);
    // Collapsing is a hide: the panels and their tabs are still loaded, the
    // splitter fractions are untouched and the model still lists them.
    QVERIFY(item(app, QStringLiteral("dockTab_subtool")));
    QVERIFY(!item(app, QStringLiteral("dockTab_subtool"))->isVisible());
    QVERIFY(dock->isVisible(QStringLiteral("subtool")));
    QCOMPARE(dock->fractionsOf(rootId), fractionsBefore);
    QCOMPARE(dock->areaSideOf(dock->groupOf(QStringLiteral("subtool"))), QStringLiteral("left"));

    // Clicking the strip restores the area exactly as it was.
    auto* strip = item(app, QStringLiteral("dockAreaStrip_left"));
    QVERIFY(strip && strip->isVisible());
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, centre(strip));
    QTRY_VERIFY(!dock->isAreaCollapsed(QStringLiteral("left")));
    QTRY_VERIFY(qAbs(canvas->width() - wide) < 2.0);
    QTRY_VERIFY(item(app, QStringLiteral("dockTab_subtool"))->isVisible());
    QCOMPARE(dock->fractionsOf(rootId), fractionsBefore);
    QTRY_VERIFY(!stripVisible(QStringLiteral("dockAreaStrip_left")));
}

void TestDockUi::collapsingTheBottomAreaKeepsTheTimelineLoaded()
{
    Running app;
    QVERIFY(start(app));
    auto* dock = app.panels.dockModel();
    QVERIFY(dock->dockToWorkspace(QStringLiteral("timeline"), QStringLiteral("bottom")));
    QVERIFY(dock->hasArea(QStringLiteral("bottom")));
    QTRY_VERIFY(item(app, QStringLiteral("dockTab_timeline")) != nullptr);
    auto* canvas = item(app, QStringLiteral("canvasViewport"));
    QVERIFY(canvas);
    const qreal tall = canvas->height();

    auto* control = item(app, QStringLiteral("dockAreaControl_bottom"));
    QVERIFY(control);
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, centre(control));
    QTRY_VERIFY(dock->isAreaCollapsed(QStringLiteral("bottom")));
    QTRY_VERIFY(item(app, QStringLiteral("dockAreaStrip_bottom")) != nullptr);
    QTRY_VERIFY(item(app, QStringLiteral("dockAreaStrip_bottom"))->isVisible());
    QTRY_VERIFY(canvas->height() > tall + 20);
    // A band under the canvas must not steal width from the side columns.
    QVERIFY(item(app, QStringLiteral("dockTab_subtool")));
    QVERIFY(item(app, QStringLiteral("dockTab_subtool"))->isVisible());
    QVERIFY(item(app, QStringLiteral("dockTab_timeline")));
    QVERIFY(!item(app, QStringLiteral("dockTab_timeline"))->isVisible());

    auto* strip = item(app, QStringLiteral("dockAreaStrip_bottom"));
    QVERIFY(strip);
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, centre(strip));
    QTRY_VERIFY(!dock->isAreaCollapsed(QStringLiteral("bottom")));
    QTRY_VERIFY(qAbs(canvas->height() - tall) < 2.0);
    QTRY_VERIFY(item(app, QStringLiteral("dockTab_timeline"))->isVisible());
    QTRY_VERIFY(!item(app, QStringLiteral("dockAreaStrip_bottom"))->isVisible());
}

void TestDockUi::areaStateAndHiddenPanelsSurviveARestart()
{
    QString stored;
    {
        Running app;
        QVERIFY(start(app));
        auto* dock = app.panels.dockModel();
        QVERIFY(dock->setAreaCollapsed(QStringLiteral("left"), true));
        dock->hidePanel(QStringLiteral("brushsize"));
        QVERIFY(!dock->isVisible(QStringLiteral("brushsize")));
        QTRY_VERIFY_WITH_TIMEOUT(!app.settings->value(QStringLiteral("workspace/layout")).toString().isEmpty(),
                                 3000);
        stored = app.settings->value(QStringLiteral("workspace/layout")).toString();
        QVERIFY(stored.contains(QStringLiteral("collapsedAreas")));
    }

    Running app;
    app.storedLayout = stored;
    QVERIFY(start(app, true));
    auto* dock = app.panels.dockModel();
    QVERIFY(dock->isAreaCollapsed(QStringLiteral("left")));
    QVERIFY(!dock->isVisible(QStringLiteral("brushsize")));
    QTRY_VERIFY(item(app, QStringLiteral("dockAreaStrip_left")) != nullptr);
    QTRY_VERIFY(item(app, QStringLiteral("dockAreaStrip_left"))->isVisible());
    QVERIFY(!item(app, QStringLiteral("dockTab_brushsize")));

    // Bringing the panel back also opens the band it belongs to.
    app.panels.toggle(QStringLiteral("brushsize"));
    QTRY_VERIFY(dock->isVisible(QStringLiteral("brushsize")));
    QTRY_VERIFY(!dock->isAreaCollapsed(QStringLiteral("left")));
    QTRY_VERIFY(item(app, QStringLiteral("dockTab_brushsize")) != nullptr);
    QCOMPARE(dock->areaSideOf(dock->groupOf(QStringLiteral("brushsize"))), QStringLiteral("left"));
}

void TestDockUi::everyDockAreaCollapsesAndComesBack()
{
    Running app;
    QVERIFY(start(app));
    auto* dock = app.panels.dockModel();
    // Make sure a band exists on every side: left and right are in the default
    // layout, the other two are built by docking a panel there.
    QVERIFY(dock->dockToWorkspace(QStringLiteral("assets"), QStringLiteral("top")));
    QVERIFY(dock->dockToWorkspace(QStringLiteral("timeline"), QStringLiteral("bottom")));
    auto* canvas = item(app, QStringLiteral("canvasViewport"));
    QVERIFY(canvas);

    const QStringList sides{ QStringLiteral("left"), QStringLiteral("right"),
                             QStringLiteral("top"), QStringLiteral("bottom") };
    for (const QString& side : sides) {
        QVERIFY2(dock->hasArea(side), qPrintable(side));
        const bool horizontal = (side == QLatin1String("left") || side == QLatin1String("right"));
        const qreal before = horizontal ? canvas->width() : canvas->height();
        const int rootId = dock->tree().value(QStringLiteral("id")).toInt();
        const QVariantList fractions = dock->fractionsOf(rootId);
        QVERIFY2(!dock->isAreaCollapsed(side), qPrintable(side));

        auto* control = item(app, QStringLiteral("dockAreaControl_") + side);
        QVERIFY2(control, qPrintable(side));
        QVERIFY2(control->isVisible(), qPrintable(side));
        QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, centre(control));
        QTRY_VERIFY2(dock->isAreaCollapsed(side), qPrintable(side));

        // The band is a thin strip and the canvas took the room back.
        auto* strip = item(app, QStringLiteral("dockAreaStrip_") + side);
        QVERIFY2(strip, qPrintable(side));
        QTRY_VERIFY2(strip->isVisible(), qPrintable(side));
        const qreal thin = horizontal ? strip->width() : strip->height();
        QVERIFY2(thin <= 20.0, qPrintable(QStringLiteral("%1 strip is %2 px").arg(side).arg(thin)));
        QTRY_VERIFY2((horizontal ? canvas->width() : canvas->height()) > before + 20,
                     qPrintable(side));
        // Nothing was lost on the way: no panel became hidden and the
        // splitter shares are untouched.
        QVERIFY2(dock->isVisible(QStringLiteral("layers")), qPrintable(side));
        QVERIFY2(dock->isVisible(QStringLiteral("timeline")), qPrintable(side));
        QCOMPARE(dock->fractionsOf(rootId), fractions);

        // Clicking the strip restores the area exactly.
        QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, centre(strip));
        QTRY_VERIFY2(!dock->isAreaCollapsed(side), qPrintable(side));
        QTRY_VERIFY2(qAbs((horizontal ? canvas->width() : canvas->height()) - before) < 2.0,
                     qPrintable(side));
        QTRY_VERIFY2(!strip->isVisible(), qPrintable(side));
        QCOMPARE(dock->fractionsOf(rootId), fractions);
    }

    // The menu path does the same thing through the model.
    QVERIFY(app.panels.toggleArea(QStringLiteral("right")));
    QTRY_VERIFY(dock->isAreaCollapsed(QStringLiteral("right")));
    QVERIFY(app.panels.toggleArea(QStringLiteral("right")));
    QTRY_VERIFY(!dock->isAreaCollapsed(QStringLiteral("right")));
    QVERIFY(dock->collapsedAreas().isEmpty());
}

QTEST_MAIN(TestDockUi)
#include "TestDockUi.moc"
