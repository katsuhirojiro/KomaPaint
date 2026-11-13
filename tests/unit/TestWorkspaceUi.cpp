// Phase 3 through the real MainWindow.qml: the recovery list, the close
// questions, per-project canvas views and session layout are driven with
// real mouse events against the real window, not by calling WorkspaceState
// behind the UI's back. Runs on the offscreen platform with the software
// renderer, so it proves the wiring, not how it looks on a real screen.
#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>

#include <memory>

#include "app/CanvasViewport.h"
#include "app/PanelManager.h"
#include "app/WorkspaceState.h"
#include "core/ApplicationConfig.h"
#include "core/NewProjectSettings.h"
#include "core/Settings.h"
#include "layers/Layer.h"

using namespace komapaint;

namespace {

struct Parts {
    WorkspaceState workspace;
    NewProjectSettings project;
    ApplicationConfig config;
    PanelManager panels;
};

// One running copy of the application: its state objects, engine and window.
struct Running {
    Parts parts;
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window = nullptr;
    QStringList messages;
};

bool paint(WorkspaceState& ws, QPoint at, quint32 color)
{
    auto* layer = ws.layers()->layerAt(ws.layers()->activeLayerIndex());
    if (!ws.history()->beginStroke(layer)) return false;
    if (layer->tiles().setPixel({at.x(), at.y()}, color) != SparseTileStore::WriteResult::Changed) {
        ws.history()->cancelStroke();
        return false;
    }
    return ws.history()->commitStroke();
}

quint32 pixel(const WorkspaceState& ws, QPoint at)
{
    return ws.layers()->layerAt(0)->tiles().pixel({at.x(), at.y()}).value();
}

QStringList snapshotFiles(const QString& recovery)
{
    return QDir(recovery).entryList({QStringLiteral("*.koma")}, QDir::Files, QDir::Name);
}

QString tabTitle(const WorkspaceState& ws, int index)
{
    return ws.projectTabs().at(index).toMap().value(QStringLiteral("title")).toString();
}

// Delegates and popup contents are not always QObject children of the
// window, so look through the visual tree.
QQuickItem* findVisual(QQuickItem* from, const QString& name)
{
    if (!from) return nullptr;
    if (from->objectName() == name) return from;
    for (auto* child : from->childItems())
        if (auto* found = findVisual(child, name)) return found;
    return nullptr;
}

QQuickItem* findInWindow(QQuickWindow* window, const QString& name)
{
    return findVisual(window->contentItem(), name);
}

// The tab bar is rebuilt and laid out on the next frame after the list of
// projects changes; a click before that would land on a stale position.
QQuickItem* tabItem(QQuickWindow* window, const QString& name, int index)
{
    QQuickItem* item = nullptr;
    for (int attempt = 0; attempt < 100; ++attempt) {
        QTest::qWait(20);
        item = findInWindow(window, name + QString::number(index));
        if (!item || item->width() <= 0) continue;
        if (index == 0) return item;
        auto* previous = findInWindow(window, name + QString::number(index - 1));
        if (previous && item->mapToScene(QPointF(0, 0)).x() > previous->mapToScene(QPointF(0, 0)).x()) return item;
    }
    return nullptr;
}

void click(QQuickWindow* window, QQuickItem* item)
{
    const QPoint at = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
}

}  // namespace

class TestWorkspaceUi : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void recoveryListRecoversAndDiscardsSingleEntries();
    void eachProjectKeepsItsOwnViewAndTool();
    void closeQuestionsCancelKeepsEverything();
    void tabCloseTouchesOnlyThatProject();
    void sessionRestoresProjectsAndLayoutAtStartup();
    void toolbarOpenAndSaveAreConnected();

private:
    bool start(Running& app, const QString& settingsName, const QString& recovery, const QString& session);
    std::unique_ptr<Settings> m_settings;
};

void TestWorkspaceUi::initTestCase()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
}

bool TestWorkspaceUi::start(Running& app, const QString& settingsName, const QString& recovery, const QString& session)
{
    m_settings = std::make_unique<Settings>(QStringLiteral("KomaPaintTests"), settingsName);
    m_settings->setValue(QStringLiteral("recovery/configured"), true);
    m_settings->setValue(QStringLiteral("recovery/intervalMinutes"), 10);
    m_settings->setWindowMaximized(false);
    m_settings->setWindowSize(QSize(1100, 700));
    connect(&app.parts.workspace, &WorkspaceState::fileErrorOccurred, &app.parts.workspace,
            [&app](const QString& message) { app.messages << message; });
    // Same order as KomaPaintApp: recovery folder, then the session file.
    if (!recovery.isEmpty() && !app.parts.workspace.initializeRecovery(recovery)) return false;
    if (!session.isEmpty() && !app.parts.workspace.initializeSession(session)) return false;
    app.engine.addImportPath(QStringLiteral("qrc:/"));
    auto* context = app.engine.rootContext();
    context->setContextProperty(QStringLiteral("Settings"), m_settings.get());
    context->setContextProperty(QStringLiteral("Workspace"), &app.parts.workspace);
    context->setContextProperty(QStringLiteral("NewProject"), &app.parts.project);
    context->setContextProperty(QStringLiteral("App"), &app.parts.config);
    context->setContextProperty(QStringLiteral("Panels"), &app.parts.panels);
    QQmlComponent component(&app.engine, QUrl(QStringLiteral("qrc:/workspaces/MainWindow.qml")));
    app.root.reset(component.create());
    if (!app.root) { qWarning() << component.errorString(); return false; }
    app.window = qobject_cast<QQuickWindow*>(app.root.get());
    if (!app.window) return false;
    app.window->show();
    if (!QTest::qWaitForWindowExposed(app.window)) return false;
    // Start-up work (session restore, recovery prompt) is deferred by one
    // event-loop turn in MainWindow.qml.
    QTest::qWait(50);
    return true;
}

void TestWorkspaceUi::recoveryListRecoversAndDiscardsSingleEntries()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    const QString savedPath = dir.filePath(QStringLiteral("picture.koma"));
    {
        WorkspaceState previous;
        QVERIFY(previous.initializeRecovery(recovery));
        QVERIFY(previous.newProject(QStringLiteral("Sketch"), 64, 64, true, QStringLiteral("white"), 72));
        QVERIFY(paint(previous, {1, 1}, 0xffff0000U));
        QVERIFY(previous.newProject(QStringLiteral("Picture"), 64, 64, true, QStringLiteral("white"), 72));
        QVERIFY(paint(previous, {1, 1}, 0xff00ff00U));
        QVERIFY(previous.saveToPath(savedPath));
        QVERIFY(paint(previous, {2, 2}, 0xff0000ffU));
        QVERIFY(previous.newProject(QStringLiteral("Doodle"), 64, 64, true, QStringLiteral("white"), 72));
        QVERIFY(paint(previous, {1, 1}, 0xffffffffU));
        QVERIFY(previous.persistWorkspace());
        // crash
    }
    QCOMPARE(snapshotFiles(recovery).size(), 3);

    Running app;
    QVERIFY(start(app, QStringLiteral("WorkspaceUiRecovery"), recovery, dir.filePath(QStringLiteral("session.json"))));
    auto& ws = app.parts.workspace;
    auto* dialog = app.root->findChild<QObject*>(QStringLiteral("recoveryChoiceDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    // Nothing was opened or deleted by merely showing the list.
    QCOMPARE(ws.projectTabs().size(), 1);
    QCOMPARE(snapshotFiles(recovery).size(), 3);
    auto* list = qobject_cast<QQuickItem*>(app.root->findChild<QObject*>(QStringLiteral("recoveryEntryList")));
    QVERIFY(list);
    QTRY_COMPARE(list->property("count").toInt(), 3);

    auto rowOf = [&](const QString& title) {
        const int count = list->property("count").toInt();
        for (int i = 0; i < count; ++i) {
            auto* label = findVisual(list, QStringLiteral("recoveryEntryTitle%1").arg(i));
            if (label && label->property("text").toString() == title) return i;
        }
        return -1;
    };
    QTRY_VERIFY(rowOf(QStringLiteral("Picture")) >= 0);
    // The list tells a saved file from work that was never saved.
    const int pictureRow = rowOf(QStringLiteral("Picture"));
    const int sketchRow = rowOf(QStringLiteral("Sketch"));
    QVERIFY(sketchRow >= 0 && rowOf(QStringLiteral("Doodle")) >= 0);
    QVERIFY(findVisual(list, QStringLiteral("recoveryEntryKind%1").arg(pictureRow))->property("text").toString()
                .contains(QStringLiteral("picture.koma")));
    QCOMPARE(findVisual(list, QStringLiteral("recoveryEntryKind%1").arg(sketchRow))->property("text").toString(),
             QStringLiteral("Never saved"));

    // Recover only "Picture".
    auto* recoverButton = findVisual(list, QStringLiteral("recoverEntryButton%1").arg(pictureRow));
    QVERIFY(recoverButton);
    click(app.window, recoverButton);
    QTRY_COMPARE(list->property("count").toInt(), 2);
    QCOMPARE(ws.projectTabs().size(), 1);
    QCOMPARE(ws.document()->title(), QStringLiteral("Picture"));
    QCOMPARE(ws.currentFile(), savedPath);
    QVERIFY(ws.document()->isModified());
    QCOMPARE(pixel(ws, {2, 2}), 0xff0000ffU);
    QVERIFY(dialog->property("opened").toBool());  // the others still need a decision
    QVERIFY(app.messages.isEmpty());

    // Discard only "Doodle": a confirmation comes first, and Cancel there
    // discards nothing.
    auto* confirm = app.root->findChild<QObject*>(QStringLiteral("recoveryDiscardConfirmation"));
    QVERIFY(confirm);
    QTRY_VERIFY(rowOf(QStringLiteral("Doodle")) >= 0);
    click(app.window, findVisual(list, QStringLiteral("discardEntryButton%1").arg(rowOf(QStringLiteral("Doodle")))));
    QTRY_VERIFY(confirm->property("opened").toBool());
    QCOMPARE(snapshotFiles(recovery).size(), 3);
    QVERIFY(QMetaObject::invokeMethod(confirm, "reject"));
    QTRY_VERIFY(!confirm->property("visible").toBool());
    QCOMPARE(snapshotFiles(recovery).size(), 3);
    QCOMPARE(list->property("count").toInt(), 2);
    click(app.window, findVisual(list, QStringLiteral("discardEntryButton%1").arg(rowOf(QStringLiteral("Doodle")))));
    QTRY_VERIFY(confirm->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(confirm, "accept"));
    QTRY_COMPARE(list->property("count").toInt(), 1);
    QCOMPARE(rowOf(QStringLiteral("Sketch")), 0);
    QCOMPARE(ws.projectTabs().size(), 1);  // discarding opened nothing

    // "Decide later" for the last one: the dialog goes away, the data stays.
    QTRY_VERIFY(!confirm->property("visible").toBool());
    auto* later = findInWindow(app.window, QStringLiteral("notNowRecoveryButton"));
    QVERIFY(later);
    click(app.window, later);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(ws.recoveryAvailable());
    QCOMPARE(ws.recoveryEntries().size(), 1);
    QVERIFY(ws.recoveryTimerActive());  // periodic snapshots run regardless
    // Picture (recovered, still unsaved) and Sketch (postponed) are on disk.
    QVERIFY(ws.persistWorkspace());
    QCOMPARE(snapshotFiles(recovery).size(), 2);
}

void TestWorkspaceUi::eachProjectKeepsItsOwnViewAndTool()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Running app;
    QVERIFY(start(app, QStringLiteral("WorkspaceUiViews"), dir.filePath(QStringLiteral("recovery")),
                  dir.filePath(QStringLiteral("session.json"))));
    auto& ws = app.parts.workspace;
    auto* canvas = qobject_cast<CanvasViewport*>(findInWindow(app.window, QStringLiteral("canvasViewport")));
    QVERIFY(canvas);
    QTRY_VERIFY(canvas->width() > 100 && canvas->height() > 100);

    QVERIFY(ws.newProject(QStringLiteral("Wide"), 800, 200, true, QStringLiteral("white"), 72));
    QTRY_COMPARE(canvas->document(), static_cast<QObject*>(ws.document()));
    // A project shown for the first time is fitted, and that becomes its view.
    QTRY_VERIFY(ws.activeViewStored());
    const qreal wideFit = ws.zoom();
    QCOMPARE(canvas->zoom(), wideFit);
    // Zoom in around a point and drag the canvas with the middle button,
    // the way the user does.
    QVERIFY(QMetaObject::invokeMethod(canvas, "zoomAt", Q_ARG(qreal, 40.0), Q_ARG(qreal, 30.0), Q_ARG(qreal, 3.0)));
    const QPoint grab = canvas->mapToScene(QPointF(canvas->width() / 2, canvas->height() / 2)).toPoint();
    QTest::mousePress(app.window, Qt::MiddleButton, Qt::NoModifier, grab);
    QTest::mouseMove(app.window, grab + QPoint(-60, 25));
    QTest::mouseMove(app.window, grab + QPoint(-120, 50));
    QTest::mouseRelease(app.window, Qt::MiddleButton, Qt::NoModifier, grab + QPoint(-120, 50));
    const qreal wideZoom = canvas->zoom();
    const QPointF wideCenter = canvas->viewCenter();
    QVERIFY(wideZoom > wideFit * 2.5);
    QCOMPARE(ws.zoom(), wideZoom);
    QCOMPARE(ws.activeViewCenter(), wideCenter);
    QVERIFY(qAbs(wideCenter.x() - 400.0) > 1.0);  // it really is off-centre
    ws.setActiveTool(QStringLiteral("eraser"));

    // A second project gets its own fitted view; it does not inherit Wide's.
    QVERIFY(ws.newProject(QStringLiteral("Tall"), 200, 900, true, QStringLiteral("white"), 72));
    QTRY_COMPARE(canvas->document(), static_cast<QObject*>(ws.document()));
    QTRY_VERIFY(ws.activeViewStored());
    const qreal tallFit = canvas->zoom();
    QVERIFY(!qFuzzyCompare(tallFit, wideZoom));
    QCOMPARE(ws.zoom(), tallFit);
    QVERIFY(qAbs(canvas->viewCenter().x() - 100.0) < 0.5);
    QVERIFY(qAbs(canvas->viewCenter().y() - 450.0) < 0.5);
    QVERIFY(QMetaObject::invokeMethod(canvas, "zoomAt", Q_ARG(qreal, 300.0), Q_ARG(qreal, 300.0), Q_ARG(qreal, 0.5)));
    const qreal tallZoom = canvas->zoom();
    const QPointF tallCenter = canvas->viewCenter();
    ws.setActiveTool(QStringLiteral("fill"));

    // Switch back with the real tab button.
    auto* wideTab = tabItem(app.window, QStringLiteral("projectTabButton"), 0);
    QVERIFY(wideTab);
    QCOMPARE(wideTab->property("text").toString(), QStringLiteral("Wide"));
    click(app.window, wideTab);
    QTRY_COMPARE(ws.document()->title(), QStringLiteral("Wide"));
    QTRY_COMPARE(canvas->document(), static_cast<QObject*>(ws.document()));
    QCOMPARE(canvas->zoom(), wideZoom);
    QVERIFY(qAbs(canvas->viewCenter().x() - wideCenter.x()) < 0.01);
    QVERIFY(qAbs(canvas->viewCenter().y() - wideCenter.y()) < 0.01);
    QCOMPARE(ws.activeTool(), QStringLiteral("eraser"));

    // And forward again: Tall's view was not disturbed by showing Wide.
    auto* tallTab = tabItem(app.window, QStringLiteral("projectTabButton"), 1);
    QVERIFY(tallTab);
    QCOMPARE(tallTab->property("text").toString(), QStringLiteral("Tall"));
    click(app.window, tallTab);
    QTRY_COMPARE(ws.document()->title(), QStringLiteral("Tall"));
    QTRY_COMPARE(canvas->document(), static_cast<QObject*>(ws.document()));
    QCOMPARE(canvas->zoom(), tallZoom);
    QVERIFY(qAbs(canvas->viewCenter().x() - tallCenter.x()) < 0.01);
    QVERIFY(qAbs(canvas->viewCenter().y() - tallCenter.y()) < 0.01);
    QCOMPARE(ws.activeTool(), QStringLiteral("fill"));

    // The toolbar zoom acts on the visible project only.
    ws.zoomIn();
    QTRY_VERIFY(canvas->zoom() > tallZoom);
    wideTab = tabItem(app.window, QStringLiteral("projectTabButton"), 0);
    QVERIFY(wideTab);
    click(app.window, wideTab);
    QTRY_COMPARE(ws.document()->title(), QStringLiteral("Wide"));
    QCOMPARE(canvas->zoom(), wideZoom);
}

void TestWorkspaceUi::closeQuestionsCancelKeepsEverything()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    const QString session = dir.filePath(QStringLiteral("session.json"));
    const QString savedPath = dir.filePath(QStringLiteral("saved.koma"));
    QByteArray savedBytes;
    {
        Running app;
        QVERIFY(start(app, QStringLiteral("WorkspaceUiClose"), recovery, session));
        auto& ws = app.parts.workspace;
        QVERIFY(ws.newProject(QStringLiteral("Saved"), 64, 64, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, 0xffff0000U));
        QVERIFY(ws.saveToPath(savedPath));
        QVERIFY(paint(ws, {2, 2}, 0xff00ff00U));
        QVERIFY(ws.newProject(QStringLiteral("Loose"), 64, 64, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, 0xff0000ffU));
        // Created and never touched: closing asks nothing about it.
        QVERIFY(ws.newProject(QStringLiteral("Quiet"), 64, 64, true, QStringLiteral("white"), 72));
        QVERIFY(ws.persistWorkspace());
        // Until it is saved or closed even the blank new project is covered.
        QCOMPARE(snapshotFiles(recovery).size(), 3);
        QFile file(savedPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        savedBytes = file.readAll();
        file.close();
        QVERIFY(ws.activateTab(2));  // the user is looking at "Quiet"

        auto* question = app.root->findChild<QObject*>(QStringLiteral("saveChangesDialog"));
        QVERIFY(question);
        auto press = [&](const char* name) {
            auto* button = findInWindow(app.window, QString::fromLatin1(name));
            if (!button) { qWarning("button %s not found", name); return; }
            click(app.window, button);
        };

        // Close the application: the first modified project is asked about.
        app.window->close();
        QTRY_VERIFY(question->property("opened").toBool());
        QVERIFY(app.window->isVisible());
        QCOMPARE(ws.document()->title(), QStringLiteral("Saved"));
        // "Don't Save" for it...
        press("saveChangesDiscardButton");
        // ...and the next one is asked about; nothing has been closed or
        // discarded on the strength of the first answer.
        QTRY_COMPARE(ws.document()->title(), QStringLiteral("Loose"));
        QTRY_VERIFY(question->property("opened").toBool());
        QCOMPARE(ws.projectTabs().size(), 3);
        QCOMPARE(snapshotFiles(recovery).size(), 3);
        // Cancel on the second question cancels the whole close.
        press("saveChangesCancelButton");
        QTRY_VERIFY(!question->property("visible").toBool());
        QTest::qWait(50);
        QVERIFY(app.window->isVisible());
        QCOMPARE(ws.projectTabs().size(), 3);
        QCOMPARE(snapshotFiles(recovery).size(), 3);
        QCOMPARE(ws.document()->title(), QStringLiteral("Quiet"));  // back where the user was
        QVERIFY(ws.activateTab(0));
        QVERIFY(ws.document()->isModified());
        QCOMPARE(pixel(ws, {2, 2}), 0xff00ff00U);
        QVERIFY(ws.history()->canUndo());
        QVERIFY(ws.activateTab(1));
        QVERIFY(ws.document()->isModified());
        QCOMPARE(pixel(ws, {1, 1}), 0xff0000ffU);

        // Close again. The earlier "Don't Save" is not remembered: both
        // projects are asked about again. This time: Save, then Don't Save.
        app.window->close();
        QTRY_VERIFY(question->property("opened").toBool());
        QCOMPARE(ws.document()->title(), QStringLiteral("Saved"));
        press("saveChangesSaveButton");
        QTRY_COMPARE(ws.document()->title(), QStringLiteral("Loose"));
        QTRY_VERIFY(question->property("opened").toBool());
        QVERIFY(app.window->isVisible());
        press("saveChangesDiscardButton");
        QTRY_VERIFY(!app.window->isVisible());
        // Saved was written; nothing is left in recovery after a clean exit.
        QFile after(savedPath);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QVERIFY(after.readAll() != savedBytes);
        QVERIFY(snapshotFiles(recovery).isEmpty());
        QVERIFY(app.messages.isEmpty());
    }
    // Next start: no recovery question, and the saved project is back.
    Running next;
    QVERIFY(start(next, QStringLiteral("WorkspaceUiClose"), recovery, session));
    auto* dialog = next.root->findChild<QObject*>(QStringLiteral("recoveryChoiceDialog"));
    QVERIFY(dialog);
    QVERIFY(!next.parts.workspace.recoveryAvailable());
    QVERIFY(!dialog->property("visible").toBool());
    QTRY_COMPARE(next.parts.workspace.projectTabs().size(), 1);
    QCOMPARE(next.parts.workspace.currentFile(), savedPath);
    QCOMPARE(pixel(next.parts.workspace, {2, 2}), 0xff00ff00U);
    QVERIFY(!next.parts.workspace.document()->isModified());
}

void TestWorkspaceUi::tabCloseTouchesOnlyThatProject()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    Running app;
    QVERIFY(start(app, QStringLiteral("WorkspaceUiTabClose"), recovery, dir.filePath(QStringLiteral("session.json"))));
    auto& ws = app.parts.workspace;
    for (const auto& name : {QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")}) {
        QVERIFY(ws.newProject(name, 64, 64, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, 0xffff0000U));
    }
    QVERIFY(ws.persistWorkspace());
    QCOMPARE(snapshotFiles(recovery).size(), 3);
    auto* question = app.root->findChild<QObject*>(QStringLiteral("saveChangesDialog"));
    QVERIFY(question);

    // "×" on a modified tab asks first; Cancel changes nothing.
    auto* closeTwo = tabItem(app.window, QStringLiteral("projectTabClose"), 1);
    QVERIFY(closeTwo);
    click(app.window, closeTwo);
    QTRY_VERIFY(question->property("opened").toBool());
    QCOMPARE(ws.document()->title(), QStringLiteral("Two"));
    click(app.window, findInWindow(app.window, QStringLiteral("saveChangesCancelButton")));
    QTRY_VERIFY(!question->property("visible").toBool());
    QCOMPARE(ws.projectTabs().size(), 3);
    QCOMPARE(snapshotFiles(recovery).size(), 3);

    // Don't Save closes that tab and removes that project's snapshot only.
    closeTwo = tabItem(app.window, QStringLiteral("projectTabClose"), 1);
    QVERIFY(closeTwo);
    click(app.window, closeTwo);
    QTRY_VERIFY(question->property("opened").toBool());
    QCOMPARE(ws.document()->title(), QStringLiteral("Two"));
    click(app.window, findInWindow(app.window, QStringLiteral("saveChangesDiscardButton")));
    QTRY_COMPARE(ws.projectTabs().size(), 2);
    QCOMPARE(tabTitle(ws, 0), QStringLiteral("One"));
    QCOMPARE(tabTitle(ws, 1), QStringLiteral("Three"));
    QCOMPARE(snapshotFiles(recovery).size(), 2);
    QVERIFY(app.window->isVisible());
    for (int i : {0, 1}) {
        QVERIFY(ws.activateTab(i));
        QVERIFY(ws.document()->isModified());
        QCOMPARE(pixel(ws, {1, 1}), 0xffff0000U);
        QVERIFY(ws.history()->canUndo());
    }
    // An unmodified project closes without a question and takes nothing
    // else with it.
    QVERIFY(ws.newProject(QStringLiteral("Blank"), 64, 64, true, QStringLiteral("white"), 72));
    QTRY_VERIFY(!question->property("visible").toBool());
    auto* closeBlank = tabItem(app.window, QStringLiteral("projectTabClose"), 2);
    QVERIFY(closeBlank);
    click(app.window, closeBlank);
    QTRY_COMPARE(ws.projectTabs().size(), 2);
    QVERIFY(!question->property("visible").toBool());
    QCOMPARE(snapshotFiles(recovery).size(), 2);
    QCOMPARE(tabTitle(ws, 0), QStringLiteral("One"));
    QCOMPARE(tabTitle(ws, 1), QStringLiteral("Three"));
}

void TestWorkspaceUi::sessionRestoresProjectsAndLayoutAtStartup()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    const QString session = dir.filePath(QStringLiteral("session.json"));
    const QString first = dir.filePath(QStringLiteral("first.koma"));
    const QString second = dir.filePath(QStringLiteral("second.koma"));
    qreal firstZoom = 0;
    QPointF firstCenter;
    {
        Running app;
        QVERIFY(start(app, QStringLiteral("WorkspaceUiSession"), recovery, session));
        auto& ws = app.parts.workspace;
        auto* canvas = qobject_cast<CanvasViewport*>(findInWindow(app.window, QStringLiteral("canvasViewport")));
        QVERIFY(canvas);
        QVERIFY(ws.newProject(QStringLiteral("first"), 400, 300, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, 0xffff0000U));
        QVERIFY(ws.saveToPath(first));
        QTRY_VERIFY(ws.activeViewStored());
        QVERIFY(QMetaObject::invokeMethod(canvas, "zoomAt", Q_ARG(qreal, 20.0), Q_ARG(qreal, 20.0), Q_ARG(qreal, 4.0)));
        firstZoom = canvas->zoom();
        firstCenter = canvas->viewCenter();
        ws.setActiveTool(QStringLiteral("eraser"));
        QVERIFY(ws.newProject(QStringLiteral("second"), 300, 300, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, 0xff0000ffU));
        QVERIFY(ws.saveToPath(second));
        QVERIFY(ws.activateTab(0));
        QTRY_COMPARE(canvas->document(), static_cast<QObject*>(ws.document()));
        // Layout changes made through the application's own panel manager
        // and the dock handle.
        app.parts.panels.setVisible(QStringLiteral("timeline"), true);
        app.parts.panels.setVisible(QStringLiteral("color"), false);
        auto* handle = findInWindow(app.window, QStringLiteral("rightDockHandle"));
        QVERIFY(handle);
        const QPoint grab = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
        QTest::mousePress(app.window, Qt::LeftButton, Qt::NoModifier, grab);
        QTest::mouseMove(app.window, grab - QPoint(40, 0));
        QTest::mouseMove(app.window, grab - QPoint(80, 0));
        QTest::mouseRelease(app.window, Qt::LeftButton, Qt::NoModifier, grab - QPoint(80, 0));
        QTRY_COMPARE(app.root->property("rightDockWidth").toInt(), 340);
        app.window->close();  // nothing is modified: no question
        QTRY_VERIFY(!app.window->isVisible());
    }
    QVERIFY(QFile::exists(session));

    Running app;
    QVERIFY(start(app, QStringLiteral("WorkspaceUiSession"), recovery, session));
    auto& ws = app.parts.workspace;
    QTRY_COMPARE(ws.projectTabs().size(), 2);
    QVERIFY(app.messages.isEmpty());
    // The project that was active is active, with its own zoom, position
    // and tool on the real canvas.
    QCOMPARE(ws.currentFile(), first);
    QCOMPARE(ws.activeTool(), QStringLiteral("eraser"));
    auto* canvas = qobject_cast<CanvasViewport*>(findInWindow(app.window, QStringLiteral("canvasViewport")));
    QVERIFY(canvas);
    QTRY_COMPARE(canvas->document(), static_cast<QObject*>(ws.document()));
    QTRY_COMPARE(canvas->zoom(), firstZoom);
    QVERIFY(qAbs(canvas->viewCenter().x() - firstCenter.x()) < 0.01);
    QVERIFY(qAbs(canvas->viewCenter().y() - firstCenter.y()) < 0.01);
    QCOMPARE(pixel(ws, {1, 1}), 0xffff0000U);
    QVERIFY(!ws.document()->isModified());
    QVERIFY(!ws.history()->canUndo());
    // Layout.
    QVERIFY(app.parts.panels.isVisible(QStringLiteral("timeline")));
    QVERIFY(!app.parts.panels.isVisible(QStringLiteral("color")));
    QVERIFY(app.parts.panels.isVisible(QStringLiteral("layers")));
    QCOMPARE(app.root->property("rightDockWidth").toInt(), 340);
    // A normal start asks no recovery question.
    QVERIFY(!app.root->findChild<QObject*>(QStringLiteral("recoveryChoiceDialog"))->property("visible").toBool());
}

void TestWorkspaceUi::toolbarOpenAndSaveAreConnected()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("toolbar.koma"));
    Running app;
    QVERIFY(start(app, QStringLiteral("WorkspaceUiToolbar"), dir.filePath(QStringLiteral("recovery")),
                  dir.filePath(QStringLiteral("session.json"))));
    auto& ws = app.parts.workspace;
    QVERIFY(ws.newProject(QStringLiteral("toolbar"), 64, 64, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, 0xffff0000U));
    QVERIFY(ws.saveToPath(path));
    QVERIFY(paint(ws, {2, 2}, 0xff00ff00U));
    QVERIFY(ws.document()->isModified());
    auto* save = findInWindow(app.window, QStringLiteral("saveToolbarButton"));
    auto* open = findInWindow(app.window, QStringLiteral("openToolbarButton"));
    QVERIFY(save && open);
    click(app.window, save);
    QTRY_VERIFY(!ws.document()->isModified());
    QVERIFY(app.messages.isEmpty());
    // The Open button reaches the same request as File > Open. (The native
    // file dialog itself cannot be driven from an offscreen test.)
    QSignalSpy clicked(open, SIGNAL(clicked()));
    click(app.window, open);
    QCOMPARE(clicked.size(), 1);
}

QTEST_MAIN(TestWorkspaceUi)
#include "TestWorkspaceUi.moc"
