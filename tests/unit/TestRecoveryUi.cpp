#include <QtTest/QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>

#include "app/CanvasViewport.h"
#include "app/PanelManager.h"
#include "app/WorkspaceState.h"
#include "core/ApplicationConfig.h"
#include "core/NewProjectSettings.h"
#include "core/Settings.h"
#include "layers/Layer.h"

using namespace komapaint;

// Exercises the recovery dialogs through the real MainWindow.qml instead of
// calling WorkspaceState directly. This does not replace the packaged Windows
// manual test; it only proves that the QML setup/restore wiring is connected.
class TestRecoveryUi : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void firstLaunchShowsSetupDefaultingToTenMinutes();
    void restoresPreviousWorkspaceThroughDialog();
};

void TestRecoveryUi::initTestCase()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
}

namespace {
struct WindowParts {
    WorkspaceState workspace;
    NewProjectSettings project;
    ApplicationConfig config;
    PanelManager panels;
};

QObject* createMainWindow(QQmlEngine& engine, WindowParts& parts)
{
    engine.addImportPath(QStringLiteral("qrc:/"));
    engine.rootContext()->setContextProperty(QStringLiteral("Workspace"), &parts.workspace);
    engine.rootContext()->setContextProperty(QStringLiteral("NewProject"), &parts.project);
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &parts.config);
    engine.rootContext()->setContextProperty(QStringLiteral("Panels"), &parts.panels);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/workspaces/MainWindow.qml")));
    QObject* root = component.create();
    if (!root) qWarning() << component.errorString();
    return root;
}
}  // namespace

void TestRecoveryUi::firstLaunchShowsSetupDefaultingToTenMinutes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings settings(QStringLiteral("KomaPaintTests"), QStringLiteral("RecoveryUiFirstLaunch"));
    settings.setValue(QStringLiteral("recovery/configured"), false);
    settings.setValue(QStringLiteral("recovery/intervalMinutes"), 10);
    settings.setWindowMaximized(false);
    settings.setWindowSize(QSize(1024, 640));

    WindowParts parts;
    QVERIFY(parts.workspace.initializeRecovery(dir.filePath(QStringLiteral("recovery"))));
    QQmlEngine engine;
    QObject* root = createMainWindow(engine, parts);
    QVERIFY(root);
    auto* window = qobject_cast<QQuickWindow*>(root);
    QVERIFY(window);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));

    auto* dialog = root->findChild<QObject*>(QStringLiteral("recoverySettingsDialog"));
    auto* combo = root->findChild<QObject*>(QStringLiteral("recoveryIntervalCombo"));
    QVERIFY(dialog && combo);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QCOMPARE(combo->property("currentIndex").toInt(), 1);  // Every 10 minutes
    QMetaObject::invokeMethod(dialog, "close");
    delete root;
}

void TestRecoveryUi::restoresPreviousWorkspaceThroughDialog()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto recovery = dir.filePath(QStringLiteral("recovery"));
    {
        WorkspaceState previous;
        QVERIFY(previous.initializeRecovery(recovery));
        QVERIFY(previous.newProject(QStringLiteral("Recovered"), 64, 64, true, "white", 72));
        auto* layer = previous.layers()->layerAt(0);
        QVERIFY(previous.history()->beginStroke(layer));
        QCOMPARE(layer->tiles().setPixel({4, 5}, 0xff00ccffU), SparseTileStore::WriteResult::Changed);
        QVERIFY(previous.history()->commitStroke());
        QVERIFY(previous.persistWorkspace());
    }

    Settings settings(QStringLiteral("KomaPaintTests"), QStringLiteral("RecoveryUiRestore"));
    settings.setValue(QStringLiteral("recovery/configured"), true);
    settings.setValue(QStringLiteral("recovery/intervalMinutes"), 5);
    settings.setWindowMaximized(false);
    settings.setWindowSize(QSize(1024, 640));

    WindowParts parts;
    QVERIFY(parts.workspace.initializeRecovery(recovery));
    QVERIFY(parts.workspace.recoveryAvailable());
    QQmlEngine engine;
    QObject* root = createMainWindow(engine, parts);
    QVERIFY(root);
    auto* window = qobject_cast<QQuickWindow*>(root);
    QVERIFY(window);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));

    auto* dialog = root->findChild<QObject*>(QStringLiteral("recoveryChoiceDialog"));
    auto* restore = root->findChild<QObject*>(QStringLiteral("restoreRecoveryButton"));
    QVERIFY(dialog && restore);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(restore, "clicked"));
    QTRY_COMPARE(parts.workspace.projectTabs().size(), 1);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(parts.workspace.document()->title(), QStringLiteral("Recovered"));
    QCOMPARE(parts.workspace.layers()->layerAt(0)->tiles().pixel({4, 5}).value(), 0xff00ccffU);
    QVERIFY(!parts.workspace.history()->canUndo());
    QVERIFY(parts.workspace.recoveryTimerActive());
    QVERIFY(!parts.workspace.recoveryAvailable());
    delete root;
}

QTEST_MAIN(TestRecoveryUi)
#include "TestRecoveryUi.moc"
