#include "drawing/BrushEngine.h"
#include "drawing/StrokeStabilizer.h"
#include "input/PointerInput.h"
#include "material/MaterialEngine.h"
#include "material/MaterialTileStore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>
#include <vector>

using namespace komapaint;
using namespace komapaint::input;

namespace {

// Lightweight global allocation counter to prove zero hot-path allocations
// during steady-state pointer normalization, arbitration, and telemetry recording.
std::atomic<bool> g_countAllocations{false};
std::atomic<std::size_t> g_allocationCount{0};

} // namespace

void* operator new(std::size_t sz)
{
    if (g_countAllocations.load(std::memory_order_relaxed)) {
        g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(sz ? sz : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

namespace {

using Clock = std::chrono::steady_clock;

double quantileUs(std::vector<double> values, double q)
{
    if (values.empty()) return 0.0;
    const std::size_t idx = std::min(values.size() - 1,
        static_cast<std::size_t>(std::floor(q * static_cast<double>(values.size() - 1))));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
    return values[idx];
}

void printStats(const char* label, const std::vector<double>& usSamples, std::size_t allocs = 0)
{
    const double med = quantileUs(usSamples, 0.50);
    const double p95 = quantileUs(usSamples, 0.95);
    const double p99 = quantileUs(usSamples, 0.99);
    std::cout << std::left << std::setw(42) << label
              << " median=" << std::fixed << std::setprecision(3) << std::setw(8) << med << " us"
              << " p95=" << std::setw(8) << p95 << " us"
              << " p99=" << std::setw(8) << p99 << " us"
              << " allocs=" << allocs << '\n';
}

RawPointerEvent makeSampleEvent(PointerPhase phase, double x, double y, double p,
                                std::uint64_t tsNs) noexcept
{
    RawPointerEvent ev;
    ev.deviceId = 42;
    ev.pointerId = 42;
    ev.timestampNs = tsNs;
    ev.pointerType = PointerType::Pen;
    ev.phase = phase;
    ev.backend = InputBackend::WindowsInk;
    ev.viewX = x;
    ev.viewY = y;
    ev.pressure = p;
    ev.tiltX = 22.0;
    ev.tiltY = -14.0;
    ev.rotationDegrees = 48.0;
    ev.buttons = PrimaryButton;
    ev.inProximity = true;
    ev.capabilities.hasPressure = true;
    ev.capabilities.hasTilt = true;
    ev.capabilities.hasRotation = true;
    ev.capabilities.hasHover = true;
    return ev;
}

} // namespace

int main()
{
    std::cout << "=== KomaPaint Stage 10 Input Performance Benchmark ===\n";

    PointerStrokeArbiter arbiter;
    std::vector<double> normLatencies;
    std::vector<double> procLatencies;
    std::vector<double> digitalSampleLatencies;
    std::vector<double> materialSampleLatencies;
    normLatencies.reserve(10000);
    procLatencies.reserve(10000);
    digitalSampleLatencies.reserve(2000);
    materialSampleLatencies.reserve(1000);

    // 1. Steady-state normalization & arbitration (10,000 samples @ 1000 Hz burst),
    // verifying ZERO heap allocations on the input hot path.
    const std::uint64_t baseNs = 1'000'000'000ULL;
    (void)arbiter.process(makeSampleEvent(PointerPhase::Down, 100.0, 100.0, 0.5, baseNs), 100.0, 100.0);

    g_allocationCount.store(0, std::memory_order_relaxed);
    g_countAllocations.store(true, std::memory_order_release);
    for (int i = 1; i <= 10000; ++i) {
        const double x = 100.0 + i * 0.25;
        const double y = 100.0 + std::sin(i * 0.05) * 40.0;
        const double p = 0.2 + 0.7 * std::abs(std::sin(i * 0.02));
        const RawPointerEvent ev = makeSampleEvent(PointerPhase::Move, x, y, p,
                                                   baseNs + static_cast<std::uint64_t>(i) * 1'000'000ULL);
        const auto t0 = Clock::now();
        const auto norm = arbiter.normalize(ev, x, y);
        const auto t1 = Clock::now();
        (void)norm;
        const auto res = arbiter.process(ev, x, y);
        const auto t2 = Clock::now();
        (void)res;
        normLatencies.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        procLatencies.push_back(std::chrono::duration<double, std::micro>(t2 - t1).count());
    }
    g_countAllocations.store(false, std::memory_order_release);
    const std::size_t hotPathAllocs = g_allocationCount.load(std::memory_order_relaxed);
    (void)arbiter.process(makeSampleEvent(PointerPhase::Up, 2600.0, 100.0, 0.0, baseNs + 10'001'000'000ULL),
                          2600.0, 100.0);

    printStats("normalize_10k_burst_1000Hz", normLatencies, hotPathAllocs);
    printStats("arbiter_process_10k_burst_1000Hz", procLatencies, hotPathAllocs);

    // 2. End-to-end Digital sample -> stabilize -> interpolate -> pixel commit latency (2000 samples)
    {
        SparseTileStore store(2048, 2048, 64);
        BrushDefinition def;
        def.size = 24.0;
        def.spacing = 0.15;
        def.color = 0xff102030U;
        def.pressureSize = 0.8;
        def.pressureOpacity = 0.8;
        def.tiltSize = 0.4;
        def.tiltAngle = true;
        BrushStroke stroke(store, def);
        StrokeStabilizer stab(20);
        arbiter.reset();

        const auto d0 = arbiter.process(makeSampleEvent(PointerPhase::Down, 100.0, 500.0, 0.5, baseNs), 100.0, 500.0);
        stab.begin({d0.sample.docX, d0.sample.docY, d0.sample.pressure,
                    d0.sample.tiltX, d0.sample.tiltY, d0.sample.rotationRadians,
                    0.0, true, true});
        stroke.begin({d0.sample.docX, d0.sample.docY, d0.sample.pressure,
                      d0.sample.tiltX, d0.sample.tiltY, d0.sample.rotationRadians,
                      0.0, true, true});

        for (int i = 1; i <= 2000; ++i) {
            const double x = 100.0 + (i % 1500) * 1.1;
            const double y = 500.0 + std::sin(i * 0.04) * 120.0;
            const auto ev = makeSampleEvent(PointerPhase::Move, x, y, 0.25 + 0.65 * ((i % 50) / 50.0),
                                            baseNs + static_cast<std::uint64_t>(i) * 1'000'000ULL);
            const auto t0 = Clock::now();
            const auto res = arbiter.process(ev, x, y);
            const InputSample filt = stab.filter({res.sample.docX, res.sample.docY, res.sample.pressure,
                                                  res.sample.tiltX, res.sample.tiltY, res.sample.rotationRadians,
                                                  0.0, true, true});
            stroke.extend(filt);
            const auto t1 = Clock::now();
            digitalSampleLatencies.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        stroke.end({1750.0, 500.0, 0.1});
    }
    printStats("digital_sample_to_pixel_24px_2000pts", digitalSampleLatencies);

    // 3. End-to-end Realistic material sample -> stroke -> dab commit latency (600 samples)
    {
        material::PigmentLibrary pigments;
        material::MaterialEngine engine(pigments);
        auto presets = material::defaultBrushDefinitions();
        material::BrushDefinition matDef = presets.front();
        matDef.size = 18.0;
        material::BrushReservoir reservoir(matDef.effectiveReservoirCapacity());
        reservoir.reload(matDef);
        material::MaterialTileStore matStore(1024, 1024, 64);
        material::MaterialStroke matStroke(matStore, matDef, reservoir, engine,
                                           material::gouacheProfile(), material::coldPressedPaper(), false);
        arbiter.reset();

        const auto m0 = arbiter.process(makeSampleEvent(PointerPhase::Down, 100.0, 300.0, 0.6, baseNs), 100.0, 300.0);
        matStroke.begin({m0.sample.docX, m0.sample.docY,
                         {m0.sample.pressure, m0.sample.speedNormalized,
                          m0.sample.tiltMagnitude, m0.sample.rotationRadians}});
        for (int i = 1; i <= 600; ++i) {
            const double x = 100.0 + i * 1.2;
            const double y = 300.0 + std::sin(i * 0.08) * 50.0;
            const auto ev = makeSampleEvent(PointerPhase::Move, x, y, 0.35 + 0.55 * ((i % 40) / 40.0),
                                            baseNs + static_cast<std::uint64_t>(i) * 2'000'000ULL);
            const auto t0 = Clock::now();
            const auto res = arbiter.process(ev, x, y);
            matStroke.extend({res.sample.docX, res.sample.docY,
                              {res.sample.pressure, res.sample.speedNormalized,
                               res.sample.tiltMagnitude, res.sample.rotationRadians}});
            const auto t1 = Clock::now();
            materialSampleLatencies.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        matStroke.end({820.0, 300.0, {0.1, 0.1, 0.2, 0.0}});
    }
    printStats("material_sample_to_cell_18px_600pts", materialSampleLatencies);

    if (hotPathAllocs != 0) {
        std::cerr << "ERROR: expected 0 hot-path heap allocations during steady-state normalization/process, got "
                  << hotPathAllocs << '\n';
        return 1;
    }
    std::cout << "test_input_performance_benchmark: PASS (0 hot-path heap allocations)\n";
    return 0;
}
