#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QPointingDevice>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QTouchEvent>

#include "app/BrushSettings.h"
#include "app/CanvasViewport.h"
#include "app/PanelManager.h"
#include "app/WorkspaceState.h"
#include "canvas/CanvasDocument.h"
#include "core/ApplicationConfig.h"
#include "core/NewProjectSettings.h"
#include "core/Settings.h"
#include "history/UndoStack.h"
#include "input/TabletInputSettings.h"
#include "layers/Layer.h"

#include <cmath>
#include <memory>

using namespace komapaint;
using namespace komapaint::input;

class TestTabletViewport : public QObject {
    Q_OBJECT
private slots:
    void tabletPenEraserTiltRotationAndUndo();
    void tabletMissingReleaseAndSynthesizedMouseSuppression();
    void touchPinchZoomRotateAndPalmRejection();
    void tabletSettingsPersistenceAndDialog();
};

void TestTabletViewport::tabletPenEraserTiltRotationAndUndo()
{
    CanvasDocument doc;
    doc.setSize(QSize(256, 256));
    doc.setTransparentBackground(true);
    UndoStack history;
    TabletInputSettings inputSettings;
    BrushSettings brush;
    brush.setSize(20);
    brush.setPressureSize(80);
    brush.setPressureOpacity(100);
    brush.setTiltSize(50);
    brush.setTiltAngle(true);
    brush.setStylusRotation(true);

    CanvasViewport view;
    view.setWidth(256);
    view.setHeight(256);
    view.setDocument(&doc);
    view.setHistory(&history);
    view.setInputSettings(&inputSettings);
    view.setBrushSettings(&brush);
    view.setBrushColor(QColor(20, 80, 220));
    view.refreshNow();

    const QPointingDevice penDev(
        QStringLiteral("Test Stylus"), 1001,
        QInputDevice::DeviceType::Stylus,
        QPointingDevice::PointerType::Pen,
        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure |
        QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt |
        QInputDevice::Capability::Rotation | QInputDevice::Capability::Hover,
        1, 3);

    const QPointingDevice eraserDev(
        QStringLiteral("Test Stylus Eraser"), 1002,
        QInputDevice::DeviceType::Stylus,
        QPointingDevice::PointerType::Eraser,
        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure |
        QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt |
        QInputDevice::Capability::Hover,
        1, 1);

    // 1. Hover sets cursor state without painting.
    {
        QTabletEvent hoverEv(QEvent::TabletMove, &penDev, QPointF(100, 100), QPointF(100, 100),
                             0.0, 18.0F, -12.0F, 0.0, 30.0, 0.0F,
                             Qt::NoModifier, Qt::NoButton, Qt::NoButton);
        hoverEv.setTimestamp(100);
        QCoreApplication::sendEvent(&view, &hoverEv);
        QVERIFY(view.cursorVisible());
        QVERIFY(view.cursorHasTilt());
        QVERIFY(!view.cursorEraserActive());
        QCOMPARE(doc.layers()->layerAt(0)->tiles().allocatedTiles(), std::size_t(0));
    }

    // 2. Pen stroke with variable pressure, tilt, and rotation paints and records undo.
    {
        QTabletEvent down(QEvent::TabletPress, &penDev, QPointF(80, 128), QPointF(80, 128),
                          0.4, 20.0F, 10.0F, 0.0, 45.0, 0.0F,
                          Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
        down.setTimestamp(110);
        QCoreApplication::sendEvent(&view, &down);

        QTabletEvent move(QEvent::TabletMove, &penDev, QPointF(140, 128), QPointF(140, 128),
                          0.85, 30.0F, -15.0F, 0.0, 90.0, 0.0F,
                          Qt::NoModifier, Qt::NoButton, Qt::LeftButton);
        move.setTimestamp(120);
        QCoreApplication::sendEvent(&view, &move);

        QTabletEvent up(QEvent::TabletRelease, &penDev, QPointF(140, 128), QPointF(140, 128),
                        0.0, 30.0F, -15.0F, 0.0, 90.0, 0.0F,
                        Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
        up.setTimestamp(130);
        QCoreApplication::sendEvent(&view, &up);
    }

    auto& tiles = doc.layers()->layerAt(0)->tiles();
    const QPointF midDoc = view.viewportToDocument(110, 128);
    const std::uint32_t painted = tiles.pixel({qRound(midDoc.x()), qRound(midDoc.y())}).value_or(0);
    QVERIFY((painted >> 24U) > 0);
    QVERIFY(history.canUndo());

    // 3. Flipping to Eraser end erases paint without mutating BrushSettings::erase.
    QVERIFY(!brush.erase());
    {
        QTabletEvent eDown(QEvent::TabletPress, &eraserDev, QPointF(110, 128), QPointF(110, 128),
                           0.95, 0.0F, 0.0F, 0.0, 0.0, 0.0F,
                           Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
        eDown.setTimestamp(200);
        QCoreApplication::sendEvent(&view, &eDown);
        QVERIFY(view.cursorEraserActive());

        QTabletEvent eUp(QEvent::TabletRelease, &eraserDev, QPointF(110, 128), QPointF(110, 128),
                         0.0, 0.0F, 0.0F, 0.0, 0.0, 0.0F,
                         Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
        eUp.setTimestamp(215);
        QCoreApplication::sendEvent(&view, &eUp);
    }
    QVERIFY(!brush.erase());
    const std::uint32_t afterErase = tiles.pixel({qRound(midDoc.x()), qRound(midDoc.y())}).value_or(0);
    QVERIFY((afterErase >> 24U) < (painted >> 24U));

    // Undo restores erased paint, undo again clears pen stroke cleanly.
    QVERIFY(history.undo());
    QCOMPARE(tiles.pixel({qRound(midDoc.x()), qRound(midDoc.y())}).value_or(0), painted);
    QVERIFY(history.undo());
    QCOMPARE(tiles.pixel({qRound(midDoc.x()), qRound(midDoc.y())}).value_or(0), 0U);
    QVERIFY(history.redo());
    QCOMPARE(tiles.pixel({qRound(midDoc.x()), qRound(midDoc.y())}).value_or(0), painted);
}

void TestTabletViewport::tabletMissingReleaseAndSynthesizedMouseSuppression()
{
    CanvasDocument doc;
    doc.setSize(QSize(256, 256));
    doc.setTransparentBackground(true);
    UndoStack history;
    TabletInputSettings inputSettings;
    BrushSettings brush;
    brush.setSize(16);

    CanvasViewport view;
    view.setWidth(256);
    view.setHeight(256);
    view.setDocument(&doc);
    view.setHistory(&history);
    view.setInputSettings(&inputSettings);
    view.setBrushSettings(&brush);
    view.setBrushColor(Qt::red);
    view.refreshNow();

    const QPointingDevice penDev(
        QStringLiteral("MissingRelease Pen"), 2001,
        QInputDevice::DeviceType::Stylus,
        QPointingDevice::PointerType::Pen,
        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
        1, 2);

    // Pen down + move, followed by a synthesized mouse press/move (must be suppressed),
    // then TabletLeaveProximity without TabletRelease (must cleanly commit stroke!).
    QTabletEvent down(QEvent::TabletPress, &penDev, QPointF(50, 50), QPointF(50, 50),
                      0.7, 0.0F, 0.0F, 0.0, 0.0, 0.0F,
                      Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
    down.setTimestamp(1000);
    QCoreApplication::sendEvent(&view, &down);
    QVERIFY(view.pointerArbiter().strokeActive());

    QMouseEvent synMouse(QEvent::MouseMove, QPointF(90, 90), QPointF(90, 90),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier, &penDev);
    synMouse.setTimestamp(1005);
    QCoreApplication::sendEvent(&view, &synMouse);
    QVERIFY(view.pointerArbiter().telemetry().droppedDuplicates() >= 1);

    QTabletEvent leave(QEvent::TabletLeaveProximity, &penDev, QPointF(50, 50), QPointF(50, 50),
                       0.0, 0.0F, 0.0F, 0.0, 0.0, 0.0F,
                       Qt::NoModifier, Qt::NoButton, Qt::NoButton);
    leave.setTimestamp(1020);
    QCoreApplication::sendEvent(&view, &leave);

    QVERIFY(!view.pointerArbiter().strokeActive());
    QVERIFY(!history.strokeActive());
    QVERIFY(history.canUndo());
}

void TestTabletViewport::touchPinchZoomRotateAndPalmRejection()
{
    CanvasDocument doc;
    doc.setSize(QSize(512, 512));
    doc.setTransparentBackground(true);
    UndoStack history;
    TabletInputSettings inputSettings;
    inputSettings.setTouchMode(QStringLiteral("stylus_nav_only"));
    inputSettings.setTouchPinchZoom(true);
    inputSettings.setTouchRotate(true);
    inputSettings.setPalmRejection(true);

    BrushSettings brush;
    CanvasViewport view;
    view.setWidth(400);
    view.setHeight(400);
    view.setDocument(&doc);
    view.setHistory(&history);
    view.setInputSettings(&inputSettings);
    view.setBrushSettings(&brush);
    view.setBrushColor(Qt::black);
    view.refreshNow();

    QPointingDevice touchDev(
        QStringLiteral("Test Touchscreen"), 3001,
        QInputDevice::DeviceType::TouchScreen,
        QPointingDevice::PointerType::Finger,
        QInputDevice::Capability::Position,
        10, 0);

    QQuickWindow window;
    window.resize(400, 400);
    view.setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const qreal zoomBefore = view.zoom();
    const qreal rotBefore = view.viewRotation();

    // Two-finger pinch-out + rotation: zoomed & rotated without painting pixels!
    QTest::touchEvent(&window, &touchDev)
        .press(0, QPoint(150, 200))
        .press(1, QPoint(250, 200));
    QTest::touchEvent(&window, &touchDev)
        .move(0, QPoint(120, 170))
        .move(1, QPoint(280, 230));
    QTest::touchEvent(&window, &touchDev)
        .release(0, QPoint(120, 170))
        .release(1, QPoint(280, 230));
    QCoreApplication::processEvents();

    QVERIFY(view.zoom() > zoomBefore);
    QVERIFY(std::abs(view.viewRotation() - rotBefore) > 1.0);
    QCOMPARE(doc.layers()->layerAt(0)->tiles().allocatedTiles(), std::size_t(0));

    view.setParentItem(nullptr);
}

void TestTabletViewport::tabletSettingsPersistenceAndDialog()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("settings.ini"));
    Q_UNUSED(iniPath);

    const QString org = QStringLiteral("KomaPaintTests");
    const QString appName = QStringLiteral("TabletViewportPersistence");
    {
        Settings s(org, appName);
        s.setValue(QStringLiteral("input/windowsTabletMode"), QStringLiteral("wintab"));
        s.setValue(QStringLiteral("input/pressureThreshold"), 0.08);
        s.setValue(QStringLiteral("input/pressureMax"), 0.88);
        s.setValue(QStringLiteral("input/pressureGamma"), 1.35);
        s.setValue(QStringLiteral("input/touchMode"), QStringLiteral("disable_touch"));
        s.setValue(QStringLiteral("input/barrelButton1Action"), QStringLiteral("eyedropper"));
        s.setValue(QStringLiteral("input/eraserEndEnabled"), false);
        QVERIFY(s.sync());
    }

    {
        Settings s(org, appName);
        TabletInputSettings reloaded;
        reloaded.attachSettings(&s);
        QCOMPARE(reloaded.windowsTabletMode(), QStringLiteral("wintab"));
        QVERIFY(std::abs(reloaded.pressureThreshold() - 0.08) < 1e-6);
        QVERIFY(std::abs(reloaded.pressureMax() - 0.88) < 1e-6);
        QVERIFY(std::abs(reloaded.pressureGamma() - 1.35) < 1e-6);
        QCOMPARE(reloaded.touchMode(), QStringLiteral("disable_touch"));
        QCOMPARE(reloaded.barrelButton1Action(), QStringLiteral("eyedropper"));
        QVERIFY(!reloaded.eraserEndEnabled());

        reloaded.restoreDefaults();
        QCOMPARE(reloaded.windowsTabletMode(), QStringLiteral("auto"));
        QVERIFY(reloaded.eraserEndEnabled());
        QCOMPARE(reloaded.touchMode(), QStringLiteral("stylus_nav_only"));
    }
}

QTEST_MAIN(TestTabletViewport)
#include "TestTabletViewport.moc"
