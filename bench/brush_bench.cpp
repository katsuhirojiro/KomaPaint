// Local (Qt-free) workload harness for the digital brush path: one continuous
// stroke, dabs spaced by the brush's own spacing, written through the coalesced
// tile-edit API the application uses.
#include "canvas/SparseTileStore.h"
#include "drawing/BrushEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

using namespace komapaint;
using Clock = std::chrono::steady_clock;

static std::atomic<bool> g_count{false};
static std::atomic<std::size_t> g_allocs{0};

void* operator new(std::size_t sz)
{
    if (g_count.load(std::memory_order_relaxed)) g_allocs.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(sz ? sz : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

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

struct Case { const char* name; double size; double spacing; double hardness; BrushTip tip; double blend; };

int main()
{
    const Case cases[] = {
        { "small brush 12px", 12, 0.15, 1.0, BrushTip::Round, 0.0 },
        { "medium brush 60px", 60, 0.15, 1.0, BrushTip::Round, 0.0 },
        { "large brush 200px", 200, 0.15, 1.0, BrushTip::Round, 0.0 },
        { "large soft brush 200px", 200, 0.15, 0.2, BrushTip::Round, 0.0 },
        { "large textured brush 200px", 200, 0.15, 1.0, BrushTip::Textured, 0.0 },
        { "large smudge brush 200px", 200, 0.15, 1.0, BrushTip::Round, 0.6 },
        { "huge brush 400px", 400, 0.15, 1.0, BrushTip::Round, 0.0 },
    };

    const int rounds = 6;
    const int samples = 700;
    std::printf("%d strokes of %d input samples, 2048x2048 canvas\n", rounds, samples);

    for (const auto& c : cases) {
        std::vector<double> totals;
        std::size_t allocs = 0;
        for (int r = 0; r < rounds; ++r) {
            SparseTileStore store(2048, 2048);
            BrushDefinition def;
            def.size = c.size;
            def.spacing = c.spacing;
            def.hardness = c.hardness;
            def.tip = c.tip;
            def.blendStrength = c.blend;
            def.color = 0xff3366ccU;
            def.opacity = 1.0;
            BrushStroke stroke(store, def);
            const double stepX = 1600.0 / double(samples - 1);
            g_allocs.store(0, std::memory_order_relaxed);
            const auto start = Clock::now();
            g_count.store(true, std::memory_order_relaxed);
            stroke.begin({100.0, 300.0, 1.0});
            for (int i = 1; i < samples - 1; ++i) {
                // A gentle sine wave, so the stroke crosses tile boundaries.
                const double y = 300.0 + 260.0 * std::sin(double(i) * 0.02);
                stroke.extend({100.0 + stepX * i, y, 0.6 + 0.4 * std::sin(double(i) * 0.11)});
            }
            stroke.end({100.0 + stepX * (samples - 1), 300.0, 1.0});
            g_count.store(false, std::memory_order_relaxed);
            const auto end = Clock::now();
            totals.push_back(std::chrono::duration<double, std::milli>(end - start).count());
            allocs = g_allocs.load(std::memory_order_relaxed);
        }
        double sum = 0; for (double t : totals) sum += t;
        const double mean = sum / double(totals.size());
        std::printf("%-30s p50 %7.2f ms  p95 %7.2f ms  mean %7.2f ms  %6.1f us/input  allocs %zu\n",
                    c.name, pct(totals, 0.5), pct(totals, 0.95), mean,
                    mean * 1000.0 / double(samples), allocs);
    }
    return 0;
}
