#pragma once
// Chronological, device-neutral speed measurement for one stroke.
//
// Every pointer protocol delivers samples at a different rate. A fixed
// exponential weight would therefore make a 1 kHz stylus respond differently
// from a 60 Hz mouse. This tracker converts each interval to a real elapsed
// time, clamps impossible gaps, and uses a time-based smoothing constant so the
// same physical motion produces the same normalized speed on every device.
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace komapaint::input {

struct SpeedTrackerConfig {
    double referenceMaxSpeedPxPerSec = 2400.0;
    double smoothingSeconds = 0.045;
    double maxGapSeconds = 0.25;
    double stationaryEpsilonPx = 0.05;
};

class SpeedTracker {
public:
    explicit SpeedTracker(SpeedTrackerConfig config = {}) noexcept : m_config(sanitize(config)) {}

    const SpeedTrackerConfig& config() const noexcept { return m_config; }
    void setConfig(SpeedTrackerConfig config) noexcept { m_config = sanitize(config); }

    void begin(std::uint64_t timestampNs, double x, double y) noexcept
    {
        m_started = std::isfinite(x) && std::isfinite(y);
        m_lastTimestampNs = timestampNs;
        m_lastX = x;
        m_lastY = y;
        m_smoothed = 0.0;
    }

    void reset() noexcept
    {
        m_started = false;
        m_lastTimestampNs = 0;
        m_lastX = 0.0;
        m_lastY = 0.0;
        m_smoothed = 0.0;
    }

    double value() const noexcept { return m_smoothed; }

    // Returns the normalized [0..1] speed after this sample. Invalid samples
    // and non-increasing timestamps leave the previous value untouched, so a
    // driver quirk can never produce an infinite or NaN speed.
    double update(std::uint64_t timestampNs, double x, double y) noexcept
    {
        if (!m_started) {
            begin(timestampNs, x, y);
            return m_smoothed;
        }
        if (!std::isfinite(x) || !std::isfinite(y) || timestampNs == 0 ||
            timestampNs <= m_lastTimestampNs) {
            return m_smoothed;
        }
        const double dt = double(timestampNs - m_lastTimestampNs) * 1.0e-9;
        if (!std::isfinite(dt) || dt <= 0.0) return m_smoothed;
        if (dt > m_config.maxGapSeconds) {
            // The stroke was interrupted (window switch, driver stall, palm
            // lift). Treat the next sample as a fresh start instead of
            // interpreting a long gap as an extremely slow movement.
            begin(timestampNs, x, y);
            return m_smoothed;
        }
        const double distance = std::hypot(x - m_lastX, y - m_lastY);
        if (!std::isfinite(distance)) return m_smoothed;

        const double target = distance <= m_config.stationaryEpsilonPx
            ? 0.0
            : clamp01((distance / std::max(dt, 1.0e-4)) / m_config.referenceMaxSpeedPxPerSec);
        double alpha = 1.0 - std::exp(-dt / std::max(m_config.smoothingSeconds, 1.0e-4));
        alpha = std::clamp(alpha, 0.05, 1.0);
        // A held pointer should visibly settle rather than freezing at its last
        // speed forever; at very high sample rates the normal alpha would take
        // seconds to do that.
        if (target == 0.0) alpha = std::max(alpha, 0.15);
        m_smoothed += (target - m_smoothed) * alpha;
        m_smoothed = clamp01(m_smoothed);
        m_lastTimestampNs = timestampNs;
        m_lastX = x;
        m_lastY = y;
        return m_smoothed;
    }

private:
    static double clamp01(double value) noexcept
    {
        if (!std::isfinite(value)) return 0.0;
        return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
    }
    static SpeedTrackerConfig sanitize(SpeedTrackerConfig config) noexcept
    {
        if (!std::isfinite(config.referenceMaxSpeedPxPerSec) || config.referenceMaxSpeedPxPerSec <= 10.0)
            config.referenceMaxSpeedPxPerSec = 2400.0;
        if (!std::isfinite(config.smoothingSeconds) || config.smoothingSeconds <= 0.0)
            config.smoothingSeconds = 0.045;
        if (!std::isfinite(config.maxGapSeconds) || config.maxGapSeconds <= 0.0)
            config.maxGapSeconds = 0.25;
        if (!std::isfinite(config.stationaryEpsilonPx) || config.stationaryEpsilonPx < 0.0)
            config.stationaryEpsilonPx = 0.05;
        return config;
    }

    SpeedTrackerConfig m_config;
    bool m_started = false;
    std::uint64_t m_lastTimestampNs = 0;
    double m_lastX = 0.0;
    double m_lastY = 0.0;
    double m_smoothed = 0.0;
};

} // namespace komapaint::input
