// Viewport cost harness: what one canvas update costs on the CPU, and what the
// smallest possible full-texture upload would cost.
//
// The editor's canvas item keeps one frame image and repaints only the dirty
// rectangle into it, so its CPU work is bounded by the dirty tiles. Qt then
// uploads the item's whole texture on any update, whatever the dirty rect was,
// so the upload volume is the frame size times four bytes - that floor is
// measured here as a memcpy of the same size, which is the cheapest a driver
// can possibly move those bytes.
#include "canvas/CpuTileCompositor.h"
#include "canvas/SparseTileStore.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace komapaint;
using Clock = std::chrono::steady_clock;

static double median(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static double timeMemcpy(std::size_t bytes, int rounds)
{
    std::vector<std::uint8_t> src(bytes, 0x5a), dst(bytes, 0);
    std::vector<double> samples;
    samples.reserve(rounds);
    for (int i = 0; i < rounds; ++i) {
        const auto start = Clock::now();
        std::memcpy(dst.data(), src.data(), bytes);
        samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        dst[std::size_t(i) % bytes] = 0; // defeat any clever optimisation
    }
    return median(samples);
}

int main()
{
    std::puts("--- full-texture upload floor (memcpy of one frame) ---");
    struct Frame { const char* name; int w; int h; };
    const Frame frames[] = {
        {"1280x720", 1280, 720},
        {"1920x1080", 1920, 1080},
        {"2560x1440", 2560, 1440},
        {"3840x2160", 3840, 2160},
    };
    for (const auto& f : frames) {
        const std::size_t bytes = std::size_t(f.w) * std::size_t(f.h) * 4;
        const double ms = timeMemcpy(bytes, 40);
        std::printf("%-12s %6.1f MB  %7.3f ms/copy  %6.1f GB/s\n", f.name,
                    double(bytes) / (1024.0 * 1024.0), ms,
                    double(bytes) / (ms * 1e6));
    }

    std::puts("\n--- per-update CPU work: compositing dirty tiles (256 px tiles, 4 layers) ---");
    const int tileSize = 256;
    TileGeometry geometry(4096, 4096, tileSize);
    if (!geometry.isValid()) { std::puts("geometry failed"); return 1; }

    std::vector<std::unique_ptr<SparseTileStore>> stores;
    std::vector<CompositeLayer> layers;
    for (int i = 0; i < 4; ++i) {
        stores.push_back(std::make_unique<SparseTileStore>(4096, 4096, tileSize));
        auto& store = *stores.back();
        for (int y = 0; y < tileSize; ++y)
            for (int x = 0; x < tileSize; ++x)
                store.setPixel({x, y}, 0xff000000u | (std::uint32_t(x * 3 + i) << 16)
                                                 | (std::uint32_t(y * 5) << 8) | 0x40u);
        CompositeLayer layer;
        layer.store = &store;
        layers.push_back(layer);
    }

    std::vector<std::uint32_t> out;
    std::int64_t w = 0, h = 0;
    for (int dirtyTiles : {1, 2, 4, 12, 30, 64}) {
        std::vector<double> samples;
        for (int round = 0; round < 30; ++round) {
            const auto start = Clock::now();
            for (int t = 0; t < dirtyTiles; ++t)
                CpuTileCompositor::composeInto(geometry, TilePoint{t % 16, t / 16}, layers,
                                               0xff202020u, {}, out, w, h);
            samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        }
        const double ms = median(samples);
        std::printf("%2d dirty tiles  %7.3f ms/frame  %5.1f%% of a 16.7 ms frame  (%.1f MB of tile pixels)\n",
                    dirtyTiles, ms, 100.0 * ms / 16.7,
                    double(dirtyTiles) * tileSize * tileSize * 4.0 / (1024.0 * 1024.0));
    }
    return 0;
}
