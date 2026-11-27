#pragma once
// Speed Dynamics: the artist-controlled bridge between how fast a pointer moves
// and how a brush behaves. It is deliberately engine-neutral and Qt-free: the
// Digital stroke loop, the Realistic material stroke and the Brush Studio
// preview all read the same factors.
//
// The master value is continuous and signed:
//   -100 .. -1  slower strokes get thicker/stronger, faster strokes thinner/weaker
//      0        speed has no influence at all
//     +1 .. 100 slower strokes get thinner/weaker, faster strokes thicker/stronger
//
// A property only responds when its weight is positive (the brush creator) and
// the artist has enabled it. Inverting a property flips its own response without
// changing the master direction, which is how "size thickens while flow fades"
// is expressed.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace komapaint::input {

struct SpeedDynamicsConfig {
    double master = 0.0;            // -1..1, 0 = neutral
    bool sizeEnabled = true;
    bool opacityEnabled = true;
    bool flowEnabled = false;
    bool spacingEnabled = false;
    bool sizeInverted = false;
    bool opacityInverted = false;
    bool flowInverted = false;
    bool spacingInverted = false;
    // Per-property response weights supplied by the brush document. The
    // personal configuration leaves these at 1 and the adapter multiplies in
    // the creator's stored values.
    double sizeWeight = 1.0;
    double opacityWeight = 1.0;
    double flowWeight = 1.0;
    double spacingWeight = 1.0;

    bool anyPropertyEnabled() const noexcept
    {
        return (sizeEnabled && sizeWeight > 0.0) || (opacityEnabled && opacityWeight > 0.0) ||
               (flowEnabled && flowWeight > 0.0) || (spacingEnabled && spacingWeight > 0.0);
    }
    bool operator==(const SpeedDynamicsConfig&) const = default;
};

struct SpeedFactors {
    double size = 1.0;
    double opacity = 1.0;
    double flow = 1.0;
    double spacing = 1.0;

    bool neutral() const noexcept
    {
        return size == 1.0 && opacity == 1.0 && flow == 1.0 && spacing == 1.0;
    }
    bool operator==(const SpeedFactors&) const = default;
};

inline double clampUnit(double value, double fallback = 0.0) noexcept
{
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, 0.0, 1.0);
}

inline SpeedDynamicsConfig sanitizedSpeedConfig(SpeedDynamicsConfig config) noexcept
{
    config.master = std::isfinite(config.master) ? std::clamp(config.master, -1.0, 1.0) : 0.0;
    config.sizeWeight = clampUnit(config.sizeWeight);
    config.opacityWeight = clampUnit(config.opacityWeight);
    config.flowWeight = clampUnit(config.flowWeight);
    config.spacingWeight = clampUnit(config.spacingWeight);
    return config;
}

// The speed signal itself is already smoothed by SpeedTracker. This function
// keeps the geometry in one place: 0.5 is the neutral midpoint, so a fast
// stroke and a slow stroke are symmetric around normal drawing speed.
inline double speedSignal(double normalizedSpeed) noexcept
{
    if (!std::isfinite(normalizedSpeed)) return 0.5;
    return std::clamp(normalizedSpeed, 0.0, 1.0);
}

inline SpeedFactors speedFactors(const SpeedDynamicsConfig& rawConfig, double normalizedSpeed) noexcept
{
    const SpeedDynamicsConfig config = sanitizedSpeedConfig(rawConfig);
    SpeedFactors factors;
    if (config.master == 0.0 || !config.anyPropertyEnabled()) return factors;

    const double signal = speedSignal(normalizedSpeed) * 2.0 - 1.0;
    auto deviation = [&](bool enabled, bool inverted, double weight) {
        if (!enabled || weight <= 0.0) return 0.0;
        return config.master * weight * (inverted ? -1.0 : 1.0) * signal;
    };
    const double sizeDeviation = deviation(config.sizeEnabled, config.sizeInverted, config.sizeWeight);
    const double opacityDeviation = deviation(config.opacityEnabled, config.opacityInverted, config.opacityWeight);
    const double flowDeviation = deviation(config.flowEnabled, config.flowInverted, config.flowWeight);
    const double spacingDeviation = deviation(config.spacingEnabled, config.spacingInverted, config.spacingWeight);

    // Size has the widest useful range. Opacity and flow can reduce a mark or
    // bring it up to its own ceiling, never above it. Spacing moves the other
    // way: a "stronger" slow stroke lays stamps closer together.
    factors.size = std::clamp(1.0 + sizeDeviation * 0.85, 0.15, 2.5);
    factors.opacity = std::clamp(1.0 + opacityDeviation * 0.90, 0.05, 1.0);
    factors.flow = std::clamp(1.0 + flowDeviation * 0.90, 0.05, 1.0);
    factors.spacing = std::clamp(1.0 - spacingDeviation * 0.85, 0.20, 2.5);
    return factors;
}

inline double speedFactorForProperty(const SpeedDynamicsConfig& config, double speed,
                                     double SpeedFactors::*member) noexcept
{
    return speedFactors(config, speed).*member;
}

// A signed percentage shown by the UI and stored in settings.
inline double speedMasterFromPercent(int percent) noexcept
{
    return std::clamp(percent, -100, 100) / 100.0;
}

} // namespace komapaint::input
