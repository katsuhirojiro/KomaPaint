#include "canvas/SparseTileStore.h"
#include "drawing/BrushEngine.h"
#include "drawing/StrokeStabilizer.h"
#include "input/PointerInput.h"
#include "material/MaterialEngine.h"
#include "material/MaterialTileStore.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

using namespace komapaint;
using namespace komapaint::input;

namespace {
int g_failures = 0;

void check(bool cond, const char* expr, int line)
{
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << expr << '\n';
        ++g_failures;
    }
}
#define CHECK(expr) check((expr), #expr, __LINE__)

constexpr std::uint64_t kMs = 1'000'000ULL;

RawPointerEvent makePenEvent(PointerPhase phase, double x, double y, double pressure,
                             std::uint64_t tsNs, InputBackend backend = InputBackend::WindowsInk,
                             bool hasTilt = true, double tiltX = 0.0, double tiltY = 0.0,
                             bool hasRot = false, double rotDeg = 0.0,
                             PointerType pt = PointerType::Pen,
                             std::uint32_t buttons = PrimaryButton)
{
    RawPointerEvent ev;
    ev.deviceId = 42;
    ev.pointerId = 42;
    ev.timestampNs = tsNs;
    ev.pointerType = pt;
    ev.phase = phase;
    ev.backend = backend;
    ev.viewX = x;
    ev.viewY = y;
    ev.pressure = pressure;
    ev.tiltX = tiltX;
    ev.tiltY = tiltY;
    ev.rotationDegrees = rotDeg;
    ev.buttons = buttons;
    ev.inProximity = (phase != PointerPhase::Leave);
    ev.capabilities.hasPressure = true;
    ev.capabilities.hasTilt = hasTilt;
    ev.capabilities.hasRotation = hasRot;
    ev.capabilities.hasHover = true;
    ev.capabilities.hasEraser = true;
    ev.capabilities.hasBarrelButtons = true;
    return ev;
}

void testCapabilityDetectionAndNoFabrication()
{
    PointerStrokeArbiter arbiter;

    // Stylus with ONLY position + pressure (no tilt, no rotation, no tangential).
    RawPointerEvent raw;
    raw.deviceId = 7;
    raw.pointerId = 7;
    raw.timestampNs = 100 * kMs;
    raw.pointerType = PointerType::Pen;
    raw.phase = PointerPhase::Down;
    raw.backend = InputBackend::QtNative;
    raw.viewX = 120.25;
    raw.viewY = 88.75;
    raw.pressure = 0.6;
    raw.tiltX = 45.0;            // bogus value from driver even though !hasTilt
    raw.tiltY = -30.0;
    raw.rotationDegrees = 135.0; // bogus value even though !hasRotation
    raw.tangentialPressure = 0.9;
    raw.capabilities.hasPressure = true;
    raw.capabilities.hasTilt = false;
    raw.capabilities.hasRotation = false;
    raw.capabilities.hasTangentialPressure = false;

    const auto res = arbiter.process(raw, 120.25, 88.75);
    CHECK(res.decision == ArbiterDecision::Accept);
    CHECK(res.sample.capabilities.hasPressure);
    CHECK(!res.sample.capabilities.hasTilt);
    CHECK(!res.sample.capabilities.hasRotation);
    CHECK(res.sample.tiltX == 0.0);
    CHECK(res.sample.tiltY == 0.0);
    CHECK(res.sample.tiltMagnitude == 0.0);
    CHECK(res.sample.rotationRadians == 0.0);
    CHECK(res.sample.tangentialPressure == 0.0);

    // Stylus lacking pressure capability must NOT silently become 1.0 max pressure!
    RawPointerEvent noPress = raw;
    noPress.timestampNs = 110 * kMs;
    noPress.phase = PointerPhase::Move;
    noPress.capabilities.hasPressure = false;
    noPress.pressure = 0.0;
    const auto resNoPress = arbiter.process(noPress, 130.0, 90.0);
    CHECK(resNoPress.decision == ArbiterDecision::Accept);
    CHECK(std::abs(resNoPress.sample.pressure - 0.50) < 1e-6);
    CHECK(resNoPress.sample.pressure < 0.99);

    // End that stroke and test Mouse fallback (predictable 1.0).
    noPress.phase = PointerPhase::Up;
    noPress.timestampNs = 120 * kMs;
    arbiter.process(noPress, 130.0, 90.0);
    RawPointerEvent leave = noPress;
    leave.phase = PointerPhase::Leave;
    leave.inProximity = false;
    leave.timestampNs = 125 * kMs;
    arbiter.process(leave, 130.0, 90.0);

    RawPointerEvent mouse;
    mouse.deviceId = 1;
    mouse.pointerId = 1;
    mouse.timestampNs = 500 * kMs;
    mouse.pointerType = PointerType::Mouse;
    mouse.phase = PointerPhase::Down;
    mouse.backend = InputBackend::QtNative;
    mouse.viewX = 10.0;
    mouse.viewY = 20.0;
    mouse.buttons = PrimaryButton;
    mouse.capabilities.hasPressure = false;
    const auto resMouse = arbiter.process(mouse, 10.0, 20.0);
    CHECK(resMouse.decision == ArbiterDecision::Accept);
    CHECK(std::abs(resMouse.sample.pressure - 1.0) < 1e-6);
}

void testPressureCalibrationAndCurves()
{
    PressureCalibration cal;
    cal.minThreshold = 0.05;
    cal.maxCalibration = 0.85;
    cal.gamma = 1.0;
    cal.curveLow = 0.15;
    cal.curveMid = 0.50;
    cal.curveHigh = 0.85;
    cal.sanitize();

    // Below deadzone -> exact 0, no resting nib spike.
    CHECK(cal.normalize(0.0, PointerType::Pen, true) == 0.0);
    CHECK(cal.normalize(0.049, PointerType::Pen, true) == 0.0);

    // At or above maxCalibration -> 1.0.
    CHECK(cal.normalize(0.85, PointerType::Pen, true) == 1.0);
    CHECK(cal.normalize(1.0, PointerType::Pen, true) == 1.0);

    // Monotonicity & smoothness without jumps across 100 steps.
    double prev = -1.0;
    double maxStep = 0.0;
    for (int i = 0; i <= 100; ++i) {
        const double x = static_cast<double>(i) / 100.0;
        const double y = cal.normalize(x, PointerType::Pen, true);
        CHECK(std::isfinite(y) && y >= 0.0 && y <= 1.0);
        CHECK(y >= prev - 1e-9);
        if (prev >= 0.0) maxStep = std::max(maxStep, y - prev);
        prev = y;
    }
    CHECK(maxStep < 0.06);

    // NaN raw pressure never produces NaN or 1.0 spike for stylus.
    const double nanOut = cal.normalize(std::numeric_limits<double>::quiet_NaN(), PointerType::Pen, true);
    CHECK(std::isfinite(nanOut) && nanOut < 0.99);
}

void testDuplicateProtocolAndSynthesizedMouseSuppression()
{
    PointerStrokeArbiter arbiter;
    InputConfig cfg = arbiter.config();
    cfg.windowsMode = WindowsTabletMode::Automatic;
    arbiter.setConfig(cfg);

    // Windows Ink begins stroke.
    const auto d1 = arbiter.process(makePenEvent(PointerPhase::Down, 50, 50, 0.4, 100 * kMs, InputBackend::WindowsInk), 50, 50);
    CHECK(d1.decision == ArbiterDecision::Accept && d1.shouldBeginStroke);

    // Simultaneous WinTab packet from same stroke must be suppressed!
    const auto dupWt = arbiter.process(makePenEvent(PointerPhase::Move, 52, 51, 0.42, 102 * kMs, InputBackend::WinTab), 52, 51);
    CHECK(dupWt.decision == ArbiterDecision::IgnoreDuplicateProtocol);

    // Synthesized mouse move from OS must be suppressed!
    RawPointerEvent synMouse;
    synMouse.deviceId = 99;
    synMouse.pointerId = 99;
    synMouse.timestampNs = 104 * kMs;
    synMouse.pointerType = PointerType::Mouse;
    synMouse.phase = PointerPhase::Move;
    synMouse.backend = InputBackend::QtNative;
    synMouse.viewX = 52;
    synMouse.viewY = 51;
    synMouse.synthesizedMouse = true;
    const auto dupMouse = arbiter.process(synMouse, 52, 51);
    CHECK(dupMouse.decision == ArbiterDecision::IgnoreSynthesizedMouse);

    // Pen up + leave, then immediate synthesized mouse release within window -> still suppressed.
    const auto u1 = arbiter.process(makePenEvent(PointerPhase::Up, 60, 60, 0.0, 120 * kMs, InputBackend::WindowsInk), 60, 60);
    CHECK(u1.decision == ArbiterDecision::Accept && u1.shouldEndStroke);
    synMouse.phase = PointerPhase::Up;
    synMouse.timestampNs = 130 * kMs;
    CHECK(arbiter.process(synMouse, 60, 60).decision == ArbiterDecision::IgnoreSynthesizedMouse);
    CHECK(arbiter.telemetry().droppedDuplicates() >= 3);

    // Explicit WinTab mode accepts WinTab and rejects Windows Ink.
    cfg.windowsMode = WindowsTabletMode::WinTab;
    arbiter.setConfig(cfg);
    arbiter.reset();
    CHECK(arbiter.process(makePenEvent(PointerPhase::Down, 10, 10, 0.5, 500 * kMs, InputBackend::WindowsInk), 10, 10).decision == ArbiterDecision::IgnoreDuplicateProtocol);
    CHECK(arbiter.process(makePenEvent(PointerPhase::Down, 10, 10, 0.5, 505 * kMs, InputBackend::WinTab), 10, 10).decision == ArbiterDecision::Accept);
}

void testBarrelEraserAndMissingReleaseRecovery()
{
    PointerStrokeArbiter arbiter;
    InputConfig cfg = arbiter.config();
    cfg.barrel1Action = BarrelAction::Eyedropper;
    cfg.barrel2Action = BarrelAction::EraserToggle;
    cfg.eraserEndEnabled = true;
    arbiter.setConfig(cfg);

    // Hover with barrel 1 pressed -> triggers Eyedropper without starting stroke.
    const auto h1 = arbiter.process(
        makePenEvent(PointerPhase::Hover, 40, 40, 0.0, 100 * kMs, InputBackend::WindowsInk,
                     true, 10.0, 15.0, false, 0.0, PointerType::Pen, SecondaryButton),
        40, 40);
    CHECK(h1.decision == ArbiterDecision::Accept);
    CHECK(h1.triggeredBarrelAction == BarrelAction::Eyedropper);
    CHECK(!h1.shouldBeginStroke);

    // Release barrel button, press eraser end -> eraserActive true.
    const auto eDown = arbiter.process(
        makePenEvent(PointerPhase::Down, 40, 40, 0.7, 110 * kMs, InputBackend::WindowsInk,
                     true, 0.0, 0.0, false, 0.0, PointerType::Eraser, PrimaryButton),
        40, 40);
    CHECK(eDown.shouldBeginStroke && eDown.sample.eraserActive);

    // Missing release: new Pen Down arrives while Eraser stroke was still open!
    const auto pDown = arbiter.process(
        makePenEvent(PointerPhase::Down, 60, 60, 0.5, 130 * kMs, InputBackend::WindowsInk,
                     true, 0.0, 0.0, false, 0.0, PointerType::Pen, PrimaryButton),
        60, 60);
    CHECK(pDown.shouldEndPreviousStroke && pDown.shouldBeginStroke && !pDown.sample.eraserActive);

    // Out-of-order stale timestamp rejected without corrupting stroke.
    const auto stale = arbiter.process(
        makePenEvent(PointerPhase::Move, 55, 55, 0.5, 125 * kMs), 55, 55);
    CHECK(stale.decision == ArbiterDecision::IgnoreOutOfOrder);

    // Proximity leave during contact recovers cleanly.
    const auto leaveRecover = arbiter.process(
        makePenEvent(PointerPhase::Leave, 70, 70, 0.0, 150 * kMs), 70, 70);
    CHECK(leaveRecover.decision == ArbiterDecision::Accept && leaveRecover.shouldEndStroke);
    CHECK(!arbiter.strokeActive());
    CHECK(arbiter.telemetry().recoveredStrokes() >= 2);

    // Device disconnect mid-stroke recovers cleanly.
    CHECK(arbiter.process(makePenEvent(PointerPhase::Down, 20, 20, 0.6, 200 * kMs), 20, 20).shouldBeginStroke);
    CHECK(arbiter.strokeActive());
    CHECK(arbiter.handleDeviceDisconnected(42));
    CHECK(!arbiter.strokeActive());
}

void testTouchAndPalmRejection()
{
    PointerStrokeArbiter arbiter;
    InputConfig cfg = arbiter.config();
    cfg.touchMode = TouchCanvasMode::StylusOnlyWithFingerNavigation;
    cfg.palmRejection = true;
    arbiter.setConfig(cfg);

    RawPointerEvent touch;
    touch.deviceId = 200;
    touch.pointerId = 10001;
    touch.timestampNs = 100 * kMs;
    touch.pointerType = PointerType::Touch;
    touch.phase = PointerPhase::Down;
    touch.backend = InputBackend::QtNative;
    touch.viewX = 80;
    touch.viewY = 90;

    // Without stylus activity, touch routes to navigation (not painting).
    const auto navRes = arbiter.process(touch, 80, 90);
    CHECK(navRes.decision == ArbiterDecision::Accept);
    CHECK(navRes.shouldRouteTouchNavigation);
    CHECK(!navRes.shouldBeginStroke);

    // Stylus enters proximity -> palm touch within window is rejected.
    arbiter.setProximity(42, true, InputBackend::WindowsInk, 200 * kMs);
    touch.timestampNs = 220 * kMs;
    const auto palmRes = arbiter.process(touch, 80, 90);
    CHECK(palmRes.decision == ArbiterDecision::IgnorePalmRejected);

    // AllowAll mode without stylus activity allows single-finger painting.
    arbiter.setProximity(42, false, InputBackend::WindowsInk, 230 * kMs);
    cfg.touchMode = TouchCanvasMode::AllowAll;
    arbiter.setConfig(cfg);
    touch.timestampNs = 800 * kMs;
    const auto drawTouch = arbiter.process(touch, 80, 90);
    CHECK(drawTouch.decision == ArbiterDecision::Accept && drawTouch.shouldBeginStroke);
}

void testEndToEndDigitalAndMaterialStrokes()
{
    PointerStrokeArbiter arbiter;

    // Digital stroke with tilt, rotation, high-frequency variable pressure, and eraser.
    SparseTileStore digitalStore(256, 256, 64);
    BrushDefinition digDef;
    digDef.size = 18.0;
    digDef.color = 0xff2040a0U;
    digDef.tip = BrushTip::Bristle;
    digDef.aspectRatio = 2.0;
    digDef.pressureSize = 0.8;
    digDef.pressureOpacity = 0.9;
    digDef.pressureFlow = 0.5;
    digDef.tiltSize = 0.6;
    digDef.tiltAngle = true;
    digDef.stylusRotation = true;
    BrushStroke digStroke(digitalStore, digDef);

    const std::uint64_t t0 = 1000 * kMs;
    auto r0 = arbiter.process(makePenEvent(PointerPhase::Down, 40, 128, 0.3, t0,
                                           InputBackend::WindowsInk, true, 15.0, -20.0, true, 35.0), 40, 128);
    CHECK(r0.shouldBeginStroke);
    CHECK(digStroke.begin({r0.sample.docX, r0.sample.docY, r0.sample.pressure,
                           r0.sample.tiltX, r0.sample.tiltY, r0.sample.rotationRadians,
                           r0.sample.tangentialPressure, true, true}));

    // 200 high-frequency samples with direction reversal and variable pressure/tilt.
    for (int i = 1; i <= 200; ++i) {
        const double u = static_cast<double>(i) / 200.0;
        const double x = 40.0 + 160.0 * u;
        const double y = 128.0 + 40.0 * std::sin(u * 9.42);
        const double p = 0.15 + 0.80 * std::sin(u * 3.14159);
        const double tx = -30.0 + 60.0 * u;
        const double ty = 20.0 * std::cos(u * 6.28);
        const double rot = 360.0 * u;
        const auto ev = makePenEvent(PointerPhase::Move, x, y, p, t0 + static_cast<std::uint64_t>(i) * 2 * kMs,
                                     InputBackend::WindowsInk, true, tx, ty, true, rot);
        const auto ri = arbiter.process(ev, x, y);
        CHECK(ri.decision == ArbiterDecision::Accept && ri.shouldExtendStroke);
        CHECK(digStroke.extend({ri.sample.docX, ri.sample.docY, ri.sample.pressure,
                                ri.sample.tiltX, ri.sample.tiltY, ri.sample.rotationRadians,
                                ri.sample.tangentialPressure, true, true}));
    }
    auto rEnd = arbiter.process(makePenEvent(PointerPhase::Up, 200, 128, 0.05, t0 + 402 * kMs), 200, 128);
    CHECK(rEnd.shouldEndStroke);
    CHECK(digStroke.end({rEnd.sample.docX, rEnd.sample.docY, rEnd.sample.pressure,
                         rEnd.sample.tiltX, rEnd.sample.tiltY, rEnd.sample.rotationRadians,
                         rEnd.sample.tangentialPressure, true, true}));
    CHECK(digitalStore.allocatedTiles() > 0);
    CHECK(digitalStore.pixel({120, 128}).value_or(0) != 0 || digitalStore.pixel({80, 160}).value_or(0) != 0);

    // Realistic Material stroke driven by normalized pressure, speed, tilt, rotation.
    material::PigmentLibrary pigments;
    material::MaterialEngine engine(pigments);
    auto presets = material::defaultBrushDefinitions();
    material::BrushDefinition matDef = presets.front();
    matDef.size = 16.0;
    matDef.tiltTransfer = 0.6;
    matDef.tiltFlattening = 0.5;
    matDef.speedTransfer = 0.4;
    material::BrushReservoir reservoir(matDef.effectiveReservoirCapacity());
    CHECK(reservoir.reload(matDef));
    material::MaterialTileStore matStore(256, 256, 64);
    material::MaterialStroke matStroke(matStore, matDef, reservoir, engine,
                                       material::gouacheProfile(), material::coldPressedPaper(), false);

    const std::uint64_t m0 = 2000 * kMs;
    auto mr0 = arbiter.process(makePenEvent(PointerPhase::Down, 50, 80, 0.65, m0,
                                            InputBackend::MacOSTablet, true, 25.0, 10.0, true, 45.0), 50, 80);
    CHECK(matStroke.begin({mr0.sample.docX, mr0.sample.docY,
                           {mr0.sample.pressure, mr0.sample.speedNormalized,
                            mr0.sample.tiltMagnitude, mr0.sample.rotationRadians}}));
    for (int i = 1; i <= 40; ++i) {
        const double x = 50.0 + i * 3.0;
        const double y = 80.0 + std::sin(i * 0.25) * 15.0;
        const auto mri = arbiter.process(
            makePenEvent(PointerPhase::Move, x, y, 0.4 + 0.5 * (i / 40.0),
                         m0 + static_cast<std::uint64_t>(i) * 4 * kMs,
                         InputBackend::MacOSTablet, true, 30.0, -15.0, true, i * 6.0), x, y);
        CHECK(mri.sample.speedNormalized > 0.0);
        CHECK(matStroke.extend({mri.sample.docX, mri.sample.docY,
                                {mri.sample.pressure, mri.sample.speedNormalized,
                                 mri.sample.tiltMagnitude, mri.sample.rotationRadians}}));
    }
    auto mrEnd = arbiter.process(makePenEvent(PointerPhase::Up, 170, 80, 0.1, m0 + 164 * kMs, InputBackend::MacOSTablet), 170, 80);
    CHECK(matStroke.end({mrEnd.sample.docX, mrEnd.sample.docY,
                         {mrEnd.sample.pressure, mrEnd.sample.speedNormalized,
                          mrEnd.sample.tiltMagnitude, mrEnd.sample.rotationRadians}}));
    CHECK(matStore.allocatedTiles() > 0);
}

} // namespace

int main()
{
    testCapabilityDetectionAndNoFabrication();
    testPressureCalibrationAndCurves();
    testDuplicateProtocolAndSynthesizedMouseSuppression();
    testBarrelEraserAndMissingReleaseRecovery();
    testTouchAndPalmRejection();
    testEndToEndDigitalAndMaterialStrokes();

    if (g_failures != 0) {
        std::cerr << g_failures << " assertion(s) failed.\n";
        return 1;
    }
    std::cout << "test_pointer_input: ALL PASSED\n";
    return 0;
}
