#include "PointerInput.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace komapaint::input {
namespace {

constexpr double kPi = 3.14159265358979323846;

inline double clamp01(double v) noexcept
{
    if (!std::isfinite(v)) return 0.0;
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

inline double clampRange(double v, double lo, double hi) noexcept
{
    if (!std::isfinite(v)) return lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

inline std::uint64_t clockNowNs() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// Monotone piecewise cubic Hermite interpolation through 5 knots:
// (0, 0), (0.25, y1), (0.5, y2), (0.75, y3), (1, 1).
double monotoneSpline5(double x, double y1, double y2, double y3) noexcept
{
    constexpr double xs[5] = {0.0, 0.25, 0.50, 0.75, 1.0};
    const double ys[5] = {0.0, y1, y2, y3, 1.0};

    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;

    // Secants between consecutive knots (h = 0.25 for each segment).
    double d[4];
    for (int i = 0; i < 4; ++i) {
        d[i] = (ys[i + 1] - ys[i]) * 4.0;
    }

    // Fritsch-Carlson monotone tangents.
    double m[5]{};
    m[0] = d[0];
    m[4] = d[3];
    for (int i = 1; i <= 3; ++i) {
        if (d[i - 1] <= 0.0 || d[i] <= 0.0) {
            m[i] = 0.0;
        } else {
            m[i] = 2.0 / (1.0 / d[i - 1] + 1.0 / d[i]);
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (d[i] <= 0.0) {
            m[i] = 0.0;
            m[i + 1] = 0.0;
        } else {
            const double alpha = m[i] / d[i];
            const double beta = m[i + 1] / d[i];
            const double s2 = alpha * alpha + beta * beta;
            if (s2 > 9.0) {
                const double tau = 3.0 / std::sqrt(s2);
                m[i] = tau * alpha * d[i];
                m[i + 1] = tau * beta * d[i];
            }
        }
    }

    int seg = 3;
    if (x < 0.25) seg = 0;
    else if (x < 0.50) seg = 1;
    else if (x < 0.75) seg = 2;

    const double t = (x - xs[seg]) * 4.0;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + t;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;
    const double h = 0.25;
    return clamp01(h00 * ys[seg] + h10 * h * m[seg] + h01 * ys[seg + 1] + h11 * h * m[seg + 1]);
}

} // namespace

void PressureCalibration::sanitize() noexcept
{
    minThreshold = clampRange(minThreshold, 0.0, 0.45);
    maxCalibration = clampRange(maxCalibration, std::max(0.40, minThreshold + 0.15), 1.0);
    gamma = clampRange(gamma, 0.35, 2.80);
    curveLow = clampRange(curveLow, 0.01, 0.94);
    curveMid = clampRange(curveMid, curveLow, 0.97);
    curveHigh = clampRange(curveHigh, curveMid, 0.99);
    missingStylusFallbackPressure = clampRange(missingStylusFallbackPressure, 0.05, 0.95);
    mouseFallbackPressure = clampRange(mouseFallbackPressure, 0.05, 1.0);
}

double PressureCalibration::evaluateCurve(double x) const noexcept
{
    const double t = clamp01(x);
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;

    const double low = clampRange(curveLow, 0.01, 0.94);
    const double mid = clampRange(curveMid, low, 0.97);
    const double high = clampRange(curveHigh, mid, 0.99);
    const double g = clampRange(gamma, 0.35, 2.80);

    const double curved = monotoneSpline5(t, low, mid, high);
    return clamp01(std::pow(curved, g));
}

double PressureCalibration::normalize(double rawPressure, PointerType type, bool hasPressure,
                                      double deviceMin, double deviceMax) const noexcept
{
    if (!hasPressure) {
        // Explicit predictable fallback: mouse clicks paint at mouseFallbackPressure (1.0),
        // while a stylus/touch device lacking pressure hardware does NOT silently jump to 1.0.
        if (type == PointerType::Mouse || type == PointerType::Unknown) {
            return clamp01(mouseFallbackPressure);
        }
        return clamp01(missingStylusFallbackPressure);
    }

    if (!std::isfinite(rawPressure)) {
        return type == PointerType::Mouse
            ? clamp01(mouseFallbackPressure)
            : clamp01(missingStylusFallbackPressure);
    }

    double unit = rawPressure;
    if (std::isfinite(deviceMin) && std::isfinite(deviceMax) && deviceMax > deviceMin) {
        unit = (rawPressure - deviceMin) / (deviceMax - deviceMin);
    }
    unit = clamp01(unit);

    const double lo = clampRange(minThreshold, 0.0, 0.45);
    const double hi = clampRange(maxCalibration, std::max(0.40, lo + 0.15), 1.0);
    if (unit <= lo) return 0.0;
    if (unit >= hi) return 1.0;

    const double rescaled = (unit - lo) / (hi - lo);
    return evaluateCurve(rescaled);
}

void InputTelemetry::reset() noexcept
{
    m_acqHead = m_acqCount = 0;
    m_normHead = m_normCount = 0;
    m_procHead = m_procCount = 0;
    m_totalSamples = 0;
    m_droppedDuplicates = 0;
    m_droppedOutOfOrder = 0;
    m_rejectedTouch = 0;
    m_recoveredStrokes = 0;
    m_prevSampleTimestampNs = 0;
    m_sampleRateHz = 0.0;
    m_hasLastSample = false;
    m_lastSample = {};
}

void InputTelemetry::pushRing(std::array<std::uint64_t, Capacity>& ring, std::size_t& head,
                              std::size_t& count, std::uint64_t value) noexcept
{
    ring[head] = value;
    head = (head + 1) % Capacity;
    if (count < Capacity) ++count;
}

double InputTelemetry::quantileUs(const std::array<std::uint64_t, Capacity>& ring,
                                  std::size_t count, double q) noexcept
{
    if (count == 0) return 0.0;
    std::array<std::uint64_t, Capacity> sorted{};
    std::copy_n(ring.data(), count, sorted.data());
    const std::size_t idx = std::min(count - 1,
        static_cast<std::size_t>(std::floor(clamp01(q) * static_cast<double>(count - 1))));
    std::nth_element(sorted.data(), sorted.data() + idx, sorted.data() + count);
    return static_cast<double>(sorted[idx]) / 1000.0;
}

void InputTelemetry::recordAcquisition(std::uint64_t durationNs) noexcept
{
    pushRing(m_acqNs, m_acqHead, m_acqCount, durationNs);
}

void InputTelemetry::recordNormalization(std::uint64_t durationNs) noexcept
{
    pushRing(m_normNs, m_normHead, m_normCount, durationNs);
}

void InputTelemetry::recordProcess(std::uint64_t durationNs) noexcept
{
    pushRing(m_procNs, m_procHead, m_procCount, durationNs);
}

void InputTelemetry::recordSample(const PointerSample& sample) noexcept
{
    ++m_totalSamples;
    if (sample.timestampNs > m_prevSampleTimestampNs && m_prevSampleTimestampNs > 0) {
        const double dtSec = static_cast<double>(sample.timestampNs - m_prevSampleTimestampNs) * 1.0e-9;
        if (dtSec >= 1.0e-4 && dtSec <= 0.5) {
            const double instHz = 1.0 / dtSec;
            m_sampleRateHz = m_sampleRateHz <= 0.0 ? instHz : (m_sampleRateHz * 0.8 + instHz * 0.2);
        }
    }
    if (sample.timestampNs > 0) m_prevSampleTimestampNs = sample.timestampNs;
    m_lastSample = sample;
    m_hasLastSample = true;
}

void InputTelemetry::recordRejected(ArbiterDecision reason) noexcept
{
    switch (reason) {
    case ArbiterDecision::IgnoreDuplicateProtocol:
    case ArbiterDecision::IgnoreSynthesizedMouse:
        ++m_droppedDuplicates;
        break;
    case ArbiterDecision::IgnoreOutOfOrder:
        ++m_droppedOutOfOrder;
        break;
    case ArbiterDecision::IgnoreTouchRejected:
    case ArbiterDecision::IgnorePalmRejected:
        ++m_rejectedTouch;
        break;
    case ArbiterDecision::Accept:
    case ArbiterDecision::IgnoreInvalid:
        break;
    }
}

void InputTelemetry::recordRecoveredStroke() noexcept
{
    ++m_recoveredStrokes;
}

double InputTelemetry::medianAcquisitionUs() const noexcept { return quantileUs(m_acqNs, m_acqCount, 0.50); }
double InputTelemetry::p95AcquisitionUs() const noexcept { return quantileUs(m_acqNs, m_acqCount, 0.95); }
double InputTelemetry::p99AcquisitionUs() const noexcept { return quantileUs(m_acqNs, m_acqCount, 0.99); }
double InputTelemetry::medianProcessUs() const noexcept { return quantileUs(m_procNs, m_procCount, 0.50); }
double InputTelemetry::p95ProcessUs() const noexcept { return quantileUs(m_procNs, m_procCount, 0.95); }
double InputTelemetry::p99ProcessUs() const noexcept { return quantileUs(m_procNs, m_procCount, 0.99); }

void PointerStrokeArbiter::setConfig(const InputConfig& config) noexcept
{
    m_config = config;
    m_config.pressure.sanitize();
    if (!std::isfinite(m_config.referenceMaxSpeedPxPerSec) || m_config.referenceMaxSpeedPxPerSec <= 10.0) {
        m_config.referenceMaxSpeedPxPerSec = 2400.0;
    }
}

void PointerStrokeArbiter::registerDevice(const PointerDeviceInfo& info)
{
    for (auto& existing : m_devices) {
        if (existing.deviceId == info.deviceId && existing.backend == info.backend) {
            existing = info;
            existing.connected = true;
            return;
        }
    }
    PointerDeviceInfo copy = info;
    copy.connected = true;
    m_devices.push_back(std::move(copy));
}

void PointerStrokeArbiter::unregisterDevice(std::uint64_t deviceId)
{
    for (auto& dev : m_devices) {
        if (dev.deviceId == deviceId) {
            dev.connected = false;
            dev.inProximity = false;
        }
    }
    handleDeviceDisconnected(deviceId);
}

bool PointerStrokeArbiter::hasDevice(std::uint64_t deviceId) const noexcept
{
    const auto* dev = findDevice(deviceId);
    return dev && dev->connected;
}

const PointerDeviceInfo* PointerStrokeArbiter::findDevice(std::uint64_t deviceId) const noexcept
{
    for (const auto& dev : m_devices) {
        if (dev.deviceId == deviceId) return &dev;
    }
    return nullptr;
}

void PointerStrokeArbiter::setProximity(std::uint64_t deviceId, bool inProximity,
                                        InputBackend backend, std::uint64_t timestampNs) noexcept
{
    for (auto& dev : m_devices) {
        if (dev.deviceId == deviceId) dev.inProximity = inProximity;
    }
    m_stylusInProximity = inProximity;
    if (inProximity) {
        m_lastStylusActivityNs = timestampNs;
        m_lastStylusBackend = backend;
        if (backend == InputBackend::WinTab) m_lastWinTabActivityNs = timestampNs;
        else if (backend == InputBackend::WindowsInk) m_lastWindowsInkActivityNs = timestampNs;
    }
}

bool PointerStrokeArbiter::stylusRecentlyActive(std::uint64_t nowNs) const noexcept
{
    if (m_stylusInProximity) return true;
    if (m_lastStylusActivityNs == 0 || nowNs < m_lastStylusActivityNs) return false;
    return (nowNs - m_lastStylusActivityNs) <= m_config.synthesizedMouseWindowNs;
}

bool PointerStrokeArbiter::shouldIgnoreProtocol(const RawPointerEvent& raw) const noexcept
{
    const bool stylus = isStylusType(raw.pointerType);

    if (raw.pointerType == PointerType::Mouse) {
        if (raw.synthesizedMouse && (m_strokeActive && isStylusType(m_activePointerType))) {
            return true;
        }
        if (raw.synthesizedMouse && stylusRecentlyActive(raw.timestampNs)) {
            return true;
        }
        if (m_strokeActive && isStylusType(m_activePointerType)) {
            return true;
        }
        if (stylusRecentlyActive(raw.timestampNs) &&
            (raw.phase == PointerPhase::Down || raw.phase == PointerPhase::Move ||
             raw.phase == PointerPhase::Hover)) {
            return true;
        }
        return false;
    }

    if (!stylus) return false;

    // Protocol arbitration on Windows: prevent duplicate Windows Ink + WinTab strokes.
    if (m_config.windowsMode == WindowsTabletMode::WindowsInk &&
        raw.backend == InputBackend::WinTab) {
        return true;
    }
    if (m_config.windowsMode == WindowsTabletMode::WinTab &&
        (raw.backend == InputBackend::WindowsInk || raw.backend == InputBackend::QtNative)) {
        return true;
    }
    if (m_config.windowsMode == WindowsTabletMode::Automatic) {
        if (m_strokeActive && isStylusType(m_activePointerType) && m_activeBackend != raw.backend) {
            return true;
        }
        if (raw.backend == InputBackend::WinTab && m_lastWindowsInkActivityNs > 0 &&
            raw.timestampNs >= m_lastWindowsInkActivityNs &&
            (raw.timestampNs - m_lastWindowsInkActivityNs) < m_config.synthesizedMouseWindowNs) {
            return true;
        }
        if ((raw.backend == InputBackend::WindowsInk || raw.backend == InputBackend::QtNative) &&
            m_lastWinTabActivityNs > 0 &&
            raw.timestampNs >= m_lastWinTabActivityNs &&
            (raw.timestampNs - m_lastWinTabActivityNs) < m_config.synthesizedMouseWindowNs) {
            return true;
        }
    }
    return false;
}

BarrelAction PointerStrokeArbiter::detectBarrelAction(std::uint32_t oldButtons,
                                                        std::uint32_t newButtons) const noexcept
{
    const std::uint32_t pressed = newButtons & ~oldButtons;
    if ((pressed & SecondaryButton) != 0) return m_config.barrel1Action;
    if ((pressed & MiddleButton) != 0) return m_config.barrel2Action;
    if ((pressed & Barrel3Button) != 0) return m_config.barrel2Action;
    return BarrelAction::None;
}

PointerSample PointerStrokeArbiter::normalize(const RawPointerEvent& raw,
                                              double docX, double docY) const noexcept
{
    PointerSample out;
    out.deviceId = raw.deviceId;
    out.pointerId = raw.pointerId;
    out.timestampNs = raw.timestampNs;
    out.pointerType = raw.pointerType;
    out.phase = raw.phase;
    out.backend = raw.backend;
    out.viewX = std::isfinite(raw.viewX) ? raw.viewX : 0.0;
    out.viewY = std::isfinite(raw.viewY) ? raw.viewY : 0.0;
    out.docX = std::isfinite(docX) ? docX : 0.0;
    out.docY = std::isfinite(docY) ? docY : 0.0;
    out.buttons = raw.buttons;
    out.inProximity = raw.inProximity;
    out.synthesizedMouse = raw.synthesizedMouse;
    out.capabilities = raw.capabilities;

    out.rawPressure = raw.capabilities.hasPressure && std::isfinite(raw.pressure)
        ? clamp01(raw.pressure)
        : 0.0;

    out.pressure = m_config.pressure.normalize(
        raw.pressure,
        raw.pointerType,
        raw.capabilities.hasPressure,
        raw.capabilities.minPressure,
        raw.capabilities.maxPressure);

    if (raw.phase == PointerPhase::Hover || raw.phase == PointerPhase::Enter ||
        raw.phase == PointerPhase::Leave || raw.phase == PointerPhase::Cancel) {
        out.pressure = 0.0;
        out.hovering = (raw.phase == PointerPhase::Hover || raw.phase == PointerPhase::Enter);
        out.tipContact = false;
    } else if (raw.phase == PointerPhase::Down || raw.phase == PointerPhase::Move) {
        out.tipContact = true;
        out.hovering = false;
    } else if (raw.phase == PointerPhase::Up) {
        out.tipContact = false;
        out.hovering = raw.inProximity;
    }

    // Never fabricate tilt, rotation, or tangential pressure if device lacks them.
    if (raw.capabilities.hasTilt && std::isfinite(raw.tiltX) && std::isfinite(raw.tiltY)) {
        out.tiltX = clampRange(raw.tiltX, -90.0, 90.0);
        out.tiltY = clampRange(raw.tiltY, -90.0, 90.0);
        out.tiltMagnitude = clamp01(std::hypot(out.tiltX, out.tiltY) / 90.0);
        out.tiltAzimuthRadians = (out.tiltX == 0.0 && out.tiltY == 0.0)
            ? 0.0
            : std::atan2(out.tiltY, out.tiltX);
    }

    if (raw.capabilities.hasRotation && std::isfinite(raw.rotationDegrees)) {
        double deg = std::fmod(raw.rotationDegrees, 360.0);
        if (deg > 180.0) deg -= 360.0;
        if (deg <= -180.0) deg += 360.0;
        out.rotationRadians = deg * (kPi / 180.0);
    }

    if (raw.capabilities.hasTangentialPressure && std::isfinite(raw.tangentialPressure)) {
        out.tangentialPressure = clampRange(raw.tangentialPressure, -1.0, 1.0);
    }

    out.eraserActive = (raw.pointerType == PointerType::Eraser && m_config.eraserEndEnabled);
    return out;
}

ProcessResult PointerStrokeArbiter::process(const RawPointerEvent& raw,
                                            double docX, double docY) noexcept
{
    const std::uint64_t tStart = clockNowNs();
    ProcessResult res;

    if (!std::isfinite(raw.viewX) || !std::isfinite(raw.viewY) ||
        !std::isfinite(docX) || !std::isfinite(docY)) {
        res.decision = ArbiterDecision::IgnoreInvalid;
        m_telemetry.recordRejected(res.decision);
        return res;
    }

    // Touch and palm rejection checks (before mutating stylus/protocol timestamps).
    if (raw.pointerType == PointerType::Touch) {
        if (m_config.touchMode == TouchCanvasMode::StylusOnlyDisableTouch) {
            res.decision = ArbiterDecision::IgnoreTouchRejected;
            m_telemetry.recordRejected(res.decision);
            return res;
        }
        if (m_config.palmRejection) {
            const bool stylusActive = (m_strokeActive && isStylusType(m_activePointerType)) ||
                                      m_stylusInProximity ||
                                      (m_lastStylusActivityNs > 0 && raw.timestampNs >= m_lastStylusActivityNs &&
                                       (raw.timestampNs - m_lastStylusActivityNs) <= m_config.palmRejectionWindowNs);
            if (stylusActive) {
                res.decision = ArbiterDecision::IgnorePalmRejected;
                m_telemetry.recordRejected(res.decision);
                return res;
            }
        }
        if (m_config.touchMode == TouchCanvasMode::StylusOnlyWithFingerNavigation) {
            res.decision = ArbiterDecision::Accept;
            res.shouldRouteTouchNavigation = true;
            res.sample = normalize(raw, docX, docY);
            m_telemetry.recordSample(res.sample);
            m_telemetry.recordProcess(clockNowNs() - tStart);
            return res;
        }
    }

    if (shouldIgnoreProtocol(raw)) {
        res.decision = (raw.pointerType == PointerType::Mouse)
            ? ArbiterDecision::IgnoreSynthesizedMouse
            : ArbiterDecision::IgnoreDuplicateProtocol;
        m_telemetry.recordRejected(res.decision);
        return res;
    }

    // Chronological guard for active stroke move/up events.
    if (m_strokeActive && raw.timestampNs > 0 && m_lastTimestampNs > 0 &&
        raw.timestampNs < m_lastTimestampNs &&
        (raw.phase == PointerPhase::Move || raw.phase == PointerPhase::Up)) {
        res.decision = ArbiterDecision::IgnoreOutOfOrder;
        m_telemetry.recordRejected(res.decision);
        return res;
    }

    if (isStylusType(raw.pointerType)) {
        m_lastStylusActivityNs = raw.timestampNs;
        m_lastStylusBackend = raw.backend;
        if (raw.backend == InputBackend::WinTab) m_lastWinTabActivityNs = raw.timestampNs;
        else if (raw.backend == InputBackend::WindowsInk || raw.backend == InputBackend::QtNative) {
            m_lastWindowsInkActivityNs = raw.timestampNs;
        }
        if (raw.phase != PointerPhase::Leave) m_stylusInProximity = raw.inProximity || raw.phase != PointerPhase::Up;
        else m_stylusInProximity = false;
    }

    const std::uint64_t tNormStart = clockNowNs();
    PointerSample sample = normalize(raw, docX, docY);
    m_telemetry.recordNormalization(clockNowNs() - tNormStart);

    // Compute normalized speed from chronological dt and distance.
    if (m_hasPrevDocPos && raw.timestampNs > m_lastTimestampNs && m_lastTimestampNs > 0) {
        const double dist = std::hypot(sample.docX - m_prevDocX, sample.docY - m_prevDocY);
        const double dtSec = clampRange(static_cast<double>(raw.timestampNs - m_lastTimestampNs) * 1.0e-9,
                                        0.0001, 0.25);
        const double instSpeed = clamp01((dist / dtSec) / m_config.referenceMaxSpeedPxPerSec);
        m_smoothedSpeed = m_smoothedSpeed * 0.4 + instSpeed * 0.6;
        sample.speedNormalized = m_smoothedSpeed;
    }

    if (isStylusType(raw.pointerType)) {
        const BarrelAction action = detectBarrelAction(m_activeButtons, raw.buttons);
        res.triggeredBarrelAction = action;
        if (m_config.barrel1Action == BarrelAction::EraserToggle && (raw.buttons & SecondaryButton) != 0) {
            sample.eraserActive = true;
        } else if (m_config.barrel2Action == BarrelAction::EraserToggle && (raw.buttons & MiddleButton) != 0) {
            sample.eraserActive = true;
        }
    }
    m_activeButtons = raw.buttons;

    switch (raw.phase) {
    case PointerPhase::Enter:
    case PointerPhase::Hover:
        break;

    case PointerPhase::Down:
        if (m_strokeActive) {
            // Missing release on previous stroke: gracefully commit previous before starting new.
            res.shouldEndPreviousStroke = true;
            m_telemetry.recordRecoveredStroke();
        }
        m_strokeActive = true;
        m_activeDeviceId = raw.deviceId;
        m_activePointerId = raw.pointerId;
        m_activeBackend = raw.backend;
        m_activePointerType = raw.pointerType;
        m_smoothedSpeed = 0.0;
        m_prevDocX = sample.docX;
        m_prevDocY = sample.docY;
        m_hasPrevDocPos = true;
        res.shouldBeginStroke = true;
        break;

    case PointerPhase::Move:
        if (m_strokeActive) {
            if (raw.pointerId == m_activePointerId || isStylusType(raw.pointerType) ||
                raw.pointerType == PointerType::Mouse) {
                res.shouldExtendStroke = true;
                m_prevDocX = sample.docX;
                m_prevDocY = sample.docY;
                m_hasPrevDocPos = true;
            }
        }
        break;

    case PointerPhase::Up:
        if (m_strokeActive) {
            res.shouldEndStroke = true;
            m_strokeActive = false;
            m_activeDeviceId = 0;
            m_activePointerId = 0;
            m_activeBackend = InputBackend::Unknown;
            m_activePointerType = PointerType::Unknown;
            m_hasPrevDocPos = false;
            m_smoothedSpeed = 0.0;
        }
        break;

    case PointerPhase::Leave:
        if (isStylusType(raw.pointerType)) m_stylusInProximity = false;
        if (m_strokeActive && raw.deviceId == m_activeDeviceId &&
            raw.backend == m_activeBackend) {
            // Stylus left proximity without an explicit PenUp event -> recover cleanly.
            res.shouldEndStroke = true;
            m_strokeActive = false;
            m_activeDeviceId = 0;
            m_activePointerId = 0;
            m_activeBackend = InputBackend::Unknown;
            m_activePointerType = PointerType::Unknown;
            m_hasPrevDocPos = false;
            m_smoothedSpeed = 0.0;
            m_telemetry.recordRecoveredStroke();
        }
        break;

    case PointerPhase::Cancel:
        if (m_strokeActive) {
            res.shouldCancelPreviousStroke = true;
            m_strokeActive = false;
            m_activeDeviceId = 0;
            m_activePointerId = 0;
            m_activeBackend = InputBackend::Unknown;
            m_activePointerType = PointerType::Unknown;
            m_hasPrevDocPos = false;
            m_smoothedSpeed = 0.0;
            m_telemetry.recordRecoveredStroke();
        }
        break;
    }

    if (raw.timestampNs > m_lastTimestampNs) m_lastTimestampNs = raw.timestampNs;

    res.decision = ArbiterDecision::Accept;
    res.sample = sample;
    m_telemetry.recordSample(sample);
    m_telemetry.recordProcess(clockNowNs() - tStart);
    return res;
}

bool PointerStrokeArbiter::handleDeviceDisconnected(std::uint64_t deviceId) noexcept
{
    if (!m_strokeActive) return false;
    if (deviceId == 0 || deviceId == m_activeDeviceId) {
        m_strokeActive = false;
        m_activeDeviceId = 0;
        m_activePointerId = 0;
        m_activeBackend = InputBackend::Unknown;
        m_activePointerType = PointerType::Unknown;
        m_hasPrevDocPos = false;
        m_smoothedSpeed = 0.0;
        m_stylusInProximity = false;
        m_telemetry.recordRecoveredStroke();
        return true;
    }
    return false;
}

bool PointerStrokeArbiter::handleFocusLost() noexcept
{
    m_stylusInProximity = false;
    m_activeButtons = NoButton;
    if (!m_strokeActive) return false;
    m_strokeActive = false;
    m_activeDeviceId = 0;
    m_activePointerId = 0;
    m_activeBackend = InputBackend::Unknown;
    m_activePointerType = PointerType::Unknown;
    m_hasPrevDocPos = false;
    m_smoothedSpeed = 0.0;
    m_telemetry.recordRecoveredStroke();
    return true;
}

bool PointerStrokeArbiter::checkStaleStroke(std::uint64_t nowNs) noexcept
{
    if (!m_strokeActive || m_lastTimestampNs == 0 || nowNs <= m_lastTimestampNs) return false;
    if ((nowNs - m_lastTimestampNs) > m_config.staleStrokeTimeoutNs) {
        return handleFocusLost();
    }
    return false;
}

void PointerStrokeArbiter::markStrokeEndedExternally() noexcept
{
    m_strokeActive = false;
    m_activeDeviceId = 0;
    m_activePointerId = 0;
    m_activeBackend = InputBackend::Unknown;
    m_activePointerType = PointerType::Unknown;
    m_hasPrevDocPos = false;
    m_smoothedSpeed = 0.0;
}

void PointerStrokeArbiter::reset() noexcept
{
    m_devices.clear();
    m_telemetry.reset();
    m_strokeActive = false;
    m_activeDeviceId = 0;
    m_activePointerId = 0;
    m_activeBackend = InputBackend::Unknown;
    m_activePointerType = PointerType::Unknown;
    m_activeButtons = NoButton;
    m_eraserLatchedByBarrel = false;
    m_lastTimestampNs = 0;
    m_hasPrevDocPos = false;
    m_prevDocX = 0.0;
    m_prevDocY = 0.0;
    m_smoothedSpeed = 0.0;
    m_stylusInProximity = false;
    m_lastStylusActivityNs = 0;
    m_lastStylusBackend = InputBackend::Unknown;
    m_lastWinTabActivityNs = 0;
    m_lastWindowsInkActivityNs = 0;
}

const char* pointerTypeName(PointerType type) noexcept
{
    switch (type) {
    case PointerType::Mouse: return "Mouse";
    case PointerType::Pen: return "Pen";
    case PointerType::Eraser: return "Eraser";
    case PointerType::Touch: return "Touch";
    case PointerType::Unknown: default: return "Unknown";
    }
}

const char* inputBackendName(InputBackend backend) noexcept
{
    switch (backend) {
    case InputBackend::QtNative: return "Qt Native";
    case InputBackend::WindowsInk: return "Windows Ink";
    case InputBackend::WinTab: return "WinTab";
    case InputBackend::MacOSTablet: return "macOS Tablet";
    case InputBackend::AndroidStylus: return "Android Stylus";
    case InputBackend::Synthetic: return "Synthetic";
    case InputBackend::Unknown: default: return "Unknown";
    }
}

const char* barrelActionName(BarrelAction action) noexcept
{
    switch (action) {
    case BarrelAction::RightClick: return "Right-Click";
    case BarrelAction::MiddlePan: return "Pan View";
    case BarrelAction::Eyedropper: return "Eyedropper";
    case BarrelAction::EraserToggle: return "Eraser";
    case BarrelAction::Undo: return "Undo";
    case BarrelAction::None: default: return "None";
    }
}

std::string formatCapabilitiesSummary(const PointerCapabilities& caps)
{
    std::string out;
    auto append = [&](const char* label) {
        if (!out.empty()) out += ", ";
        out += label;
    };
    if (caps.hasPressure) append("Pressure");
    if (caps.hasTilt) append("Tilt");
    if (caps.hasRotation) append("Rotation");
    if (caps.hasTangentialPressure) append("Tangential");
    if (caps.hasEraser) append("Eraser");
    if (caps.hasHover) append("Hover");
    if (caps.hasBarrelButtons) append("Barrel Buttons");
    if (out.empty()) return "Position only";
    return out;
}

} // namespace komapaint::input
