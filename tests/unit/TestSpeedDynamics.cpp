// Speed Dynamics signal and geometry, without Qt or a GPU.
//
// These tests pin the promises the UI makes: a neutral centre changes nothing,
// both directions are symmetric, each property responds independently, and the
// speed signal itself is the same physical value at any sample rate.
#include "input/PointerInput.h"
#include "input/SpeedDynamics.h"
#include "input/SpeedTracker.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace komapaint;
using namespace komapaint::input;

namespace {
int g_failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("FAIL line %d: %s\n", __LINE__, #expr); ++g_failures; } } while (false)

constexpr std::uint64_t kMs = 1'000'000ULL;

SpeedDynamicsConfig configWithMaster(double master)
{
    SpeedDynamicsConfig config;
    config.master = master;
    return config;
}

void neutralCentreChangesNothing()
{
    const SpeedDynamicsConfig config; // master 0, defaults
    for (double speed : {0.0, 0.05, 0.5, 0.95, 1.0}) {
        CHECK(speedFactors(config, speed).neutral());
        CHECK(speedFactors(config, speed).size == 1.0);
        CHECK(speedFactors(config, speed).opacity == 1.0);
        CHECK(speedFactors(config, speed).flow == 1.0);
        CHECK(speedFactors(config, speed).spacing == 1.0);
    }
    CHECK(!speedFactors(configWithMaster(0.0001), 0.0).neutral());
}

void bothDirectionsAreSymmetricAndMonotonic()
{
    SpeedDynamicsConfig left = configWithMaster(-1.0);
    const auto leftSlow = speedFactors(left, 0.0);
    const auto leftFast = speedFactors(left, 1.0);
    CHECK(leftSlow.size > leftFast.size);
    CHECK(leftSlow.opacity > leftFast.opacity);
    CHECK(leftFast.size < 1.0);
    CHECK(leftSlow.size > 1.0);

    SpeedDynamicsConfig right = configWithMaster(1.0);
    const auto rightSlow = speedFactors(right, 0.0);
    const auto rightFast = speedFactors(right, 1.0);
    CHECK(rightFast.size > rightSlow.size);
    CHECK(rightFast.opacity > rightSlow.opacity);
    CHECK(std::abs(leftSlow.size - rightFast.size) < 1e-12);
    CHECK(std::abs(leftFast.size - rightSlow.size) < 1e-12);

    // Strength grows continuously with distance from centre.
    double previous = 0.0;
    for (int master = 10; master <= 100; master += 10) {
        const double deviation = std::abs(speedFactors(configWithMaster(master / 100.0), 0.0).size - 1.0);
        CHECK(deviation > previous);
        previous = deviation;
    }
}

void everyPropertyIsIndependent()
{
    SpeedDynamicsConfig onlySize = configWithMaster(-1.0);
    onlySize.opacityEnabled = false;
    onlySize.flowEnabled = false;
    onlySize.spacingEnabled = false;
    const auto size = speedFactors(onlySize, 1.0);
    CHECK(size.size < 1.0);
    CHECK(size.opacity == 1.0);
    CHECK(size.flow == 1.0);
    CHECK(size.spacing == 1.0);

    SpeedDynamicsConfig onlyOpacity = configWithMaster(-1.0);
    onlyOpacity.sizeEnabled = false;
    onlyOpacity.flowEnabled = false;
    onlyOpacity.spacingEnabled = false;
    const auto opacity = speedFactors(onlyOpacity, 1.0);
    CHECK(opacity.opacity < 1.0);
    CHECK(opacity.size == 1.0);

    SpeedDynamicsConfig onlyFlow = configWithMaster(-1.0);
    onlyFlow.sizeEnabled = false;
    onlyFlow.opacityEnabled = false;
    onlyFlow.flowEnabled = true;
    onlyFlow.spacingEnabled = false;
    CHECK(speedFactors(onlyFlow, 1.0).flow < 1.0);
    CHECK(speedFactors(onlyFlow, 1.0).size == 1.0);

    SpeedDynamicsConfig onlySpacing = configWithMaster(-1.0);
    onlySpacing.sizeEnabled = false;
    onlySpacing.opacityEnabled = false;
    onlySpacing.flowEnabled = false;
    onlySpacing.spacingEnabled = true;
    const auto spacing = speedFactors(onlySpacing, 1.0);
    CHECK(spacing.spacing > 1.0);
    CHECK(spacing.size == 1.0);
}

void authoredWeightAndInversionAreHonoured()
{
    SpeedDynamicsConfig config = configWithMaster(-1.0);
    config.sizeWeight = 0.0;
    CHECK(speedFactors(config, 1.0).size == 1.0);

    config.sizeWeight = 0.5;
    const auto half = speedFactors(config, 1.0);
    CHECK(half.size > 0.15);
    CHECK(half.size < 1.0);

    config.sizeInverted = true;
    CHECK(speedFactors(config, 1.0).size > 1.0);
    CHECK(speedFactors(config, 0.0).size < 1.0);

    config.master = 1.0;
    config.sizeInverted = false;
    config.flowEnabled = true;
    config.flowInverted = true;
    CHECK(speedFactors(config, 1.0).flow < 1.0);
}

void invalidInputsStayFiniteAndNeutral()
{
    SpeedDynamicsConfig config = configWithMaster(-1.0);
    for (double speed : {-1.0, 2.0, std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity()}) {
        const auto factors = speedFactors(config, speed);
        CHECK(std::isfinite(factors.size));
        CHECK(std::isfinite(factors.opacity));
        CHECK(std::isfinite(factors.flow));
        CHECK(std::isfinite(factors.spacing));
        CHECK(factors.size >= 0.15 && factors.size <= 2.5);
        CHECK(factors.opacity >= 0.05 && factors.opacity <= 1.0);
        CHECK(factors.flow >= 0.05 && factors.flow <= 1.0);
        CHECK(factors.spacing >= 0.20 && factors.spacing <= 2.5);
    }
    SpeedDynamicsConfig bad = config;
    bad.master = NAN;
    CHECK(speedFactors(bad, 0.5).neutral());
    bad.master = -1.0;
    bad.sizeWeight = NAN;
    CHECK(speedFactors(bad, 0.5).size == 1.0);
}

void sampleRateDoesNotChangeTheSpeedSignal()
{
    // The same physical motion (1000 px/s) sampled at 500 Hz and 50 Hz must
    // produce the same normalized speed after the same elapsed time.
    SpeedTracker fast;
    SpeedTracker slow;
    fast.begin(0, 0.0, 0.0);
    slow.begin(0, 0.0, 0.0);
    for (int i = 1; i <= 200; ++i) {
        const std::uint64_t ns = std::uint64_t(i) * 2 * kMs;
        fast.update(ns, double(i) * 2.0, 0.0);
    }
    for (int i = 1; i <= 20; ++i) {
        const std::uint64_t ns = std::uint64_t(i) * 20 * kMs;
        slow.update(ns, double(i) * 20.0, 0.0);
    }
    CHECK(std::abs(fast.value() - slow.value()) < 0.03);
    CHECK(fast.value() > 0.30 && fast.value() < 0.60); // 1000 / 2400
}

void stationaryShortAndInterruptedStrokesSettle()
{
    SpeedTracker tracker;
    tracker.begin(0, 0.0, 0.0);
    double x = 0.0;
    for (int i = 1; i <= 50; ++i) {
        x += 12.0;
        tracker.update(std::uint64_t(i) * 8 * kMs, x, 0.0);
    }
    CHECK(tracker.value() > 0.2);

    // Held still: the value must decay rather than freeze at the last speed.
    for (int i = 1; i <= 40; ++i) {
        tracker.update(std::uint64_t(400 + i * 5) * kMs, x, 0.0);
    }
    CHECK(tracker.value() < 0.15);

    // A short stroke still produces a finite, bounded signal.
    SpeedTracker shortStroke;
    shortStroke.begin(0, 0.0, 0.0);
    shortStroke.update(4 * kMs, 1.0, 0.0);
    CHECK(std::isfinite(shortStroke.value()));
    CHECK(shortStroke.value() >= 0.0 && shortStroke.value() <= 1.0);

    // A stalled interval is a new start, not an absurdly slow movement.
    SpeedTracker gap;
    gap.begin(0, 0.0, 0.0);
    gap.update(8 * kMs, 10.0, 0.0);
    gap.update(1500 * kMs, 10.0, 0.0);
    CHECK(gap.value() == 0.0);
    CHECK(gap.update(1520 * kMs, 20.0, 0.0) > 0.0);
}

void sharpTurnsNoiseAndBadTimestampsStayBounded()
{
    SpeedTracker tracker;
    tracker.begin(0, 0.0, 0.0);
    double x = 0.0;
    for (int i = 1; i <= 20; ++i) {
        x += (i % 2 == 0) ? -6.0 : 6.0; // sharp reversal every sample
        const double v = tracker.update(std::uint64_t(i) * 5 * kMs, x, 0.0);
        CHECK(std::isfinite(v));
        CHECK(v >= 0.0 && v <= 1.0);
    }
    // Repeated/backwards timestamps are ignored, not turned into infinity.
    const double before = tracker.value();
    CHECK(tracker.update(100 * kMs, x + 4.0, 0.0) == before);
    CHECK(tracker.update(90 * kMs, x + 4.0, 0.0) == before);
    CHECK(tracker.update(110 * kMs, NAN, 0.0) == before);
}

void arbiterPublishesSmoothedSpeedAndResetsAtStrokeBoundaries()
{
    PointerStrokeArbiter arbiter;
    RawPointerEvent down;
    down.deviceId = 1;
    down.pointerId = 1;
    down.timestampNs = 100 * kMs;
    down.pointerType = PointerType::Mouse;
    down.phase = PointerPhase::Down;
    down.viewX = 10.0;
    down.viewY = 10.0;
    down.buttons = PrimaryButton;
    auto result = arbiter.process(down, 10.0, 10.0);
    CHECK(result.shouldBeginStroke);
    CHECK(result.sample.speedNormalized == 0.0);

    for (int i = 1; i <= 8; ++i) {
        RawPointerEvent move = down;
        move.phase = PointerPhase::Move;
        move.timestampNs = std::uint64_t(100 + i * 8) * kMs;
        move.viewX = 10.0 + i * 12.0;
        result = arbiter.process(move, move.viewX, move.viewY);
        CHECK(result.shouldExtendStroke);
    }
    CHECK(result.sample.speedNormalized > 0.1);
    const double movingSpeed = result.sample.speedNormalized;

    RawPointerEvent up = down;
    up.phase = PointerPhase::Up;
    up.timestampNs = 200 * kMs;
    up.viewX = 110.0;
    up.buttons = 0;
    result = arbiter.process(up, 110.0, 10.0);
    CHECK(result.shouldEndStroke);
    CHECK(result.sample.speedNormalized >= 0.0);

    RawPointerEvent second = down;
    second.timestampNs = 300 * kMs;
    result = arbiter.process(second, 10.0, 10.0);
    CHECK(result.sample.speedNormalized == 0.0);
    CHECK(movingSpeed > 0.0);
}

} // namespace

int main()
{
    neutralCentreChangesNothing();
    bothDirectionsAreSymmetricAndMonotonic();
    everyPropertyIsIndependent();
    authoredWeightAndInversionAreHonoured();
    invalidInputsStayFiniteAndNeutral();
    sampleRateDoesNotChangeTheSpeedSignal();
    stationaryShortAndInterruptedStrokesSettle();
    sharpTurnsNoiseAndBadTimestampsStayBounded();
    arbiterPublishesSmoothedSpeedAndResetsAtStrokeBoundaries();
    if (g_failures) {
        std::printf("%d Speed Dynamics checks failed\n", g_failures);
        return 1;
    }
    std::printf("Speed Dynamics: all checks passed\n");
    return 0;
}
