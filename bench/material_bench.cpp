// Local (Qt-free) workload harness for the Traditional Material Engine.
//
// Two application-level workloads, both taken from what the editor actually
// does with a watercolour brush:
//   1. one stroke through the reservoir/deposition path, and
//   2. the render pass the viewport runs for every visible tile that the
//      stroke dirtied.
//
// They are measured separately because they fail for different reasons: the
// stroke is limited by per-cell Kubelka-Munk arithmetic plus reservoir
// bookkeeping, the render pass by optical evaluation per distinct cell.
#include "material/MaterialEngine.h"
#include "material/MaterialTileStore.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace komapaint;
using namespace komapaint::material;
using Clock = std::chrono::steady_clock;

static double pct(std::vector<double> v, double q)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double idx = q * double(v.size() - 1);
    const auto lo = std::size_t(idx);
    const auto hi = std::min(v.size() - 1, lo + 1);
    const double f = idx - double(lo);
    return v[lo] + (v[hi] - v[lo]) * f;
}

static const BrushDefinition* findPreset(const std::vector<BrushDefinition>& presets, const char* id)
{
    for (const auto& brush : presets)
        if (brush.id == id) return &brush;
    return nullptr;
}

int main()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    const auto presets = defaultBrushDefinitions();
    const auto* wash = findPreset(presets, "watercolour-wash");
    if (!wash) { std::puts("watercolour-wash preset missing"); return 1; }

    const int strokeRounds = 12;
    std::vector<double> strokeSamples;
    strokeSamples.reserve(strokeRounds);
    std::vector<TilePoint> tiles;
    {
        MaterialTileStore store(1600, 1200, 64);
        BrushReservoir reservoir(wash->effectiveReservoirCapacity());
        reservoir.reload(*wash);
        MaterialStroke stroke(store, *wash, reservoir, engine, watercolourProfile(), coldPressedPaper());
        stroke.begin({160.0, 260.0, {0.8, 0.25, 0.2, 0.0}});
        stroke.extend({460.0, 290.0, {0.85, 0.30, 0.25, 0.0}});
        stroke.end({760.0, 320.0, {0.7, 0.20, 0.15, 0.0}});
        if (stroke.failed()) { std::puts("stroke failed"); return 1; }
        tiles = store.activeTilePoints();
    }

    for (int r = 0; r < strokeRounds; ++r) {
        MaterialTileStore store(1600, 1200, 64);
        BrushReservoir reservoir(wash->effectiveReservoirCapacity());
        reservoir.reload(*wash);
        MaterialStroke stroke(store, *wash, reservoir, engine, watercolourProfile(), coldPressedPaper());
        const auto start = Clock::now();
        stroke.begin({160.0, 260.0, {0.8, 0.25, 0.2, 0.0}});
        stroke.extend({460.0, 290.0, {0.85, 0.30, 0.25, 0.0}});
        stroke.end({760.0, 320.0, {0.7, 0.20, 0.15, 0.0}});
        const auto end = Clock::now();
        if (stroke.failed()) { std::puts("stroke failed"); return 1; }
        strokeSamples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    const int passes = 40;
    MaterialRenderer renderer(engine);
    std::vector<std::uint32_t> out;
    std::vector<double> passSamples;
    passSamples.reserve(passes);
    {
        MaterialTileStore store(1600, 1200, 64);
        BrushReservoir reservoir(wash->effectiveReservoirCapacity());
        reservoir.reload(*wash);
        MaterialStroke stroke(store, *wash, reservoir, engine, watercolourProfile(), coldPressedPaper());
        stroke.begin({160.0, 260.0, {0.8, 0.25, 0.2, 0.0}});
        stroke.extend({460.0, 290.0, {0.85, 0.30, 0.25, 0.0}});
        stroke.end({760.0, 320.0, {0.7, 0.20, 0.15, 0.0}});
        const auto active = store.activeTilePoints();
        for (int i = 0; i < passes; ++i) {
            const auto start = Clock::now();
            for (const TilePoint& t : active)
                renderer.renderTile(store, t, {}, watercolourProfile(), coldPressedPaper(), &out);
            const auto end = Clock::now();
            passSamples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }
    }

    std::printf("watercolour wash stroke on 1600x1200 (%zu active tiles)\n", tiles.size());
    std::printf("  stroke   p50 %7.2f ms  p95 %7.2f ms  p99 %7.2f ms\n",
                pct(strokeSamples, 0.50), pct(strokeSamples, 0.95), pct(strokeSamples, 0.99));
    std::printf("  render   p50 %7.3f ms  p95 %7.3f ms  p99 %7.3f ms  (visible-tile pass, %d passes)\n",
                pct(passSamples, 0.50), pct(passSamples, 0.95), pct(passSamples, 0.99), passes);
    return 0;
}
