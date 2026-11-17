#include "canvas/CpuTileCompositor.h"
#include "canvas/DirtyTileTracker.h"
#include "canvas/SparseTileStore.h"
#include "drawing/BrushEngine.h"
#include "drawing/StrokeStabilizer.h"
#include "material/MaterialEngine.h"
#include "material/MaterialTileStore.h"
#include "material/PaintMixerModel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Stats {
    double minMs = 0.0;
    double medianMs = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double meanMs = 0.0;
    double totalMs = 0.0;
};

Stats computeStats(std::vector<double> samples)
{
    Stats s{};
    if (samples.empty()) return s;
    std::sort(samples.begin(), samples.end());
    s.minMs = samples.front();
    s.totalMs = std::accumulate(samples.begin(), samples.end(), 0.0);
    s.meanMs = s.totalMs / static_cast<double>(samples.size());
    auto quantile = [&](double q) {
        const double idx = q * static_cast<double>(samples.size() - 1);
        const auto lo = static_cast<std::size_t>(idx);
        const auto hi = std::min(samples.size() - 1, lo + 1);
        const double frac = idx - static_cast<double>(lo);
        return samples[lo] + (samples[hi] - samples[lo]) * frac;
    };
    s.medianMs = quantile(0.50);
    s.p95Ms = quantile(0.95);
    s.p99Ms = quantile(0.99);
    return s;
}

template <typename Fn>
Stats benchmarkRuns(int warmup, int runs, Fn&& fn)
{
    for (int i = 0; i < warmup; ++i) fn();
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(runs));
    for (int i = 0; i < runs; ++i) {
        const auto t0 = Clock::now();
        fn();
        const auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return computeStats(std::move(samples));
}

void printRow(const char* category, const char* name, const char* scope,
              int runs, const Stats& s)
{
    std::cout << std::left << std::setw(14) << category
              << " | " << std::setw(32) << name
              << " | " << std::setw(36) << scope
              << " | n=" << std::setw(3) << runs
              << " | med=" << std::fixed << std::setprecision(3) << std::setw(9) << s.medianMs << "ms"
              << " p95=" << std::setw(9) << s.p95Ms << "ms"
              << " p99=" << std::setw(9) << s.p99Ms << "ms"
              << " mean=" << std::setw(9) << s.meanMs << "ms"
              << " tot=" << std::setw(9) << s.totalMs << "ms\n";
}

const komapaint::material::BrushDefinition& findPreset(const char* id)
{
    static const auto presets = komapaint::material::defaultBrushDefinitions();
    for (const auto& b : presets)
        if (b.id == id) return b;
    std::abort();
}

std::string buildConfigString()
{
    std::ostringstream os;
#if defined(_MSC_VER)
    os << "MSVC " << _MSC_VER;
#elif defined(__clang__)
    os << "Clang " << __clang_major__ << "." << __clang_minor__;
#elif defined(__GNUC__)
    os << "GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
#else
    os << "UnknownCompiler";
#endif
#if defined(__OPTIMIZE__) || defined(NDEBUG)
    os << " Release/Optimized";
#else
    os << " Unoptimized/Debug";
#endif
#if defined(__x86_64__) || defined(_M_X64)
    os << " x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    os << " arm64";
#endif
    return os.str();
}

} // namespace

int main()
{
    using namespace komapaint;
    using namespace komapaint::material;
    using DigitalBrush = komapaint::BrushDefinition;

    std::cout << "=== KomaPaint Performance Benchmark ===\n";
    std::cout << "BuildConfig: " << buildConfigString() << "\n";
    std::cout << "CanvasDefaults: Digital=2048x2048 (1024 tiles@64px), MaterialStage6=1600x1200 (475 tiles@64px), Mixer=256x160\n";

    // ---- 1. Digital Engine: individual dab + full 600px strokes ----
    {
        auto runDigitalStroke = [](BrushTip tip, double size, double spacing, double hardness,
                                   double blendStrength, double strokeLen, int stabilizerStrength) {
            SparseTileStore store(2048, 2048, 64);
            DigitalBrush brush;
            brush.size = size;
            brush.spacing = spacing;
            brush.hardness = hardness;
            brush.tip = tip;
            brush.color = 0xff2856a8U;
            brush.opacity = 0.85;
            brush.flow = 0.85;
            brush.pressureSize = 0.4;
            brush.followDirection = (tip == BrushTip::Bristle || tip == BrushTip::Graphite);
            brush.aspectRatio = (tip == BrushTip::Bristle) ? 1.6 : 1.0;
            brush.blendStrength = blendStrength;
            brush.colorMix = blendStrength > 0.0 ? 0.45 : 1.0;

            if (blendStrength > 0.0) {
                DigitalBrush under;
                under.size = 64.0;
                under.spacing = 0.25;
                under.color = 0xffd9532fU;
                BrushStroke pre(store, under);
                pre.begin({160.0, 240.0, 1.0});
                pre.end({160.0 + strokeLen, 240.0, 1.0});
            }

            StrokeStabilizer stab(stabilizerStrength);
            BrushStroke stroke(store, brush);
            auto s0 = stab.filter({180.0, 240.0, 0.75});
            stroke.begin({s0.x, s0.y, s0.pressure});
            const int steps = 20;
            for (int i = 1; i < steps; ++i) {
                const double t = static_cast<double>(i) / steps;
                auto si = stab.filter({180.0 + strokeLen * t, 240.0 + 18.0 * std::sin(t * 3.14159), 0.75});
                stroke.extend({si.x, si.y, si.pressure});
            }
            auto sn = stab.filter({180.0 + strokeLen, 240.0, 0.75});
            stroke.end({sn.x, sn.y, sn.pressure});
            if (stroke.failed() || store.allocatedTiles() == 0) std::abort();
        };

        auto runDigitalDab = [](BrushTip tip, double size, double hardness, double blendStrength) {
            SparseTileStore store(2048, 2048, 64);
            DigitalBrush brush;
            brush.size = size;
            brush.hardness = hardness;
            brush.tip = tip;
            brush.color = 0xff2856a8U;
            brush.opacity = 0.85;
            brush.flow = 0.85;
            brush.blendStrength = blendStrength;
            brush.colorMix = blendStrength > 0.0 ? 0.5 : 1.0;
            if (blendStrength > 0.0) {
                DigitalBrush under;
                under.size = size + 16.0;
                under.color = 0xffd9532fU;
                BrushRasterizer::dab(store, under, {512.0, 512.0, 1.0});
            }
            BrushRasterizer::dab(store, brush, {512.0, 512.0, 0.8});
        };

        printRow("Digital", "dab_round_48px", "1 dab @ 48px soft", 25,
                 benchmarkRuns(3, 25, [&] { runDigitalDab(BrushTip::Round, 48.0, 0.5, 0.0); }));
        printRow("Digital", "dab_round_128px", "1 dab @ 128px soft", 15,
                 benchmarkRuns(2, 15, [&] { runDigitalDab(BrushTip::Round, 128.0, 0.6, 0.0); }));
        printRow("Digital", "dab_bristle_48px", "1 dab @ 48px bristle", 20,
                 benchmarkRuns(2, 20, [&] { runDigitalDab(BrushTip::Bristle, 48.0, 0.7, 0.0); }));
        printRow("Digital", "dab_smudge_48px", "1 dab @ 48px blend=0.7", 20,
                 benchmarkRuns(2, 20, [&] { runDigitalDab(BrushTip::Round, 48.0, 0.6, 0.7); }));

        printRow("Digital", "stroke_round_48px_600px", "600px stroke sp=0.15 stab=3", 11,
                 benchmarkRuns(2, 11, [&] { runDigitalStroke(BrushTip::Round, 48.0, 0.15, 0.5, 0.0, 600.0, 3); }));
        printRow("Digital", "stroke_textured_48px_600px", "600px stroke sp=0.20 stab=3", 11,
                 benchmarkRuns(2, 11, [&] { runDigitalStroke(BrushTip::Textured, 48.0, 0.20, 0.6, 0.0, 600.0, 3); }));
        printRow("Digital", "stroke_bristle_36px_600px", "600px stroke sp=0.18 stab=3", 11,
                 benchmarkRuns(2, 11, [&] { runDigitalStroke(BrushTip::Bristle, 36.0, 0.18, 0.7, 0.0, 600.0, 3); }));
        printRow("Digital", "stroke_graphite_24px_600px", "600px stroke sp=0.15 stab=2", 11,
                 benchmarkRuns(2, 11, [&] { runDigitalStroke(BrushTip::Graphite, 24.0, 0.15, 0.7, 0.0, 600.0, 2); }));
        printRow("Digital", "stroke_stipple_40px_600px", "600px stroke sp=0.25 stab=2", 11,
                 benchmarkRuns(2, 11, [&] { runDigitalStroke(BrushTip::Stipple, 40.0, 0.25, 0.7, 0.0, 600.0, 2); }));
        printRow("Digital", "stroke_smudge_36px_400px", "400px stroke sp=0.20 blend=0.7", 11,
                 benchmarkRuns(2, 11, [&] { runDigitalStroke(BrushTip::Round, 36.0, 0.20, 0.6, 0.7, 400.0, 2); }));
        printRow("Digital", "stroke_round_128px_400px", "400px stroke sp=0.15 size=128", 7,
                 benchmarkRuns(1, 7, [&] { runDigitalStroke(BrushTip::Round, 128.0, 0.15, 0.6, 0.0, 400.0, 2); }));
        // Wide washes and background blocking: sizes above the old 256 px
        // ceiling used to fail outright, so this row is also a limit check.
        printRow("Digital", "stroke_round_400px_400px", "400px stroke sp=0.15 size=400", 5,
                 benchmarkRuns(1, 5, [&] { runDigitalStroke(BrushTip::Round, 400.0, 0.15, 0.7, 0.0, 400.0, 2); }));
        printRow("Digital", "stroke_soft_400px_400px", "400px soft stroke size=400", 5,
                 benchmarkRuns(1, 5, [&] { runDigitalStroke(BrushTip::Round, 400.0, 0.15, 0.15, 0.0, 400.0, 2); }));
    }

    // ---- 2. CPU Tile Compositor & Dirty Regions ----
    {
        TileGeometry geom(2048, 2048, 64);
        std::deque<SparseTileStore> layerStores;
        for (int i = 0; i < 8; ++i) layerStores.emplace_back(2048, 2048, 64);

        const std::uint32_t palette[8] = {
            0x80204080U, 0xa0408020U, 0x90802040U, 0x70505010U,
            0x60106080U, 0xb0804010U, 0x50702070U, 0xc0303030U
        };
        for (int l = 0; l < 8; ++l) {
            DigitalBrush b;
            b.size = 180.0;
            b.spacing = 0.25;
            b.hardness = 0.6;
            b.color = palette[l];
            BrushStroke s(layerStores[static_cast<std::size_t>(l)], b);
            s.begin({200.0 + l * 40.0, 200.0 + l * 30.0, 1.0});
            s.end({1400.0, 1400.0, 1.0});
        }
        std::vector<CompositeLayer> layers;
        for (int l = 7; l >= 0; --l) {
            BlendMode mode = BlendMode::Normal;
            if (l == 2) mode = BlendMode::Multiply;
            else if (l == 4) mode = BlendMode::Screen;
            else if (l == 6) mode = BlendMode::Overlay;
            layers.push_back({&layerStores[static_cast<std::size_t>(l)], true, 0.85, mode, nullptr});
        }

        std::vector<TilePoint> sampleTiles;
        for (int ty = 3; ty < 19; ++ty)
            for (int tx = 3; tx < 19; ++tx)
                sampleTiles.push_back({tx, ty});

        printRow("Compositor", "compose_single_tile_8layers", "1 tile (64x64) 8 layers mixed", 50,
                 benchmarkRuns(5, 50, [&] {
                     auto out = CpuTileCompositor::compose(geom, {8, 8}, layers, 0xffffffffU);
                     if (!out) std::abort();
                 }));

        printRow("Compositor", "compose_256tiles_8layers", "256 tiles (1024x1024) 8 layers", 7,
                 benchmarkRuns(1, 7, [&] {
                     for (const TilePoint tp : sampleTiles) {
                         auto out = CpuTileCompositor::compose(geom, tp, layers, 0xffffffffU);
                         if (!out) std::abort();
                     }
                 }));

        printRow("Compositor", "dirty_invalidation_plan_ack", "prepare+accept+ack 64 dirty tiles", 30,
                 benchmarkRuns(3, 30, [&] {
                     CompositeInvalidation inv;
                     for (int ty = 4; ty < 12; ++ty)
                         for (int tx = 4; tx < 12; ++tx)
                             layerStores[0].setPixel({tx * 64 + 10, ty * 64 + 10}, 0xff112233U);
                     auto plan = inv.prepare(layers, 0xffffffffU);
                     if (!inv.accept(plan, layers, 0xffffffffU)) std::abort();
                     for (const TilePoint tp : plan.tiles) {
                         if (!inv.acknowledge(plan, tp, layers, 0xffffffffU)) std::abort();
                     }
                 }));
    }

    // ---- 3. Realistic Material Engine ----
    {
        PigmentLibrary pigments;
        MaterialEngine engine(pigments);
        const auto& wash = findPreset("watercolour-wash");
        const auto& gouache = findPreset("gouache-round");
        const auto& dryGouache = findPreset("dry-gouache");

        printRow("Material", "dab_watercolour_wash_48px", "1 dab @ 48px on empty 1600x1200", 15,
                 benchmarkRuns(2, 15, [&] {
                     MaterialTileStore store(1600, 1200, 64);
                     BrushReservoir res(wash.effectiveReservoirCapacity());
                     res.reload(wash);
                     MaterialDab dab(engine, watercolourProfile(), coldPressedPaper());
                     dab.apply(store, wash, res, 400.0, 400.0, {0.8, 0.25, 0.2, 0.0});
                 }));

        printRow("Material", "dab_gouache_round_18px", "1 dab @ 18px on empty 1600x1200", 25,
                 benchmarkRuns(3, 25, [&] {
                     MaterialTileStore store(1600, 1200, 64);
                     BrushReservoir res(gouache.effectiveReservoirCapacity());
                     res.reload(gouache);
                     MaterialDab dab(engine, gouacheProfile(), coldPressedPaper());
                     dab.apply(store, gouache, res, 400.0, 400.0, {0.8, 0.25, 0.2, 0.0});
                 }));

        printRow("Material", "dab_dry_gouache_24px", "1 dab @ 24px bristle on empty", 25,
                 benchmarkRuns(3, 25, [&] {
                     MaterialTileStore store(1600, 1200, 64);
                     BrushReservoir res(dryGouache.effectiveReservoirCapacity());
                     res.reload(dryGouache);
                     MaterialDab dab(engine, gouacheProfile(), coldPressedPaper());
                     dab.apply(store, dryGouache, res, 400.0, 400.0, {0.8, 0.25, 0.2, 0.0});
                 }));

        printRow("Material", "stage6_wash_stroke_600px", "1600x1200 watercolour-wash 600px", 5,
                 benchmarkRuns(1, 5, [&] {
                     MaterialTileStore store(1600, 1200, 64);
                     BrushReservoir res(wash.effectiveReservoirCapacity());
                     res.reload(wash);
                     MaterialStroke stroke(store, wash, res, engine, watercolourProfile(), coldPressedPaper());
                     stroke.begin({160.0, 260.0, {0.8, 0.25, 0.2, 0.0}});
                     stroke.extend({460.0, 290.0, {0.85, 0.30, 0.25, 0.0}});
                     stroke.end({760.0, 320.0, {0.7, 0.20, 0.15, 0.0}});
                     if (stroke.failed()) std::abort();
                 }));

        printRow("Material", "stage6_gouache_stroke_600px", "1600x1200 gouache-round 600px", 7,
                 benchmarkRuns(1, 7, [&] {
                     MaterialTileStore store(1600, 1200, 64);
                     BrushReservoir res(gouache.effectiveReservoirCapacity());
                     res.reload(gouache);
                     MaterialStroke stroke(store, gouache, res, engine, gouacheProfile(), coldPressedPaper());
                     stroke.begin({160.0, 260.0, {0.8, 0.25, 0.2, 0.0}});
                     stroke.extend({460.0, 290.0, {0.85, 0.30, 0.25, 0.0}});
                     stroke.end({760.0, 320.0, {0.7, 0.20, 0.15, 0.0}});
                     if (stroke.failed()) std::abort();
                 }));

        MaterialTileStore stage6Store(1600, 1200, 64);
        {
            BrushReservoir res(wash.effectiveReservoirCapacity());
            res.reload(wash);
            MaterialStroke stroke(stage6Store, wash, res, engine, watercolourProfile(), coldPressedPaper());
            stroke.begin({160.0, 260.0, {0.8, 0.25, 0.2, 0.0}});
            stroke.extend({460.0, 290.0, {0.85, 0.30, 0.25, 0.0}});
            stroke.end({760.0, 320.0, {0.7, 0.20, 0.15, 0.0}});
        }
        const auto activeTiles = stage6Store.activeTilePoints();
        MaterialRenderer renderer(engine);
        std::vector<std::uint32_t> rendered;

        printRow("Material", "stage6_render_active_tiles", "renderTile over 22 active tiles", 15,
                 benchmarkRuns(2, 15, [&] {
                     for (const TilePoint t : activeTiles) {
                         if (!renderer.renderTile(stage6Store, t, {}, watercolourProfile(), coldPressedPaper(), &rendered))
                             std::abort();
                     }
                 }));

        printRow("Material", "stage6_wet_step_0.2s", "advanceWetMaterial(0.2s) 22 tiles", 7,
                 benchmarkRuns(1, 7, [&] {
                     MaterialTileStore copy(1600, 1200, 64);
                     std::vector<MaterialTileSnapshot> snaps;
                     for (const TilePoint t : stage6Store.allocatedTilePoints()) snaps.push_back(stage6Store.snapshot(t));
                     copy.importCompositions(stage6Store.compositions());
                     copy.restoreTiles(snaps, false);
                     const auto st = engine.advanceWetMaterial(copy, 0.2, coldPressedPaper());
                     if (st.failed) std::abort();
                 }));
    }

    // ---- 4. Paint Mixer ----
    {
        PaintMixerModel mixer;
        mixer.addPigmentDeposit("PY3", 1.2);
        mixer.addPigmentDeposit("PR122", 1.1);
        mixer.addPigmentDeposit("PB15", 1.0);
        mixer.addPigmentDeposit("PW6", 0.8);
        const std::string depA = mixer.activeDepositId();
        const std::string depB = mixer.deposits().front().id;
        std::vector<std::uint32_t> surfacePixels;

        printRow("PaintMixer", "mixer_add_water_plus_render", "addWaterToActive + renderSurface 256x160", 9,
                 benchmarkRuns(2, 9, [&] {
                     mixer.addWaterToActive(0.05);
                     if (!mixer.renderSurface(surfacePixels)) std::abort();
                 }));

        printRow("PaintMixer", "mixer_mix_deposits_plus_render", "mixDeposits(new=false) + renderSurface", 9,
                 benchmarkRuns(2, 9, [&] {
                     mixer.mixDeposits(depB, depA, 0.25, false);
                     if (!mixer.renderSurface(surfacePixels)) std::abort();
                 }));

        printRow("PaintMixer", "mixer_render_surface_only", "renderSurface 256x160 (5 deposits)", 15,
                 benchmarkRuns(2, 15, [&] {
                     if (!mixer.renderSurface(surfacePixels)) std::abort();
                 }));

        printRow("PaintMixer", "mixer_10_pigment_swatches", "pigmentPreviewArgb x 10 pigments", 50,
                 benchmarkRuns(5, 50, [&] {
                     std::uint32_t sink = 0;
                     for (const auto& p : mixer.pigments().pigments()) {
                         sink ^= pigmentPreviewArgb(mixer.engine(), p, mixer.activeMediumId());
                     }
                     if (sink == 0xdeadbeefU) std::cout << sink;
                 }));
    }

    // ---- 5. SparseTileStore Snapshot/Restore & DirtyTileTracker ----
    {
        SparseTileStore store(2048, 2048, 64);
        DigitalBrush b;
        b.size = 120.0;
        b.spacing = 0.25;
        b.color = 0xff336699U;
        BrushStroke s(store, b);
        s.begin({200.0, 200.0, 1.0});
        s.end({1200.0, 1200.0, 1.0});
        const auto tilePts = store.allocatedTilePoints();

        printRow("Memory/Undo", "sparse_snapshot_restore", "snapshot + restoreTiles across painted tiles", 15,
                 benchmarkRuns(2, 15, [&] {
                     std::vector<SparseTileStore::TileSnapshot> snaps;
                     snaps.reserve(tilePts.size());
                     for (const TilePoint tp : tilePts) {
                         const auto px = store.tilePixels(tp);
                         snaps.push_back({tp, {px.begin(), px.end()}});
                     }
                     const auto res = store.restoreTiles(snaps, false);
                     if (res == SparseTileStore::WriteResult::AllocationFailed) std::abort();
                 }));

        printRow("Memory/Undo", "dirty_tracker_4096_pixel_marks", "DirtyTileTracker::mark 4096 pixel-marks", 50,
                 benchmarkRuns(5, 50, [&] {
                     DirtyTileTracker dt(TileGeometry(2048, 2048, 64));
                     for (int y = 0; y < 64; ++y)
                         for (int x = 0; x < 64; ++x)
                             dt.mark({100 + x, 100 + y, 101 + x, 101 + y});
                 }));
    }

    std::cout << "=== Benchmark Complete ===\n";
    return 0;
}
