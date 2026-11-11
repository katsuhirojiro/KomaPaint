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

class TestProjectWorkflow : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void createAndPaint();
    void replaceOpenedArtwork();
};

void TestProjectWorkflow::initTestCase()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
}

void TestProjectWorkflow::createAndPaint()
{
    WorkspaceState workspace;
    NewProjectSettings project;
    ApplicationConfig config;
    Settings settings(QStringLiteral("KomaPaintTests"), QStringLiteral("ProjectWorkflow"));
    settings.setWindowMaximized(false);
    settings.setWindowSize(QSize(1024, 640));
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
    for (const QChar ch : QStringLiteral("workflow test")) {
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
    QCOMPARE(workspace.document()->title(), QStringLiteral("workflow test"));
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
// Open a real saved illustration, add unsaved artwork, then replace it through
// the same toolbar, confirmation, dialog and input path used by a person.
void TestProjectWorkflow::replaceOpenedArtwork()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WorkspaceState workspace;
    NewProjectSettings project;
    ApplicationConfig config;
    Settings settings(QStringLiteral("KomaPaintTests"), QStringLiteral("ReplaceArtwork"));
    settings.setWindowMaximized(false);
    settings.setWindowSize(QSize(1024, 640));
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
    auto* canvas = root->findChild<CanvasViewport*>(QStringLiteral("canvasViewport"));
    auto* toolbar = root->findChild<QQuickItem*>(QStringLiteral("newProjectToolbarButton"));
    auto* dialog = root->findChild<QObject*>(QStringLiteral("newProjectDialog"));
    auto* discard = root->findChild<QObject*>(QStringLiteral("discardForNewDialog"));
    auto* name = root->findChild<QQuickItem*>(QStringLiteral("projectNameInput"));
    auto* width = root->findChild<QQuickItem*>(QStringLiteral("projectWidthInput"));
    auto* height = root->findChild<QQuickItem*>(QStringLiteral("projectHeightInput"));
    auto* dpi = root->findChild<QQuickItem*>(QStringLiteral("projectDpiInput"));
    auto* units = root->findChild<QQuickItem*>(QStringLiteral("projectUnitInput"));
    auto* transparent = root->findChild<QQuickItem*>(QStringLiteral("projectTransparentInput"));
    auto* background = root->findChild<QQuickItem*>(QStringLiteral("projectBackgroundButton"));
    auto* colorDialog = root->findChild<QObject*>(QStringLiteral("projectColorDialog"));
    auto* create = root->findChild<QQuickItem*>(QStringLiteral("createProjectButton"));
    QVERIFY(canvas && toolbar && dialog && discard && name && width && height && dpi && units && transparent && background && colorDialog && create);
    auto click = [window](QQuickItem* item) {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
    };
    auto draw = [window, canvas] {
        const auto p = canvas->mapToScene(QPointF(canvas->width()/2, canvas->height()/2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, p);
        QTest::mouseMove(window, p + QPoint(20, 0), 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, p + QPoint(20, 0));
        QCoreApplication::processEvents();
    };
    QTest::qWait(50);
    draw();
    QTRY_VERIFY(workspace.document()->isModified());
    const auto file = dir.filePath(QStringLiteral("existing.koma"));
    QVERIFY(workspace.saveToPath(file));
    QVERIFY(workspace.openFromPath(file));
    QCOMPARE(workspace.currentFile(), file);
    QCOMPARE(canvas->document(), static_cast<QObject*>(workspace.document()));
    draw();
    QTRY_VERIFY(workspace.document()->isModified());
    auto* previous = workspace.document();
    QVERIFY(workspace.history()->canUndo());
    const int originalTab = workspace.activeTab();
    click(toolbar);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY(!discard->property("visible").toBool());
    QCOMPARE(workspace.document(), previous);
    QCOMPARE(workspace.currentFile(), file);
    // Every active control must have geometry inside the actual window.
    for (auto* control : {name, width, height, dpi, units, transparent, background, create}) {
        QVERIFY(control->isVisible());
        QVERIFY(control->width() > 0 && control->height() > 0);
        const auto r = control->mapRectToScene(QRectF(0, 0, control->width(), control->height()));
        QVERIFY2(QRectF(0, 0, window->width(), window->height()).contains(r), qPrintable(control->objectName()));
    }
    // Disable transparency to exercise opaque background controls.
    click(transparent);
    QTRY_VERIFY(!project.transparentBackground());
    QVERIFY(background->isEnabled());
    click(background);
    QTRY_VERIFY(colorDialog->property("visible").toBool());
    QVERIFY(colorDialog->setProperty("selectedColor", QColor(QStringLiteral("#22aa44"))));
    QVERIFY(QMetaObject::invokeMethod(colorDialog, "accept"));
    QTRY_VERIFY(!colorDialog->property("visible").toBool());
    QCOMPARE(QColor(project.backgroundColor()), QColor(QStringLiteral("#22aa44")));
    window->requestActivate();
    QTRY_VERIFY(window->isActive());
    auto editSpin = [window, &click](QQuickItem* spin, const QString& value) {
        auto* input = qvariant_cast<QQuickItem*>(spin->property("contentItem"));
        if (!input) return false;
        click(input);
        input->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        for (QChar ch : value) QTest::keyClick(window, ch == u'.' ? Qt::Key_Period : static_cast<Qt::Key>(ch.unicode()));
        QTest::keyClick(window, Qt::Key_Return);
        QCoreApplication::processEvents();
        return true;
    };
    QVERIFY(editSpin(dpi, QStringLiteral("144")));
    QTRY_COMPARE(project.dpi(), 144.0);
    QVERIFY(editSpin(width, QStringLiteral("3000")));
    QVERIFY(editSpin(height, QStringLiteral("2000")));
    QTRY_COMPARE(project.widthPixels(), 3000);
    QTRY_COMPARE(project.heightPixels(), 2000);
    click(units);
    QTest::keyClick(window, Qt::Key_End);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(project.units(), QStringLiteral("inches"));
    QTRY_COMPARE(project.widthPixels(), 3000);
    QTRY_COMPARE(project.heightPixels(), 2000);
    QVERIFY(editSpin(width, QStringLiteral("10.50")));
    QTRY_COMPARE(project.widthPixels(), 1512);
    click(units);
    QTest::keyClick(window, Qt::Key_Home);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(project.units(), QStringLiteral("px"));
    QVERIFY(editSpin(width, QStringLiteral("3000")));
    QVERIFY(editSpin(height, QStringLiteral("2000")));
    QTRY_COMPARE(project.widthPixels(), 3000);
    QTRY_COMPARE(project.heightPixels(), 2000);
    // Service rejection must not close the dialog or replace the original.
    project.setBackgroundColor(QStringLiteral("invalid-color"));
    click(create);
    QTRY_VERIFY(!workspace.creationError().isEmpty());
    QVERIFY(dialog->property("visible").toBool());
    QCOMPARE(workspace.document(), previous);
    QCOMPARE(workspace.currentFile(), file);
    project.setBackgroundColor(QStringLiteral("#22aa44"));
    QSignalSpy changed(&workspace, &WorkspaceState::documentChanged);
    click(create);
    QTRY_COMPARE(changed.size(), 1);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(workspace.document() != previous);
    QCOMPARE(workspace.document()->size(), QSize(3000, 2000));
    QCOMPARE(workspace.document()->dpi(), 144.0);
    QVERIFY(!workspace.document()->hasTransparentBackground());
    QCOMPARE(workspace.document()->backgroundColor(), QColor(QStringLiteral("#22aa44")));
    QVERIFY(workspace.currentFile().isEmpty());
    QVERIFY(!workspace.history()->canUndo());
    QCOMPARE(canvas->document(), static_cast<QObject*>(workspace.document()));
    auto* created = workspace.document();
    QVERIFY(workspace.activateTab(originalTab));
    QCOMPARE(workspace.document(), previous);
    QCOMPARE(workspace.currentFile(), file);
    QVERIFY(workspace.document()->isModified());
    QVERIFY(workspace.history()->canUndo());
    QVERIFY(workspace.activateTab(static_cast<int>(workspace.projectTabs().size()) - 1));
    QCOMPARE(workspace.document(), created);
    canvas->refreshNow();
    const QPoint center(qRound(canvas->width()/2), qRound(canvas->height()/2));
    QCOMPARE(canvas->frameSnapshot().pixelColor(center), QColor(QStringLiteral("#22aa44")));
    QVERIFY(!workspace.document()->isModified());
    draw();
    QTRY_VERIFY(workspace.document()->isModified());
    QVERIFY(workspace.history()->canUndo());
    canvas->refreshNow();
    QVERIFY(canvas->frameSnapshot().pixelColor(center) != QColor(QStringLiteral("#22aa44")));
    delete root;
}
QTEST_MAIN(TestProjectWorkflow)
#include "TestProjectWorkflow.moc"
