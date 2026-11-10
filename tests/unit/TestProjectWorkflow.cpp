#include <QtTest/QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include "app/CanvasViewport.h"
#include "app/PanelManager.h"
#include "app/WorkspaceState.h"
#include "core/ApplicationConfig.h"
#include "core/NewProjectSettings.h"
#include "core/Settings.h"
#include "layers/Layer.h"

using namespace komapaint;

class TestProjectWorkflow : public QObject {
    Q_OBJECT
private slots:
    void createAndPaint();
};

void TestProjectWorkflow::createAndPaint()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
    WorkspaceState workspace;
    NewProjectSettings project;
    ApplicationConfig config;
    Settings settings(QStringLiteral("KomaPaintTests"), QStringLiteral("ProjectWorkflow"));
    settings.setWindowMaximized(false);
    PanelManager panels;
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    engine.rootContext()->setContextProperty(QStringLiteral("Workspace"), &workspace);
    engine.rootContext()->setContextProperty(QStringLiteral("NewProject"), &project);
    engine.rootContext()->setContextProperty(QStringLiteral("Settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &config);
    engine.rootContext()->setContextProperty(QStringLiteral("Panels"), &panels);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/workspaces/MainWindow.qml")));
    QObject* root = component.create();
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(root);
    QVERIFY(window);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* toolbar = root->findChild<QQuickItem*>(QStringLiteral("newProjectToolbarButton"));
    auto* dialog = root->findChild<QObject*>(QStringLiteral("newProjectDialog"));
    auto* create = root->findChild<QQuickItem*>(QStringLiteral("createProjectButton"));
    auto* name = root->findChild<QQuickItem*>(QStringLiteral("projectNameInput"));
    auto* canvas = root->findChild<CanvasViewport*>(QStringLiteral("canvasViewport"));
    QVERIFY(toolbar && dialog && create && name && canvas);
    auto click = [window](QQuickItem* item) {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
    };
    click(toolbar);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QTRY_VERIFY(create->isEnabled());
    QVERIFY(create->width() > 0 && create->height() > 0);
    // Exercise editing and reactive validation rather than calling C++ setters.
    click(name);
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Backspace);
    QTRY_VERIFY(!create->isEnabled());
    for (const QChar ch : QStringLiteral("Workflow Test")) {
        const auto key = ch == u' ' ? Qt::Key_Space : static_cast<Qt::Key>(ch.toUpper().unicode());
        QTest::keyClick(window, key, ch.isUpper() ? Qt::ShiftModifier : Qt::NoModifier);
    }
    QTRY_VERIFY(create->isEnabled());
    auto* previous = workspace.document();
    QSignalSpy changed(&workspace, &WorkspaceState::documentChanged);
    click(create);
    QTRY_COMPARE(changed.size(), 1);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(workspace.document() != previous);
    QCOMPARE(workspace.document()->title(), QStringLiteral("Workflow Test"));
    QCOMPARE(workspace.document()->width(), 2550);
    QCOMPARE(workspace.document()->height(), 3300);
    QCOMPARE(canvas->document(), static_cast<QObject*>(workspace.document()));
    canvas->refreshNow();
    QVERIFY(!canvas->frameSnapshot().isNull());
    const QPointF center(canvas->width()/2, canvas->height()/2);
    const auto docPoint = canvas->viewportToDocument(center.x(), center.y());
    const auto pos = canvas->mapToScene(center).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseMove(window, pos + QPoint(20, 0), 20);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, pos + QPoint(20, 0));
    QTRY_VERIFY(workspace.document()->isModified());
    QVERIFY(workspace.layers()->layerAt(0)->tiles().pixel(
        {static_cast<int>(docPoint.x()), static_cast<int>(docPoint.y())}).value() != 0);
    QVERIFY(workspace.history()->canUndo());
    delete root;
}
QTEST_MAIN(TestProjectWorkflow)
#include "TestProjectWorkflow.moc"
