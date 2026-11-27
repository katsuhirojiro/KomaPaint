#include "TabletInputSettings.h"
#include "QtPointerAdapter.h"
#include "WinTabBackend.h"
#include "../core/Settings.h"

#include <algorithm>
#include <cmath>
#include <QStringList>

namespace komapaint::input {
namespace {

QString sanitizeWindowsMode(const QString& s)
{
    const QString lower = s.trimmed().toLower();
    if (lower == QStringLiteral("windows_ink") || lower == QStringLiteral("wintab")) return lower;
    return QStringLiteral("auto");
}

QString sanitizeTouchMode(const QString& s)
{
    const QString lower = s.trimmed().toLower();
    if (lower == QStringLiteral("allow_all") || lower == QStringLiteral("disable_touch")) return lower;
    return QStringLiteral("stylus_nav_only");
}

QString sanitizeBarrelAction(const QString& s, const QString& fallback)
{
    const QString lower = s.trimmed().toLower();
    if (lower == QStringLiteral("none") || lower == QStringLiteral("right_click") ||
        lower == QStringLiteral("pan") || lower == QStringLiteral("eyedropper") ||
        lower == QStringLiteral("eraser") || lower == QStringLiteral("undo")) {
        return lower;
    }
    return fallback;
}

double clampFinite(double v, double lo, double hi, double fallback) noexcept
{
    if (!std::isfinite(v)) return fallback;
    return std::clamp(v, lo, hi);
}

} // namespace

TabletInputSettings::TabletInputSettings(QObject* parent)
    : QObject(parent)
{
    refreshDevices();
    m_liveDiagnostics = QVariantMap{
        {QStringLiteral("backend"), QStringLiteral("None yet")},
        {QStringLiteral("pointerType"), QStringLiteral("None")},
        {QStringLiteral("pressure"), 0.0},
        {QStringLiteral("rawPressure"), 0.0},
        {QStringLiteral("tiltX"), 0.0},
        {QStringLiteral("tiltY"), 0.0},
        {QStringLiteral("rotationDeg"), 0.0},
        {QStringLiteral("capabilities"), QStringLiteral("Waiting for pointer input")},
        {QStringLiteral("sampleRateHz"), 0.0},
        {QStringLiteral("medianUs"), 0.0},
        {QStringLiteral("p95Us"), 0.0},
        {QStringLiteral("droppedDuplicates"), 0},
        {QStringLiteral("recoveredStrokes"), 0}
    };
}

void TabletInputSettings::attachSettings(Settings* settings)
{
    m_settings = settings;
    if (m_settings) loadFromSettings();
}

void TabletInputSettings::loadFromSettings()
{
    if (!m_settings) return;
    m_windowsTabletMode = sanitizeWindowsMode(m_settings->value(QStringLiteral("input/windowsTabletMode"), QStringLiteral("auto")).toString());
    m_windowsDisableWmPointer = m_settings->value(QStringLiteral("input/windowsDisableWmPointer"), false).toBool();
    m_pressureThreshold = clampFinite(m_settings->value(QStringLiteral("input/pressureThreshold"), 0.02).toDouble(), 0.0, 0.30, 0.02);
    m_pressureMax = clampFinite(m_settings->value(QStringLiteral("input/pressureMax"), 1.0).toDouble(), std::max(0.40, m_pressureThreshold + 0.15), 1.0, 1.0);
    m_pressureGamma = clampFinite(m_settings->value(QStringLiteral("input/pressureGamma"), 1.0).toDouble(), 0.35, 2.80, 1.0);
    m_pressureCurveLow = clampFinite(m_settings->value(QStringLiteral("input/pressureCurveLow"), 0.25).toDouble(), 0.01, 0.94, 0.25);
    m_pressureCurveMid = clampFinite(m_settings->value(QStringLiteral("input/pressureCurveMid"), 0.50).toDouble(), m_pressureCurveLow, 0.97, 0.50);
    m_pressureCurveHigh = clampFinite(m_settings->value(QStringLiteral("input/pressureCurveHigh"), 0.75).toDouble(), m_pressureCurveMid, 0.99, 0.75);
    m_stabilizationDefault = std::clamp(m_settings->value(QStringLiteral("input/stabilizationDefault"), 0).toInt(), 0, 100);
    m_touchMode = sanitizeTouchMode(m_settings->value(QStringLiteral("input/touchMode"), QStringLiteral("stylus_nav_only")).toString());
    m_touchPinchZoom = m_settings->value(QStringLiteral("input/touchPinchZoom"), true).toBool();
    m_touchRotate = m_settings->value(QStringLiteral("input/touchRotate"), true).toBool();
    m_palmRejection = m_settings->value(QStringLiteral("input/palmRejection"), true).toBool();
    m_barrelButton1Action = sanitizeBarrelAction(m_settings->value(QStringLiteral("input/barrelButton1Action"), QStringLiteral("right_click")).toString(), QStringLiteral("right_click"));
    m_barrelButton2Action = sanitizeBarrelAction(m_settings->value(QStringLiteral("input/barrelButton2Action"), QStringLiteral("pan")).toString(), QStringLiteral("pan"));
    m_eraserEndEnabled = m_settings->value(QStringLiteral("input/eraserEndEnabled"), true).toBool();
    m_hoverOutlineEnabled = m_settings->value(QStringLiteral("input/hoverOutlineEnabled"), true).toBool();
    m_tiltCursorEnabled = m_settings->value(QStringLiteral("input/tiltCursorEnabled"), true).toBool();
    m_speedDynamics = std::clamp(m_settings->value(QStringLiteral("input/speedDynamics"), 0).toInt(), -100, 100);
    m_speedAffectsSize = m_settings->value(QStringLiteral("input/speedAffectsSize"), true).toBool();
    m_speedAffectsOpacity = m_settings->value(QStringLiteral("input/speedAffectsOpacity"), true).toBool();
    m_speedAffectsFlow = m_settings->value(QStringLiteral("input/speedAffectsFlow"), false).toBool();
    m_speedAffectsSpacing = m_settings->value(QStringLiteral("input/speedAffectsSpacing"), false).toBool();
    m_speedInvertSize = m_settings->value(QStringLiteral("input/speedInvertSize"), false).toBool();
    m_speedInvertOpacity = m_settings->value(QStringLiteral("input/speedInvertOpacity"), false).toBool();
    m_speedInvertFlow = m_settings->value(QStringLiteral("input/speedInvertFlow"), false).toBool();
    m_speedInvertSpacing = m_settings->value(QStringLiteral("input/speedInvertSpacing"), false).toBool();
    emit configChanged();
}

bool TabletInputSettings::saveToSettings()
{
    if (!m_settings) return true;
    m_settings->setValue(QStringLiteral("input/windowsTabletMode"), m_windowsTabletMode);
    m_settings->setValue(QStringLiteral("input/windowsDisableWmPointer"), m_windowsDisableWmPointer);
    m_settings->setValue(QStringLiteral("input/pressureThreshold"), m_pressureThreshold);
    m_settings->setValue(QStringLiteral("input/pressureMax"), m_pressureMax);
    m_settings->setValue(QStringLiteral("input/pressureGamma"), m_pressureGamma);
    m_settings->setValue(QStringLiteral("input/pressureCurveLow"), m_pressureCurveLow);
    m_settings->setValue(QStringLiteral("input/pressureCurveMid"), m_pressureCurveMid);
    m_settings->setValue(QStringLiteral("input/pressureCurveHigh"), m_pressureCurveHigh);
    m_settings->setValue(QStringLiteral("input/stabilizationDefault"), m_stabilizationDefault);
    m_settings->setValue(QStringLiteral("input/touchMode"), m_touchMode);
    m_settings->setValue(QStringLiteral("input/touchPinchZoom"), m_touchPinchZoom);
    m_settings->setValue(QStringLiteral("input/touchRotate"), m_touchRotate);
    m_settings->setValue(QStringLiteral("input/palmRejection"), m_palmRejection);
    m_settings->setValue(QStringLiteral("input/barrelButton1Action"), m_barrelButton1Action);
    m_settings->setValue(QStringLiteral("input/barrelButton2Action"), m_barrelButton2Action);
    m_settings->setValue(QStringLiteral("input/eraserEndEnabled"), m_eraserEndEnabled);
    m_settings->setValue(QStringLiteral("input/hoverOutlineEnabled"), m_hoverOutlineEnabled);
    m_settings->setValue(QStringLiteral("input/tiltCursorEnabled"), m_tiltCursorEnabled);
    m_settings->setValue(QStringLiteral("input/speedDynamics"), m_speedDynamics);
    m_settings->setValue(QStringLiteral("input/speedAffectsSize"), m_speedAffectsSize);
    m_settings->setValue(QStringLiteral("input/speedAffectsOpacity"), m_speedAffectsOpacity);
    m_settings->setValue(QStringLiteral("input/speedAffectsFlow"), m_speedAffectsFlow);
    m_settings->setValue(QStringLiteral("input/speedAffectsSpacing"), m_speedAffectsSpacing);
    m_settings->setValue(QStringLiteral("input/speedInvertSize"), m_speedInvertSize);
    m_settings->setValue(QStringLiteral("input/speedInvertOpacity"), m_speedInvertOpacity);
    m_settings->setValue(QStringLiteral("input/speedInvertFlow"), m_speedInvertFlow);
    m_settings->setValue(QStringLiteral("input/speedInvertSpacing"), m_speedInvertSpacing);
    return m_settings->sync();
}

WindowsTabletMode TabletInputSettings::parseWindowsMode(const QString& s) noexcept
{
    if (s == QStringLiteral("windows_ink")) return WindowsTabletMode::WindowsInk;
    if (s == QStringLiteral("wintab")) return WindowsTabletMode::WinTab;
    return WindowsTabletMode::Automatic;
}

TouchCanvasMode TabletInputSettings::parseTouchMode(const QString& s) noexcept
{
    if (s == QStringLiteral("allow_all")) return TouchCanvasMode::AllowAll;
    if (s == QStringLiteral("disable_touch")) return TouchCanvasMode::StylusOnlyDisableTouch;
    return TouchCanvasMode::StylusOnlyWithFingerNavigation;
}

BarrelAction TabletInputSettings::parseBarrelAction(const QString& s) noexcept
{
    if (s == QStringLiteral("right_click")) return BarrelAction::RightClick;
    if (s == QStringLiteral("pan")) return BarrelAction::MiddlePan;
    if (s == QStringLiteral("eyedropper")) return BarrelAction::Eyedropper;
    if (s == QStringLiteral("eraser")) return BarrelAction::EraserToggle;
    if (s == QStringLiteral("undo")) return BarrelAction::Undo;
    return BarrelAction::None;
}

InputConfig TabletInputSettings::toInputConfig() const noexcept
{
    InputConfig cfg;
    cfg.windowsMode = parseWindowsMode(m_windowsTabletMode);
    cfg.pressure.minThreshold = m_pressureThreshold;
    cfg.pressure.maxCalibration = m_pressureMax;
    cfg.pressure.gamma = m_pressureGamma;
    cfg.pressure.curveLow = m_pressureCurveLow;
    cfg.pressure.curveMid = m_pressureCurveMid;
    cfg.pressure.curveHigh = m_pressureCurveHigh;
    cfg.pressure.sanitize();
    cfg.touchMode = parseTouchMode(m_touchMode);
    cfg.palmRejection = m_palmRejection;
    cfg.barrel1Action = parseBarrelAction(m_barrelButton1Action);
    cfg.barrel2Action = parseBarrelAction(m_barrelButton2Action);
    cfg.eraserEndEnabled = m_eraserEndEnabled;
    cfg.hoverOutlineEnabled = m_hoverOutlineEnabled;
    cfg.tiltCursorEnabled = m_tiltCursorEnabled;
    return cfg;
}

void TabletInputSettings::setWindowsTabletMode(const QString& mode)
{
    const QString next = sanitizeWindowsMode(mode);
    if (m_windowsTabletMode == next) return;
    m_windowsTabletMode = next;
    emit configChanged();
}

void TabletInputSettings::setWindowsDisableWmPointer(bool disable)
{
    if (m_windowsDisableWmPointer == disable) return;
    m_windowsDisableWmPointer = disable;
    emit configChanged();
}

void TabletInputSettings::setPressureThreshold(double v)
{
    const double next = clampFinite(v, 0.0, 0.30, m_pressureThreshold);
    if (qFuzzyCompare(m_pressureThreshold + 1.0, next + 1.0)) return;
    m_pressureThreshold = next;
    if (m_pressureMax < m_pressureThreshold + 0.15) m_pressureMax = std::min(1.0, m_pressureThreshold + 0.15);
    emit configChanged();
}

void TabletInputSettings::setPressureMax(double v)
{
    const double next = clampFinite(v, std::max(0.40, m_pressureThreshold + 0.15), 1.0, m_pressureMax);
    if (qFuzzyCompare(m_pressureMax + 1.0, next + 1.0)) return;
    m_pressureMax = next;
    emit configChanged();
}

void TabletInputSettings::setPressureGamma(double v)
{
    const double next = clampFinite(v, 0.35, 2.80, m_pressureGamma);
    if (qFuzzyCompare(m_pressureGamma + 1.0, next + 1.0)) return;
    m_pressureGamma = next;
    emit configChanged();
}

void TabletInputSettings::setPressureCurveLow(double v)
{
    const double next = clampFinite(v, 0.01, 0.94, m_pressureCurveLow);
    if (qFuzzyCompare(m_pressureCurveLow + 1.0, next + 1.0)) return;
    m_pressureCurveLow = next;
    if (m_pressureCurveMid < m_pressureCurveLow) m_pressureCurveMid = m_pressureCurveLow;
    if (m_pressureCurveHigh < m_pressureCurveMid) m_pressureCurveHigh = m_pressureCurveMid;
    emit configChanged();
}

void TabletInputSettings::setPressureCurveMid(double v)
{
    const double next = clampFinite(v, m_pressureCurveLow, 0.97, m_pressureCurveMid);
    if (qFuzzyCompare(m_pressureCurveMid + 1.0, next + 1.0)) return;
    m_pressureCurveMid = next;
    if (m_pressureCurveHigh < m_pressureCurveMid) m_pressureCurveHigh = m_pressureCurveMid;
    emit configChanged();
}

void TabletInputSettings::setPressureCurveHigh(double v)
{
    const double next = clampFinite(v, m_pressureCurveMid, 0.99, m_pressureCurveHigh);
    if (qFuzzyCompare(m_pressureCurveHigh + 1.0, next + 1.0)) return;
    m_pressureCurveHigh = next;
    emit configChanged();
}

void TabletInputSettings::setStabilizationDefault(int v)
{
    const int next = std::clamp(v, 0, 100);
    if (m_stabilizationDefault == next) return;
    m_stabilizationDefault = next;
    emit configChanged();
}

void TabletInputSettings::setTouchMode(const QString& mode)
{
    const QString next = sanitizeTouchMode(mode);
    if (m_touchMode == next) return;
    m_touchMode = next;
    emit configChanged();
}

void TabletInputSettings::setTouchPinchZoom(bool enabled)
{
    if (m_touchPinchZoom == enabled) return;
    m_touchPinchZoom = enabled;
    emit configChanged();
}

void TabletInputSettings::setTouchRotate(bool enabled)
{
    if (m_touchRotate == enabled) return;
    m_touchRotate = enabled;
    emit configChanged();
}

void TabletInputSettings::setPalmRejection(bool enabled)
{
    if (m_palmRejection == enabled) return;
    m_palmRejection = enabled;
    emit configChanged();
}

void TabletInputSettings::setBarrelButton1Action(const QString& action)
{
    const QString next = sanitizeBarrelAction(action, QStringLiteral("right_click"));
    if (m_barrelButton1Action == next) return;
    m_barrelButton1Action = next;
    emit configChanged();
}

void TabletInputSettings::setBarrelButton2Action(const QString& action)
{
    const QString next = sanitizeBarrelAction(action, QStringLiteral("pan"));
    if (m_barrelButton2Action == next) return;
    m_barrelButton2Action = next;
    emit configChanged();
}

void TabletInputSettings::setEraserEndEnabled(bool enabled)
{
    if (m_eraserEndEnabled == enabled) return;
    m_eraserEndEnabled = enabled;
    emit configChanged();
}

void TabletInputSettings::setHoverOutlineEnabled(bool enabled)
{
    if (m_hoverOutlineEnabled == enabled) return;
    m_hoverOutlineEnabled = enabled;
    emit configChanged();
}

void TabletInputSettings::setTiltCursorEnabled(bool enabled)
{
    if (m_tiltCursorEnabled == enabled) return;
    m_tiltCursorEnabled = enabled;
    emit configChanged();
}

void TabletInputSettings::setSpeedDynamics(int percent)
{
    const int clamped = std::clamp(percent, -100, 100);
    if (m_speedDynamics == clamped) return;
    m_speedDynamics = clamped;
    emit configChanged();
}

QString TabletInputSettings::speedDynamicsSummary() const
{
    if (m_speedDynamics == 0) {
        return QStringLiteral("Speed is neutral. A stroke looks the same whether you draw slowly or quickly, and your pen pressure keeps working as usual.");
    }
    const bool slowStrong = m_speedDynamics < 0;
    QStringList selected;
    bool anyInverted = false;
    auto add = [&](bool enabled, bool inverted, const QString& name) {
        if (!enabled) return;
        selected << (inverted ? name + QStringLiteral(" (opposite direction)") : name);
        anyInverted = anyInverted || inverted;
    };
    add(m_speedAffectsSize, m_speedInvertSize, QStringLiteral("size"));
    add(m_speedAffectsOpacity, m_speedInvertOpacity, QStringLiteral("opacity"));
    add(m_speedAffectsFlow, m_speedInvertFlow, QStringLiteral("flow"));
    add(m_speedAffectsSpacing, m_speedInvertSpacing, QStringLiteral("spacing"));
    if (selected.isEmpty()) {
        return QStringLiteral("Speed is set, but no property responds yet. Turn on at least one property below. Your pen pressure is never replaced.");
    }
    QString direction = slowStrong
        ? QStringLiteral("Slower strokes become thicker or stronger, and faster strokes become thinner or lighter.")
        : QStringLiteral("Slower strokes become thinner or lighter, and faster strokes become thicker or stronger.");
    if (anyInverted) {
        direction += QStringLiteral(" Properties marked opposite follow the other direction.");
    }
    return direction + QStringLiteral(" Responding: %1. Your pen pressure still applies on its own.")
        .arg(selected.join(QStringLiteral(", ")));
}

void TabletInputSettings::setSpeedAffectsSize(bool enabled)
{
    if (m_speedAffectsSize == enabled) return;
    m_speedAffectsSize = enabled;
    emit configChanged();
}
void TabletInputSettings::setSpeedAffectsOpacity(bool enabled)
{
    if (m_speedAffectsOpacity == enabled) return;
    m_speedAffectsOpacity = enabled;
    emit configChanged();
}
void TabletInputSettings::setSpeedAffectsFlow(bool enabled)
{
    if (m_speedAffectsFlow == enabled) return;
    m_speedAffectsFlow = enabled;
    emit configChanged();
}
void TabletInputSettings::setSpeedAffectsSpacing(bool enabled)
{
    if (m_speedAffectsSpacing == enabled) return;
    m_speedAffectsSpacing = enabled;
    emit configChanged();
}
void TabletInputSettings::setSpeedInvertSize(bool inverted)
{
    if (m_speedInvertSize == inverted) return;
    m_speedInvertSize = inverted;
    emit configChanged();
}
void TabletInputSettings::setSpeedInvertOpacity(bool inverted)
{
    if (m_speedInvertOpacity == inverted) return;
    m_speedInvertOpacity = inverted;
    emit configChanged();
}
void TabletInputSettings::setSpeedInvertFlow(bool inverted)
{
    if (m_speedInvertFlow == inverted) return;
    m_speedInvertFlow = inverted;
    emit configChanged();
}
void TabletInputSettings::setSpeedInvertSpacing(bool inverted)
{
    if (m_speedInvertSpacing == inverted) return;
    m_speedInvertSpacing = inverted;
    emit configChanged();
}

SpeedDynamicsConfig TabletInputSettings::speedDynamicsConfig() const noexcept
{
    SpeedDynamicsConfig config;
    config.master = speedMasterFromPercent(m_speedDynamics);
    config.sizeEnabled = m_speedAffectsSize;
    config.opacityEnabled = m_speedAffectsOpacity;
    config.flowEnabled = m_speedAffectsFlow;
    config.spacingEnabled = m_speedAffectsSpacing;
    config.sizeInverted = m_speedInvertSize;
    config.opacityInverted = m_speedInvertOpacity;
    config.flowInverted = m_speedInvertFlow;
    config.spacingInverted = m_speedInvertSpacing;
    return config;
}

QVariantMap TabletInputSettings::toVariantMap() const
{
    return {
        {QStringLiteral("windowsTabletMode"), m_windowsTabletMode},
        {QStringLiteral("windowsDisableWmPointer"), m_windowsDisableWmPointer},
        {QStringLiteral("pressureThreshold"), m_pressureThreshold},
        {QStringLiteral("pressureMax"), m_pressureMax},
        {QStringLiteral("pressureGamma"), m_pressureGamma},
        {QStringLiteral("pressureCurveLow"), m_pressureCurveLow},
        {QStringLiteral("pressureCurveMid"), m_pressureCurveMid},
        {QStringLiteral("pressureCurveHigh"), m_pressureCurveHigh},
        {QStringLiteral("stabilizationDefault"), m_stabilizationDefault},
        {QStringLiteral("touchMode"), m_touchMode},
        {QStringLiteral("touchPinchZoom"), m_touchPinchZoom},
        {QStringLiteral("touchRotate"), m_touchRotate},
        {QStringLiteral("palmRejection"), m_palmRejection},
        {QStringLiteral("barrelButton1Action"), m_barrelButton1Action},
        {QStringLiteral("barrelButton2Action"), m_barrelButton2Action},
        {QStringLiteral("eraserEndEnabled"), m_eraserEndEnabled},
        {QStringLiteral("hoverOutlineEnabled"), m_hoverOutlineEnabled},
        {QStringLiteral("tiltCursorEnabled"), m_tiltCursorEnabled},
        {QStringLiteral("speedDynamics"), m_speedDynamics},
        {QStringLiteral("speedAffectsSize"), m_speedAffectsSize},
        {QStringLiteral("speedAffectsOpacity"), m_speedAffectsOpacity},
        {QStringLiteral("speedAffectsFlow"), m_speedAffectsFlow},
        {QStringLiteral("speedAffectsSpacing"), m_speedAffectsSpacing},
        {QStringLiteral("speedInvertSize"), m_speedInvertSize},
        {QStringLiteral("speedInvertOpacity"), m_speedInvertOpacity},
        {QStringLiteral("speedInvertFlow"), m_speedInvertFlow},
        {QStringLiteral("speedInvertSpacing"), m_speedInvertSpacing}
    };
}

QVariantMap TabletInputSettings::defaultVariantMap() const
{
    return {
        {QStringLiteral("windowsTabletMode"), QStringLiteral("auto")},
        {QStringLiteral("windowsDisableWmPointer"), false},
        {QStringLiteral("pressureThreshold"), 0.02},
        {QStringLiteral("pressureMax"), 1.0},
        {QStringLiteral("pressureGamma"), 1.0},
        {QStringLiteral("pressureCurveLow"), 0.25},
        {QStringLiteral("pressureCurveMid"), 0.50},
        {QStringLiteral("pressureCurveHigh"), 0.75},
        {QStringLiteral("stabilizationDefault"), 0},
        {QStringLiteral("touchMode"), QStringLiteral("stylus_nav_only")},
        {QStringLiteral("touchPinchZoom"), true},
        {QStringLiteral("touchRotate"), true},
        {QStringLiteral("palmRejection"), true},
        {QStringLiteral("barrelButton1Action"), QStringLiteral("right_click")},
        {QStringLiteral("barrelButton2Action"), QStringLiteral("pan")},
        {QStringLiteral("eraserEndEnabled"), true},
        {QStringLiteral("hoverOutlineEnabled"), true},
        {QStringLiteral("tiltCursorEnabled"), true},
        {QStringLiteral("speedDynamics"), 0},
        {QStringLiteral("speedAffectsSize"), true},
        {QStringLiteral("speedAffectsOpacity"), true},
        {QStringLiteral("speedAffectsFlow"), false},
        {QStringLiteral("speedAffectsSpacing"), false},
        {QStringLiteral("speedInvertSize"), false},
        {QStringLiteral("speedInvertOpacity"), false},
        {QStringLiteral("speedInvertFlow"), false},
        {QStringLiteral("speedInvertSpacing"), false}
    };
}

bool TabletInputSettings::applyVariantMap(const QVariantMap& map)
{
    setWindowsTabletMode(map.value(QStringLiteral("windowsTabletMode"), m_windowsTabletMode).toString());
    setWindowsDisableWmPointer(map.value(QStringLiteral("windowsDisableWmPointer"), m_windowsDisableWmPointer).toBool());
    setPressureThreshold(map.value(QStringLiteral("pressureThreshold"), m_pressureThreshold).toDouble());
    setPressureMax(map.value(QStringLiteral("pressureMax"), m_pressureMax).toDouble());
    setPressureGamma(map.value(QStringLiteral("pressureGamma"), m_pressureGamma).toDouble());
    setPressureCurveLow(map.value(QStringLiteral("pressureCurveLow"), m_pressureCurveLow).toDouble());
    setPressureCurveMid(map.value(QStringLiteral("pressureCurveMid"), m_pressureCurveMid).toDouble());
    setPressureCurveHigh(map.value(QStringLiteral("pressureCurveHigh"), m_pressureCurveHigh).toDouble());
    setStabilizationDefault(map.value(QStringLiteral("stabilizationDefault"), m_stabilizationDefault).toInt());
    setTouchMode(map.value(QStringLiteral("touchMode"), m_touchMode).toString());
    setTouchPinchZoom(map.value(QStringLiteral("touchPinchZoom"), m_touchPinchZoom).toBool());
    setTouchRotate(map.value(QStringLiteral("touchRotate"), m_touchRotate).toBool());
    setPalmRejection(map.value(QStringLiteral("palmRejection"), m_palmRejection).toBool());
    setBarrelButton1Action(map.value(QStringLiteral("barrelButton1Action"), m_barrelButton1Action).toString());
    setBarrelButton2Action(map.value(QStringLiteral("barrelButton2Action"), m_barrelButton2Action).toString());
    setEraserEndEnabled(map.value(QStringLiteral("eraserEndEnabled"), m_eraserEndEnabled).toBool());
    setHoverOutlineEnabled(map.value(QStringLiteral("hoverOutlineEnabled"), m_hoverOutlineEnabled).toBool());
    setTiltCursorEnabled(map.value(QStringLiteral("tiltCursorEnabled"), m_tiltCursorEnabled).toBool());
    setSpeedDynamics(map.value(QStringLiteral("speedDynamics"), m_speedDynamics).toInt());
    setSpeedAffectsSize(map.value(QStringLiteral("speedAffectsSize"), m_speedAffectsSize).toBool());
    setSpeedAffectsOpacity(map.value(QStringLiteral("speedAffectsOpacity"), m_speedAffectsOpacity).toBool());
    setSpeedAffectsFlow(map.value(QStringLiteral("speedAffectsFlow"), m_speedAffectsFlow).toBool());
    setSpeedAffectsSpacing(map.value(QStringLiteral("speedAffectsSpacing"), m_speedAffectsSpacing).toBool());
    setSpeedInvertSize(map.value(QStringLiteral("speedInvertSize"), m_speedInvertSize).toBool());
    setSpeedInvertOpacity(map.value(QStringLiteral("speedInvertOpacity"), m_speedInvertOpacity).toBool());
    setSpeedInvertFlow(map.value(QStringLiteral("speedInvertFlow"), m_speedInvertFlow).toBool());
    setSpeedInvertSpacing(map.value(QStringLiteral("speedInvertSpacing"), m_speedInvertSpacing).toBool());
    return saveToSettings();
}

void TabletInputSettings::restoreDefaults()
{
    applyVariantMap(defaultVariantMap());
}

double TabletInputSettings::evaluatePressureCurve(double rawInput,
                                                  double threshold,
                                                  double maxCal,
                                                  double gamma,
                                                  double low,
                                                  double mid,
                                                  double high) const
{
    PressureCalibration cal;
    cal.minThreshold = threshold;
    cal.maxCalibration = maxCal;
    cal.gamma = gamma;
    cal.curveLow = low;
    cal.curveMid = mid;
    cal.curveHigh = high;
    cal.sanitize();
    return cal.normalize(rawInput, PointerType::Pen, true);
}

void TabletInputSettings::refreshDevices()
{
    m_detectedDevices.clear();
    const auto qtDevs = QtPointerAdapter::enumerateDevices();
    for (const auto& d : qtDevs) {
        m_detectedDevices.push_back(QVariantMap{
            {QStringLiteral("name"), QString::fromStdString(d.name)},
            {QStringLiteral("type"), QString::fromLatin1(pointerTypeName(d.primaryType))},
            {QStringLiteral("backend"), QString::fromLatin1(inputBackendName(d.backend))},
            {QStringLiteral("capabilities"), QString::fromStdString(formatCapabilitiesSummary(d.capabilities))}
        });
    }

    const WinTabDiagnostics wt = WinTabBackend::probe();
    m_winTabAvailable = wt.dllPresent;
#if defined(Q_OS_WIN)
    if (wt.dllPresent) {
        m_winTabStatus = QStringLiteral("wintab32.dll detected (%1 device%2, %3)")
            .arg(wt.deviceCount)
            .arg(wt.deviceCount == 1 ? QString() : QStringLiteral("s"))
            .arg(QString::fromStdString(formatCapabilitiesSummary(wt.capabilities)));
    } else {
        m_winTabStatus = QStringLiteral("wintab32.dll not present (Windows Ink / Qt Native active)");
    }
#else
    m_winTabStatus = QStringLiteral("WinTab is Windows-only; native OS/Qt pointer input active");
#endif
    emit diagnosticsChanged();
}

void TabletInputSettings::updateLiveDiagnostics(const PointerSample& sample,
                                                const InputTelemetry& telemetry,
                                                bool winTabActive)
{
    m_liveDiagnostics = QVariantMap{
        {QStringLiteral("backend"), QString::fromLatin1(inputBackendName(sample.backend))},
        {QStringLiteral("pointerType"), QString::fromLatin1(pointerTypeName(sample.pointerType))},
        {QStringLiteral("pressure"), sample.pressure},
        {QStringLiteral("rawPressure"), sample.rawPressure},
        {QStringLiteral("tiltX"), sample.tiltX},
        {QStringLiteral("tiltY"), sample.tiltY},
        {QStringLiteral("rotationDeg"), sample.rotationRadians * 57.29577951308232},
        {QStringLiteral("eraserActive"), sample.eraserActive},
        {QStringLiteral("winTabActive"), winTabActive},
        {QStringLiteral("capabilities"), QString::fromStdString(formatCapabilitiesSummary(sample.capabilities))},
        {QStringLiteral("sampleRateHz"), telemetry.sampleRateHz()},
        {QStringLiteral("medianUs"), telemetry.medianProcessUs()},
        {QStringLiteral("p95Us"), telemetry.p95ProcessUs()},
        {QStringLiteral("droppedDuplicates"), static_cast<qulonglong>(telemetry.droppedDuplicates())},
        {QStringLiteral("recoveredStrokes"), static_cast<qulonglong>(telemetry.recoveredStrokes())}
    };
    emit diagnosticsChanged();
}

} // namespace komapaint::input
