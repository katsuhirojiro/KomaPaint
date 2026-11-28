// Levels through the real MainWindow: the Edit menu opens the dialog, Apply
// writes the layer and creates one "Levels" undo step, Cancel restores.
#include <QtTest/QtTest>

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QUrl>
#include <QSGRendererInterface>

#include <memory>

#include "app/CanvasViewport.h"
#include "app/DockModel.h"
#include "app/LevelsController.h"
#include "app/LevelsPreviewItem.h"
#include "app/MixerSurfaceItem.h"
#include "app/PanelManager.h"
#include "app/WorkspaceState.h"
#include "canvas/CanvasDocument.h"
#include "core/ApplicationConfig.h"
#include "core/NewProjectSettings.h"
#include "core/Settings.h"
#include "history/UndoStack.h"
#include "layers/Layer.h"
#include "layers/LayerModel.h"

using namespace komapaint;

class TestLevelsUi : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void menuOpensDialogAndApplyCreatesUndo();
};

void TestLevelsUi::initTestCase()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
    qmlRegisterType<MixerSurfaceItem>("KomaPaint", 1, 0, "MixerSurfaceItem");
    qmlRegisterType<LevelsPreviewItem>("KomaPaint", 1, 0, "LevelsPreview");
}

void TestLevelsUi::menuOpensDialogAndApplyCreatesUndo()
{
    WorkspaceState workspace;
    NewProjectSettings project;
    ApplicationConfig config;
    Settings settings(QStringLiteral("KomaPaintTests"), QStringLiteral("LevelsUi"));
    settings.setWindowMaximized(false);
    settings.setWindowSize(QSize(1100, 760));
    PanelManager panels;
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    engine.rootContext()->setContextProperty(QStringLiteral("Workspace"), &workspace);
    engine.rootContext()->setContextProperty(QStringLiteral("NewProject"), &project);
    engine.rootContext()->setContextProperty(QStringLiteral("Settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &config);
    engine.rootContext()->setContextProperty(QStringLiteral("Panels"), &panels);
    engine.rootContext()->setContextProperty(QStringLiteral("Dock"), panels.dockModel());

    QVERIFY(workspace.newDocument(64, 64, true, Qt::white));
    auto* layer = workspace.layers()->layerAt(workspace.layers()->activeLayerIndex());
    QVERIFY(layer);
    QCOMPARE(layer->tiles().setPixel({10, 10}, 0xff404040U), SparseTileStore::WriteResult::Changed);

    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/workspaces/MainWindow.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    QVERIFY(window);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));

    auto* menu = root->findChild<QObject*>(QStringLiteral("adjustmentsMenu"));
    auto* item = root->findChild<QObject*>(QStringLiteral("adjustmentsLevelsMenuItem"));
    QVERIFY(menu && item);
    QVERIFY(item->property("enabled").toBool());
    QVERIFY(item->property("text").toString().contains(QStringLiteral("Levels")));

    QVERIFY(QMetaObject::invokeMethod(root.get(), "openLevels"));
    auto* dialog = root->findChild<QObject*>(QStringLiteral("levelsDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    auto* levels = workspace.levels();
    QVERIFY(levels && levels->isActive());

    levels->setInputWhite(128);
    levels->previewNow();
    QCOMPARE(layer->tiles().pixel({10, 10}).value(), 0xff808080U);

    auto* apply = qobject_cast<QQuickItem*>(root->findChild<QObject*>(QStringLiteral("levelsApplyButton")));
    QVERIFY(apply && apply->isEnabled() && apply->isVisible());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      apply->mapToScene(QPointF(apply->width() / 2, apply->height() / 2)).toPoint());
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(layer->tiles().pixel({10, 10}).value(), 0xff808080U);
    QVERIFY(workspace.history()->canUndo());
    QCOMPARE(workspace.history()->undoText(), QStringLiteral("Levels"));

    workspace.history()->undo();
    QCOMPARE(layer->tiles().pixel({10, 10}).value(), 0xff404040U);
    workspace.history()->redo();
    QCOMPARE(layer->tiles().pixel({10, 10}).value(), 0xff808080U);

    // A second session cancelled from the dialog must restore the redo result.
    QVERIFY(QMetaObject::invokeMethod(root.get(), "openLevels"));
    QTRY_VERIFY(dialog->property("visible").toBool());
    levels->setInputWhite(200);
    levels->previewNow();
    QVERIFY(layer->tiles().pixel({10, 10}).value() != 0xff808080U);
    auto* cancel = qobject_cast<QQuickItem*>(root->findChild<QObject*>(QStringLiteral("levelsCancelButton")));
    QVERIFY(cancel && cancel->isEnabled() && cancel->isVisible());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      cancel->mapToScene(QPointF(cancel->width() / 2, cancel->height() / 2)).toPoint());
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(layer->tiles().pixel({10, 10}).value(), 0xff808080U);
}

QTEST_MAIN(TestLevelsUi)
#include "TestLevelsUi.moc"
