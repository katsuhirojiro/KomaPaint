#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "SpeedTracker.h"

namespace komapaint::input {

enum class PointerType : std::uint8_t {
    Unknown = 0,
    Mouse,
    Pen,
    Eraser,
    Touch
};

enum class PointerPhase : std::uint8_t {
    Enter = 0,
    Hover,
    Down,
    Move,
    Up,
    Leave,
    Cancel
};

enum class InputBackend : std::uint8_t {
    Unknown = 0,
    QtNative,
    WindowsInk,
    WinTab,
    MacOSTablet,
    AndroidStylus,
    Synthetic
};

enum class WindowsTabletMode : std::uint8_t {
    Automatic = 0,
    WindowsInk,
    WinTab
};

enum class BarrelAction : std::uint8_t {
    None = 0,
    RightClick,
    MiddlePan,
    Eyedropper,
    EraserToggle,
    Undo
};

enum class TouchCanvasMode : std::uint8_t {
    AllowAll = 0,
    StylusOnlyWithFingerNavigation,
    StylusOnlyDisableTouch
};

enum PointerButton : std::uint32_t {
    NoButton = 0,
    PrimaryButton = 1U << 0,   // Tip contact / Left mouse
    SecondaryButton = 1U << 1, // Barrel 1 / Right mouse
    MiddleButton = 1U << 2,    // Barrel 2 / Middle mouse
    Barrel3Button = 1U << 3    // Extra barrel button
};

struct PointerCapabilities {
    bool hasPressure = false;
    bool hasTilt = false;
    bool hasRotation = false;
    bool hasTangentialPressure = false;
    bool hasEraser = false;
    bool hasHover = false;
    bool hasBarrelButtons = false;
    bool supportsCoalescing = false;
    double minPressure = 0.0;
    double maxPressure = 1.0;

    bool operator==(const PointerCapabilities&) const = default;
};

struct PointerDeviceInfo {
    std::uint64_t deviceId = 0;
    std::string name;
    std::string vendor;
    PointerType primaryType = PointerType::Unknown;
    InputBackend backend = InputBackend::Unknown;
    PointerCapabilities capabilities{};
    bool connected = false;
    bool inProximity = false;
};

// Raw OS/Qt pointer sample prior to calibration & stroke arbitration.
struct RawPointerEvent {
    std::uint64_t deviceId = 0;
    std::uint64_t pointerId = 0;
    std::uint64_t timestampNs = 0;
    PointerType pointerType = PointerType::Unknown;
    PointerPhase phase = PointerPhase::Move;
    InputBackend backend = InputBackend::Unknown;
    double viewX = 0.0;
    double viewY = 0.0;
    double pressure = 0.0;          // raw [0..1] if hasPressure
    double tiltX = 0.0;             // degrees [-90..90] if hasTilt
    double tiltY = 0.0;             // degrees [-90..90] if hasTilt
    double rotationDegrees = 0.0;   // degrees [0..360) or [-180..180] if hasRotation
    double tangentialPressure = 0.0;// [-1..1] if hasTangentialPressure
    std::uint32_t buttons = NoButton;
    bool inProximity = false;
    bool synthesizedMouse = false;
    PointerCapabilities capabilities{};
};

// Normalized, capability-safe sample used by CanvasViewport and brush engines.
struct PointerSample {
    std::uint64_t deviceId = 0;
    std::uint64_t pointerId = 0;
    std::uint64_t timestampNs = 0;
    PointerType pointerType = PointerType::Unknown;
    PointerPhase phase = PointerPhase::Move;
    InputBackend backend = InputBackend::Unknown;
    double viewX = 0.0;
    double viewY = 0.0;
    double docX = 0.0;
    double docY = 0.0;
    double rawPressure = 0.0;
    double pressure = 0.0;          // calibrated [0..1]
    double tiltX = 0.0;             // degrees [-90..90], 0 if !hasTilt
    double tiltY = 0.0;             // degrees [-90..90], 0 if !hasTilt
    double tiltMagnitude = 0.0;     // [0..1], 0 if !hasTilt
    double tiltAzimuthRadians = 0.0;// [-pi..pi], 0 if !hasTilt
    double rotationRadians = 0.0;   // [-pi..pi], 0 if !hasRotation
    double tangentialPressure = 0.0;// [-1..1], 0 if !hasTangentialPressure
    double speedNormalized = 0.0;   // [0..1] from chronological dt and distance
    std::uint32_t buttons = NoButton;
    bool tipContact = false;
    bool eraserActive = false;
    bool hovering = false;
    bool inProximity = false;
    bool synthesizedMouse = false;
    PointerCapabilities capabilities{};
};

struct PressureCalibration {
    double minThreshold = 0.02;                // [0.0, 0.45]
    double maxCalibration = 1.0;               // [0.40, 1.0]
    double gamma = 1.0;                        // [0.35, 2.8]
    double curveLow = 0.25;                    // y at x = 0.25, [0.02, 0.90]
    double curveMid = 0.50;                    // y at x = 0.50, [0.05, 0.95]
    double curveHigh = 0.75;                   // y at x = 0.75, [0.10, 0.98]
    double missingStylusFallbackPressure = 0.50; // explicit fallback when stylus lacks pressure
    double mouseFallbackPressure = 1.0;        // predictable fallback for mouse clicks

    void sanitize() noexcept;
    double evaluateCurve(double x) const noexcept;
    double normalize(double rawPressure, PointerType type, bool hasPressure,
                     double deviceMin = 0.0, double deviceMax = 1.0) const noexcept;
};

struct InputConfig {
    WindowsTabletMode windowsMode = WindowsTabletMode::Automatic;
    PressureCalibration pressure{};
    TouchCanvasMode touchMode = TouchCanvasMode::StylusOnlyWithFingerNavigation;
    bool palmRejection = true;
    std::uint64_t palmRejectionWindowNs = 220'000'000ULL;
    std::uint64_t synthesizedMouseWindowNs = 140'000'000ULL;
    std::uint64_t staleStrokeTimeoutNs = 30'000'000'000ULL;
    double referenceMaxSpeedPxPerSec = 2400.0;
    BarrelAction barrel1Action = BarrelAction::RightClick;
    BarrelAction barrel2Action = BarrelAction::MiddlePan;
    bool eraserEndEnabled = true;
    bool hoverOutlineEnabled = true;
    bool tiltCursorEnabled = true;
};

enum class ArbiterDecision : std::uint8_t {
    Accept = 0,
    IgnoreDuplicateProtocol,
    IgnoreSynthesizedMouse,
    IgnoreTouchRejected,
    IgnorePalmRejected,
    IgnoreOutOfOrder,
    IgnoreInvalid
};

struct ProcessResult {
    ArbiterDecision decision = ArbiterDecision::IgnoreInvalid;
    bool shouldBeginStroke = false;
    bool shouldExtendStroke = false;
    bool shouldEndStroke = false;
    bool shouldCancelPreviousStroke = false;
    bool shouldEndPreviousStroke = false;
    bool shouldRouteTouchNavigation = false;
    BarrelAction triggeredBarrelAction = BarrelAction::None;
    PointerSample sample{};
};

// Fixed-capacity, zero-allocation hot-path telemetry & latency ring.
class InputTelemetry {
public:
    static constexpr std::size_t Capacity = 256;

    void reset() noexcept;
    void recordAcquisition(std::uint64_t durationNs) noexcept;
    void recordNormalization(std::uint64_t durationNs) noexcept;
    void recordProcess(std::uint64_t durationNs) noexcept;
    void recordSample(const PointerSample& sample) noexcept;
    void recordRejected(ArbiterDecision reason) noexcept;
    void recordRecoveredStroke() noexcept;

    std::uint64_t totalSamples() const noexcept { return m_totalSamples; }
    std::uint64_t droppedDuplicates() const noexcept { return m_droppedDuplicates; }
    std::uint64_t droppedOutOfOrder() const noexcept { return m_droppedOutOfOrder; }
    std::uint64_t rejectedTouch() const noexcept { return m_rejectedTouch; }
    std::uint64_t recoveredStrokes() const noexcept { return m_recoveredStrokes; }

    double sampleRateHz() const noexcept { return m_sampleRateHz; }
    double medianAcquisitionUs() const noexcept;
    double p95AcquisitionUs() const noexcept;
    double p99AcquisitionUs() const noexcept;
    double medianProcessUs() const noexcept;
    double p95ProcessUs() const noexcept;
    double p99ProcessUs() const noexcept;

    bool hasLastSample() const noexcept { return m_hasLastSample; }
    const PointerSample& lastSample() const noexcept { return m_lastSample; }

private:
    static void pushRing(std::array<std::uint64_t, Capacity>& ring, std::size_t& head,
                         std::size_t& count, std::uint64_t value) noexcept;
    static double quantileUs(const std::array<std::uint64_t, Capacity>& ring,
                             std::size_t count, double q) noexcept;

    std::array<std::uint64_t, Capacity> m_acqNs{};
    std::size_t m_acqHead = 0;
    std::size_t m_acqCount = 0;

    std::array<std::uint64_t, Capacity> m_normNs{};
    std::size_t m_normHead = 0;
    std::size_t m_normCount = 0;

    std::array<std::uint64_t, Capacity> m_procNs{};
    std::size_t m_procHead = 0;
    std::size_t m_procCount = 0;

    std::uint64_t m_totalSamples = 0;
    std::uint64_t m_droppedDuplicates = 0;
    std::uint64_t m_droppedOutOfOrder = 0;
    std::uint64_t m_rejectedTouch = 0;
    std::uint64_t m_recoveredStrokes = 0;

    std::uint64_t m_prevSampleTimestampNs = 0;
    double m_sampleRateHz = 0.0;
    bool m_hasLastSample = false;
    PointerSample m_lastSample{};
};

// Arbitrates device protocols, duplicate suppression, chronological ordering,
// palm/touch policy, barrel button mapping, and missing-release recovery.
class PointerStrokeArbiter {
public:
    PointerStrokeArbiter() = default;

    void setConfig(const InputConfig& config) noexcept;
    const InputConfig& config() const noexcept { return m_config; }

    void registerDevice(const PointerDeviceInfo& info);
    void unregisterDevice(std::uint64_t deviceId);
    bool hasDevice(std::uint64_t deviceId) const noexcept;
    const PointerDeviceInfo* findDevice(std::uint64_t deviceId) const noexcept;
    const std::vector<PointerDeviceInfo>& devices() const noexcept { return m_devices; }

    void setProximity(std::uint64_t deviceId, bool inProximity, InputBackend backend,
                      std::uint64_t timestampNs) noexcept;

    // Normalizes raw event coordinates/axes according to explicit capabilities
    // and calibration without touching stroke state.
    PointerSample normalize(const RawPointerEvent& raw,
                            double docX, double docY) const noexcept;

    // Processes an incoming event through arbitration + stroke state machine.
    // Zero heap allocations on the steady-state sample path.
    ProcessResult process(const RawPointerEvent& raw,
                          double docX, double docY) noexcept;

    // Recovery triggers (missing release, disconnect, proximity leave, focus loss).
    bool handleDeviceDisconnected(std::uint64_t deviceId) noexcept;
    bool handleFocusLost() noexcept;
    bool checkStaleStroke(std::uint64_t nowNs) noexcept;
    void markStrokeEndedExternally() noexcept;

    bool strokeActive() const noexcept { return m_strokeActive; }
    std::uint64_t activeDeviceId() const noexcept { return m_activeDeviceId; }
    std::uint64_t activePointerId() const noexcept { return m_activePointerId; }
    InputBackend activeBackend() const noexcept { return m_activeBackend; }
    PointerType activePointerType() const noexcept { return m_activePointerType; }
    bool stylusRecentlyActive(std::uint64_t nowNs) const noexcept;

    InputTelemetry& telemetry() noexcept { return m_telemetry; }
    const InputTelemetry& telemetry() const noexcept { return m_telemetry; }

    void reset() noexcept;

private:
    bool isStylusType(PointerType t) const noexcept {
        return t == PointerType::Pen || t == PointerType::Eraser;
    }
    bool shouldIgnoreProtocol(const RawPointerEvent& raw) const noexcept;
    BarrelAction detectBarrelAction(std::uint32_t oldButtons, std::uint32_t newButtons) const noexcept;

    InputConfig m_config{};
    std::vector<PointerDeviceInfo> m_devices;
    InputTelemetry m_telemetry{};

    bool m_strokeActive = false;
    std::uint64_t m_activeDeviceId = 0;
    std::uint64_t m_activePointerId = 0;
    InputBackend m_activeBackend = InputBackend::Unknown;
    PointerType m_activePointerType = PointerType::Unknown;
    std::uint32_t m_activeButtons = NoButton;
    bool m_eraserLatchedByBarrel = false;

    std::uint64_t m_lastTimestampNs = 0;
    SpeedTracker m_speedTracker{};

    bool m_stylusInProximity = false;
    std::uint64_t m_lastStylusActivityNs = 0;
    InputBackend m_lastStylusBackend = InputBackend::Unknown;
    std::uint64_t m_lastWinTabActivityNs = 0;
    std::uint64_t m_lastWindowsInkActivityNs = 0;
};

const char* pointerTypeName(PointerType type) noexcept;
const char* inputBackendName(InputBackend backend) noexcept;
const char* barrelActionName(BarrelAction action) noexcept;
std::string formatCapabilitiesSummary(const PointerCapabilities& caps);

} // namespace komapaint::input
