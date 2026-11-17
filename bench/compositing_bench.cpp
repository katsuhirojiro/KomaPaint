// Local (Qt-free) workload harness for the compositing hot path.
// Mirrors what the viewport does per frame per visible tile: fill the output
// with the background, then source-over each visible layer from bottom to top.
#include "canvas/CpuTileCompositor.h"
#include "canvas/SparseTileStore.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <memory>
#include <vector>

using namespace komapaint;
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

struct Case {
    std::string name;
    int layers;
    bool opaqueSource;
    double coverage;   // fraction of tile painted, as a centred disc
    double opacity;    // layer opacity
    BlendMode mode;
};

static std::uint32_t pixelAt(int x, int y, bool opaqueSource)
{
    const unsigned r = unsigned((x * 7 + y * 3) & 255);
    const unsigned g = unsigned((x * 3 + y * 11) & 255);
    const unsigned b = unsigned((x * 13 + y * 5) & 255);
    if (opaqueSource) return 0xff000000u | (r << 16) | (g << 8) | b;
    const unsigned a = 128;
    return (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
}

int main()
{
    const std::vector<Case> cases = {
        { "1 opaque layer, disc 12% of tile", 1, true, 0.12, 1.0, BlendMode::Normal },
        { "4 opaque layers, disc 12% of tile", 4, true, 0.12, 1.0, BlendMode::Normal },
        { "4 opaque layers, full coverage", 4, true, 1.0, 1.0, BlendMode::Normal },
        { "4 half-alpha layers, disc 40% of tile", 4, false, 0.40, 1.0, BlendMode::Normal },
        { "4 layers at 50% opacity, disc 40%", 4, false, 0.40, 0.5, BlendMode::Normal },
        { "4 Multiply layers, disc 40% of tile", 4, false, 0.40, 1.0, BlendMode::Multiply },
        { "8 opaque layers, disc 30% of tile", 8, true, 0.30, 1.0, BlendMode::Normal },
    };

    const int tileSize = 256;
    TileGeometry geometry(tileSize, tileSize, tileSize);
    if (!geometry.isValid()) { std::puts("geometry failed"); return 1; }

    const int rounds = 120;
    std::printf("tile %dx%d, %d compositions per case\n", tileSize, tileSize, rounds);

    for (const auto& c : cases) {
        std::vector<std::unique_ptr<SparseTileStore>> stores;
        stores.reserve(std::size_t(c.layers));
        for (int i = 0; i < c.layers; ++i) stores.push_back(std::make_unique<SparseTileStore>(tileSize, tileSize));
        std::vector<std::vector<std::uint32_t>> overrides;
        std::vector<CompositeLayer> layers;
        std::mt19937 rng(1234);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        for (int i = 0; i < c.layers; ++i) {
            auto& store = *stores[std::size_t(i)];
            // Paint the requested coverage into the single tile (0,0).
            // A contiguous blob, like a brush mark, rather than random pixels:
            // random coverage mispredicts the branch on every pixel and is not
            // what a canvas actually contains.
            const double radius = std::sqrt(c.coverage / 3.14159265358979) * tileSize / 2.0;
            const double cx = tileSize / 2.0, cy = tileSize / 2.0;
            for (int y = 0; y < tileSize; ++y) {
                for (int x = 0; x < tileSize; ++x) {
                    const double dx = x - cx, dy = y - cy;
                    if (dx * dx + dy * dy > radius * radius) continue;
                    if (c.coverage < 0.3 && unit(rng) < 0.15) continue; // ragged edge
                    store.setPixel({x, y}, pixelAt(x + i, y, c.opaqueSource));
                }
            }
            CompositeLayer layer;
            layer.store = &store;
            layer.opacity = c.opacity;
            layer.blendMode = c.mode;
            layers.push_back(layer);
        }

        std::vector<std::uint32_t> out;
        std::int64_t w = 0, h = 0;
        std::vector<double> samples;
        samples.reserve(rounds);
        for (int r = 0; r < rounds; ++r) {
            const auto start = Clock::now();
            if (!CpuTileCompositor::composeInto(geometry, TilePoint{0, 0}, layers, 0xff202020u,
                                                {}, out, w, h)) {
                std::puts("compose failed"); return 1;
            }
            const auto end = Clock::now();
            samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }
        const double p50 = pct(samples, 0.50), p95 = pct(samples, 0.95), p99 = pct(samples, 0.99);
        double total = 0; for (double s : samples) total += s;
        const double mean = total / double(samples.size());
        std::printf("%-44s p50 %6.3f ms  p95 %6.3f ms  p99 %6.3f ms  mean %6.3f ms  %5.1f ns/px\n",
                    c.name.c_str(), p50, p95, p99, mean, mean * 1e6 / double(w * h));
    }
    return 0;
}
