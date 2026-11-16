// TEMPORARY developer harness: renders the real MainWindow offscreen and writes
// PNGs so the interface can be inspected visually. Not part of the test suite's
// intent; deleted after the design pass.
#include <QtTest/QtTest>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>

#include <memory>

#include <QDebug>

namespace {

// QML prints binding errors and load failures through the message handler. The
// design pass needs to see them, so they are collected and printed at the end
// rather than scrolling past during the run.
QStringList g_messages;
QtMessageHandler g_previous = nullptr;

void collectMessage(QtMsgType type, const QMessageLogContext& context, const QString& text)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        g_messages.append(text);
    if (g_previous) g_previous(type, context, text);
}

}

#include "app/CanvasViewport.h"
#include "app/DockModel.h"
#include "app/MixerSurfaceItem.h"
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

QQuickItem* findVisual(QQuickItem* from, const QString& name)
{
    if (!from) return nullptr;
    if (from->objectName() == name) return from;
    for (auto* child : from->childItems())
        if (auto* found = findVisual(child, name)) return found;
    return nullptr;
}

}

class TestShots : public QObject
{
    Q_OBJECT
private slots:
    void capture();
};

void TestShots::capture()
{
    g_previous = qInstallMessageHandler(collectMessage);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
    qmlRegisterType<MixerSurfaceItem>("KomaPaint", 1, 0, "MixerSurfaceItem");

    const QString dir = qEnvironmentVariable("SHOT_DIR", QStringLiteral("/tmp/shots"));
    const int width = qEnvironmentVariableIntValue("SHOT_W") > 0 ? qEnvironmentVariableIntValue("SHOT_W") : 1280;
    const int height = qEnvironmentVariableIntValue("SHOT_H") > 0 ? qEnvironmentVariableIntValue("SHOT_H") : 900;
    const QString theme = qEnvironmentVariable("SHOT_THEME", QStringLiteral("dark"));
    QDir().mkpath(dir);

    QTemporaryDir tmp;
    Settings settings(QStringLiteral("KomaPaintShots"), QStringLiteral("capture"));
    settings.setValue(QStringLiteral("recovery/configured"), true);
    settings.setValue(QStringLiteral("recovery/intervalMinutes"), 10);
    settings.setWindowMaximized(false);
    settings.setWindowSize(QSize(width, height));
    settings.setValue(QStringLiteral("workspace/layout"), QString());
    settings.setValue(QStringLiteral("ui/theme"), theme);

    Parts parts;
    parts.panels.dockModel()->setSettings(&settings);
    QVERIFY(parts.workspace.initializeRecovery(tmp.filePath(QStringLiteral("recovery"))));
    QVERIFY(parts.workspace.initializeSession(tmp.filePath(QStringLiteral("session"))));
    QVERIFY(parts.workspace.newProject(QStringLiteral("Untitled"), 1400, 1000, false, QStringLiteral("white"), 144));

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    auto* context = engine.rootContext();
    context->setContextProperty(QStringLiteral("Settings"), &settings);
    context->setContextProperty(QStringLiteral("Workspace"), &parts.workspace);
    context->setContextProperty(QStringLiteral("NewProject"), &parts.project);
    context->setContextProperty(QStringLiteral("App"), &parts.config);
    context->setContextProperty(QStringLiteral("Panels"), &parts.panels);
    context->setContextProperty(QStringLiteral("Dock"), parts.panels.dockModel());

    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/workspaces/MainWindow.qml")));
    std::unique_ptr<QObject> root(component.create());
    if (!root) { qWarning() << component.errorString(); QVERIFY(root != nullptr); }
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    QVERIFY(window);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTest::qWait(400);

    // Some artwork on two layers, so the canvas and the layer stack are not empty.
    for (int i = 0; i < 2; ++i) {
        auto* layer = parts.workspace.layers()->layerAt(0);
        if (!parts.workspace.history()->beginStroke(layer)) break;
        for (int step = 0; step < 260; ++step) {
            const int x = 120 + step * 4;
            const int y = 420 + int(140 * std::sin(step * 0.08));
            layer->tiles().setPixel({x, y}, 0xff3e6f9eU);
            layer->tiles().setPixel({x, y + 6}, 0xff3e6f9eU);
        }
        parts.workspace.history()->commitStroke();
        if (i == 0) parts.workspace.layers()->addLayer();
        QTest::qWait(30);
    }
    QTest::qWait(200);

    // Fit the document to the viewport, the way opening a project does; a
    // screenshot at the raw 1:1 zoom shows a corner of the canvas at 10000%.
    if (auto* canvas = window->findChild<QObject*>(QStringLiteral("canvasArea")))
        QMetaObject::invokeMethod(canvas, "fitCanvas");
    QTest::qWait(250);

    const QString base = qEnvironmentVariable("SHOT_NAME", QStringLiteral("shot"));
    window->grabWindow().save(dir + "/" + base + "-default.png");

    // Docking: float the colour panel, collapse the left area, resize.
    auto* dock = parts.panels.dockModel();
    dock->floatPanel(QStringLiteral("color"), 300, 200, 340, 420);
    QTest::qWait(300);
    window->grabWindow().save(dir + "/" + base + "-floating.png");
    // The floated panel lives in its own top-level window, so it is missing
    // from the shell grab above; capture it separately.
    const QVariantList floating = dock->floating();
    const int floatingId = floating.isEmpty()
                           ? -1 : floating.first().toMap().value(QStringLiteral("id")).toInt();
    for (QWindow* top : QGuiApplication::topLevelWindows()) {
        auto* quick = qobject_cast<QQuickWindow*>(top);
        if (!quick || quick == window || !quick->isVisible()) continue;
        quick->grabWindow().save(dir + "/" + base + "-floatingpanel.png");
    }
    if (floatingId >= 0) dock->dockBack(floatingId);
    QTest::qWait(150);
    window->grabWindow().save(dir + "/" + base + "-docked-back.png");
    dock->setAreaCollapsed(QStringLiteral("left"), true);
    QTest::qWait(300);
    window->grabWindow().save(dir + "/" + base + "-collapsed.png");
    dock->setAreaCollapsed(QStringLiteral("left"), false);
    QTest::qWait(200);

    // Smaller window.
    window->resize(width > 1100 ? 1024 : 900, 720);
    QTest::qWait(400);
    window->grabWindow().save(dir + "/" + base + "-small.png");

    // Light theme.
    window->resize(width, height);
    QTest::qWait(200);
    settings.setValue(QStringLiteral("ui/theme"), QStringLiteral("light"));
    QMetaObject::invokeMethod(window, "loadThemePreferences");
    QTest::qWait(400);
    window->grabWindow().save(dir + "/" + base + "-light.png");

    // Dialogs: the shared frame is used by every message and form dialog, so
    // two of them are worth a look next to the shell.
    settings.setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
    QMetaObject::invokeMethod(window, "loadThemePreferences");
    QTest::qWait(200);
    if (auto* about = window->findChild<QObject*>(QStringLiteral("aboutDialog"))) {
        QMetaObject::invokeMethod(about, "open");
        QTest::qWait(250);
        window->grabWindow().save(dir + "/" + base + "-about.png");
        QMetaObject::invokeMethod(about, "close");
    }
    if (auto* newProject = window->findChild<QObject*>(QStringLiteral("newProjectDialog"))) {
        QMetaObject::invokeMethod(newProject, "open");
        QTest::qWait(300);
        window->grabWindow().save(dir + "/" + base + "-newproject.png");
        QMetaObject::invokeMethod(newProject, "close");
    }
    QTest::qWait(150);

    qInstallMessageHandler(g_previous);
    qInfo() << "component errors:" << component.errors();
    qInfo() << "interface warnings:" << g_messages.size();
    for (const QString& line : g_messages) qInfo().noquote() << "  " << line;
}

QTEST_MAIN(TestShots)
#include "TestShots.moc"
