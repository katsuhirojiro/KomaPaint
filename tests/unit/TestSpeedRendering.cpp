// Speed Dynamics must change rendered artwork, not just a settings value.
// These tests paint through the production Digital and Realistic stroke paths
// and compare the pixels. A property is only covered here when it can actually
// change a mark.
#include "brush/BrushModel.h"
#include "canvas/SparseTileStore.h"
#include "drawing/BrushDocumentAdapter.h"
#include "drawing/BrushEngine.h"
#include "material/MaterialEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace komapaint;
using namespace komapaint::brush;

namespace {
int g_failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("FAIL line %d: %s\n", __LINE__, #expr); ++g_failures; } } while (false)

constexpr std::int64_t kSize = 96;

std::uint64_t alphaSum(const SparseTileStore& store)
{
    std::uint64_t sum = 0;
    const auto& g = store.geometry();
    for (std::int64_t y = 0; y < g.height(); ++y) {
        for (std::int64_t x = 0; x < g.width(); ++x) {
            const std::uint32_t p = store.pixel({x, y}).value_or(0);
            sum += (p >> 24U) & 0xffU;
        }
    }
    return sum;
}

std::uint64_t hashPixels(const SparseTileStore& store)
{
    std::uint64_t h = 1469598103934665603ull;
    const auto& g = store.geometry();
    for (std::int64_t y = 0; y < g.height(); ++y) {
        for (std::int64_t x = 0; x < g.width(); ++x) {
            h ^= store.pixel({x, y}).value_or(0);
            h *= 1099511628211ull;
        }
    }
    return h;
}

LegacyBrush plainBrush()
{
    LegacyBrush brush;
    brush.size = 18;
    brush.opacity = 100;
    brush.flow = 100;
    brush.spacing = 20;
    brush.hardness = 100;
    brush.pressureSize = 0;
    brush.pressureOpacity = 100;
    brush.pressureFlow = 0;
    brush.speedSize = 100;
    brush.speedOpacity = 100;
    brush.speedFlow = 100;
    brush.speedSpacing = 100;
    return brush;
}

struct DigitalResult {
    std::uint64_t alpha = 0;
    std::uint64_t hash = 0;
    std::uint64_t stamps = 0;
};

DigitalResult paintDigital(const LegacyBrush& legacy, const input::SpeedDynamicsConfig& config,
                           double speed, double pressure = 0.85)
{
    SparseTileStore store(kSize, kSize);
    StrokeRuntime runtime;
    runtime.color = 0xff203040;
    runtime.speed = config;
    BrushDocument document = BrushDocument::fromLegacy(legacy, "speed-brush", "Speed brush");
    auto shared = std::make_shared<const BrushDocument>(std::move(document));
    const BrushDefinition base = drawing::digitalDefinition(*shared, runtime);
    drawing::DocumentStroke stroke(store, shared, base, 0x51eeu);

    InputSample sample;
    sample.pressure = pressure;
    sample.speed = speed;
    sample.x = 18.0;
    sample.y = 48.0;
    CHECK(stroke.begin(sample));
    for (int i = 1; i <= 18; ++i) {
        sample.x = 18.0 + double(i) * 3.4;
        sample.speed = speed;
        CHECK(stroke.extend(sample));
    }
    CHECK(stroke.end(sample));
    CHECK(!stroke.failed());
    return {alphaSum(store), hashPixels(store), stroke.stampCount()};
}

input::SpeedDynamicsConfig master(double value)
{
    input::SpeedDynamicsConfig config;
    config.master = value;
    return config;
}

input::SpeedDynamicsConfig only(input::SpeedDynamicsConfig config, const char* property)
{
    const std::string p = property;
    config.sizeEnabled = p == "size";
    config.opacityEnabled = p == "opacity";
    config.flowEnabled = p == "flow";
    config.spacingEnabled = p == "spacing";
    return config;
}

void neutralCentrePaintsIdentically()
{
    const auto slow = paintDigital(plainBrush(), input::SpeedDynamicsConfig{}, 0.0);
    const auto fast = paintDigital(plainBrush(), input::SpeedDynamicsConfig{}, 1.0);
    CHECK(slow.hash == fast.hash);
    CHECK(slow.alpha == fast.alpha);
}

void digitalMasterDirectionsChangeRenderedPixels()
{
    const auto leftSlow = paintDigital(plainBrush(), master(-1.0), 0.0);
    const auto leftFast = paintDigital(plainBrush(), master(-1.0), 1.0);
    CHECK(leftSlow.alpha > leftFast.alpha);

    const auto rightSlow = paintDigital(plainBrush(), master(1.0), 0.0);
    const auto rightFast = paintDigital(plainBrush(), master(1.0), 1.0);
    CHECK(rightFast.alpha > rightSlow.alpha);

    // Strength is continuous, not an on/off switch.
    const auto weak = paintDigital(plainBrush(), master(-0.25), 1.0);
    CHECK(weak.alpha > leftFast.alpha);
}

void digitalPropertiesRespondIndependently()
{
    const auto sizeSlow = paintDigital(plainBrush(), only(master(-1.0), "size"), 0.0);
    const auto sizeFast = paintDigital(plainBrush(), only(master(-1.0), "size"), 1.0);
    CHECK(sizeSlow.alpha > sizeFast.alpha);

    const auto opacitySlow = paintDigital(plainBrush(), only(master(-1.0), "opacity"), 0.0);
    const auto opacityFast = paintDigital(plainBrush(), only(master(-1.0), "opacity"), 1.0);
    CHECK(opacitySlow.alpha > opacityFast.alpha);
    CHECK(opacitySlow.alpha > 0);
    CHECK(opacityFast.alpha > 0);

    const auto flowSlow = paintDigital(plainBrush(), only(master(-1.0), "flow"), 0.0);
    const auto flowFast = paintDigital(plainBrush(), only(master(-1.0), "flow"), 1.0);
    CHECK(flowSlow.alpha > flowFast.alpha);

    const auto spacingSlow = paintDigital(plainBrush(), only(master(-1.0), "spacing"), 0.0);
    const auto spacingFast = paintDigital(plainBrush(), only(master(-1.0), "spacing"), 1.0);
    CHECK(spacingSlow.stamps > spacingFast.stamps);
}

void digitalPressureAndSpeedCombineWithoutReplacingPressure()
{
    const auto config = only(master(-1.0), "size");
    const auto lightSlow = paintDigital(plainBrush(), config, 0.0, 0.25);
    const auto heavySlow = paintDigital(plainBrush(), config, 0.0, 0.95);
    CHECK(heavySlow.alpha > lightSlow.alpha);

    const auto lightFast = paintDigital(plainBrush(), config, 1.0, 0.25);
    const auto heavyFast = paintDigital(plainBrush(), config, 1.0, 0.95);
    CHECK(heavyFast.alpha > lightFast.alpha);

    // Same pressure, different speed: speed alone still changes the mark.
    CHECK(lightSlow.alpha > lightFast.alpha);
}

struct MaterialResult {
    std::uint64_t alpha = 0;
    std::uint64_t hash = 0;
};

MaterialResult paintMaterial(const input::SpeedDynamicsConfig& config, double speed,
                             double pressure = 0.85)
{
    auto presets = material::defaultBrushDefinitions();
    const auto it = std::find_if(presets.begin(), presets.end(), [](const auto& brush) {
        return brush.id == "gouache-round";
    });
    CHECK(it != presets.end());
    material::BrushDefinition preset = *it;
    preset.speedTransfer = 0.0; // isolate Speed Dynamics from the authored transfer

    LegacyBrush legacy = plainBrush();
    StrokeRuntime runtime;
    runtime.color = 0xff8a3b2a;
    runtime.speed = config;
    material::BrushDefinition brush = drawing::materialDefinition(preset, legacy, runtime);
    drawing::assignInitialPaint(&brush, legacy, runtime.color, 1.0);

    SparseTileStore surface(96, 96);
    material::BrushReservoir reservoir(brush.reservoirCapacity);
    material::MaterialEngine engine(material::PigmentLibrary{});
    material::MaterialStroke stroke(surface, brush, reservoir, engine,
                                    material::gouacheProfile(), material::absorbentPaper());
    material::MaterialSample sample;
    sample.input = {pressure, speed, 0.0, 0.0};
    sample.x = 16.0;
    sample.y = 48.0;
    CHECK(stroke.begin(sample));
    for (int i = 1; i <= 20; ++i) {
        sample.x = 16.0 + double(i) * 3.2;
        sample.input.speed = speed;
        CHECK(stroke.extend(sample));
    }
    CHECK(stroke.end(sample));
    CHECK(!stroke.failed());
    return {alphaSum(surface), hashPixels(surface)};
}

void authoredWeightsSurviveTheBrushDocument()
{
    LegacyBrush legacy = plainBrush();
    legacy.speedSize = 17;
    legacy.speedOpacity = 23;
    legacy.speedFlow = 31;
    legacy.speedSpacing = 47;
    const BrushDocument document = BrushDocument::fromLegacy(legacy, "weights", "Weights");
    const LegacyBrush reopened = document.toLegacy();
    CHECK(reopened.speedSize == 17);
    CHECK(reopened.speedOpacity == 23);
    CHECK(reopened.speedFlow == 31);
    CHECK(reopened.speedSpacing == 47);

    // A creator ceiling of zero removes the response even when the artist has
    // enabled that property and turned the master all the way up.
    LegacyBrush noSize = plainBrush();
    noSize.speedSize = 0;
    const auto slow = paintDigital(noSize, only(master(-1.0), "size"), 0.0);
    const auto fast = paintDigital(noSize, only(master(-1.0), "size"), 1.0);
    CHECK(slow.hash == fast.hash);
}

void realisticPressureAndSpeedCombine()
{
    const auto config = only(master(-1.0), "size");
    const auto lightSlow = paintMaterial(config, 0.0, 0.25);
    const auto heavySlow = paintMaterial(config, 0.0, 0.95);
    CHECK(heavySlow.alpha > lightSlow.alpha);
    const auto lightFast = paintMaterial(config, 1.0, 0.25);
    const auto heavyFast = paintMaterial(config, 1.0, 0.95);
    CHECK(heavyFast.alpha > lightFast.alpha);
    CHECK(lightSlow.alpha > lightFast.alpha);
}

void realisticMasterChangesTheRenderedStroke()
{
    const auto neutralSlow = paintMaterial(input::SpeedDynamicsConfig{}, 0.0);
    const auto neutralFast = paintMaterial(input::SpeedDynamicsConfig{}, 1.0);
    CHECK(neutralSlow.hash == neutralFast.hash);

    const auto leftSlow = paintMaterial(only(master(-1.0), "size"), 0.0);
    const auto leftFast = paintMaterial(only(master(-1.0), "size"), 1.0);
    CHECK(leftSlow.alpha > leftFast.alpha);
    CHECK(leftSlow.hash != leftFast.hash);

    const auto rightSlow = paintMaterial(only(master(1.0), "opacity"), 0.0);
    const auto rightFast = paintMaterial(only(master(1.0), "opacity"), 1.0);
    CHECK(rightFast.alpha > rightSlow.alpha);
}

} // namespace

int main()
{
    neutralCentrePaintsIdentically();
    digitalMasterDirectionsChangeRenderedPixels();
    digitalPropertiesRespondIndependently();
    digitalPressureAndSpeedCombineWithoutReplacingPressure();
    authoredWeightsSurviveTheBrushDocument();
    realisticPressureAndSpeedCombine();
    realisticMasterChangesTheRenderedStroke();
    if (g_failures) {
        std::printf("%d Speed Dynamics rendering checks failed\n", g_failures);
        return 1;
    }
    std::printf("Speed Dynamics rendering: all checks passed\n");
    return 0;
}
