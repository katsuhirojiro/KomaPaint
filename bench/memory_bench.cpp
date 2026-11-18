// Memory behaviour harness for the painting session.
//
// Covers the four ways an editor leaks or balloons: a long painting session,
// a large canvas with many layers, repeated project open/close, and a long
// undo history being walked back and forth. Numbers are the process's own
// resident pages where the platform exposes them, plus the shapes the engine
// reports itself, so a growth curve can be told apart from allocator noise.
#include "canvas/SparseTileStore.h"
#include "drawing/BrushEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <vector>

#if defined(__linux__)
#include <cstdio>
#endif

using namespace komapaint;

namespace {

std::size_t residentBytes()
{
#if defined(__linux__)
    if (std::FILE* f = std::fopen("/proc/self/statm", "r")) {
        long pages = 0, resident = 0;
        const int read = std::fscanf(f, "%ld %ld", &pages, &resident);
        std::fclose(f);
        if (read == 2) return std::size_t(resident) * 4096;
    }
#endif
    return 0;
}

void printMemory(const char* label, std::size_t baseline)
{
    const std::size_t now = residentBytes();
    if (now == 0) {
        std::printf("%-42s (resident pages not exposed on this platform)\n", label);
        return;
    }
    const double delta = (double(std::int64_t(now)) - double(std::int64_t(baseline)))
                         / (1024.0 * 1024.0);
    std::printf("%-42s %8.1f MB resident (%+.1f MB since baseline)\n", label,
                double(now) / (1024.0 * 1024.0), delta);
}

// One stroke of `dabs` dabs across a canvas, written through the same coalesced
// tile API the editor uses.
std::size_t paintStroke(SparseTileStore& store, int seed, double y, int dabs = 60)
{
    BrushDefinition def;
    def.size = 48.0;
    def.hardness = 0.8;
    def.color = 0xff000000u | std::uint32_t(seed * 2654435761u & 0xffffffu);
    BrushStroke stroke(store, def);
    stroke.begin({40.0, y, 1.0});
    for (int i = 1; i < dabs; ++i)
        stroke.extend({40.0 + 12.0 * i, y + 60.0 * std::sin(double(i) * 0.2), 0.9});
    stroke.end({40.0 + 12.0 * (dabs - 1), y, 1.0});
    return 0;
}

} // namespace

int main()
{
    std::puts("--- long painting session (2048x2048, 1 layer) ---");
    std::size_t baseline = 0;
    {
        SparseTileStore store(2048, 2048);
        baseline = residentBytes();
        printMemory("start", baseline);
        for (int i = 0; i < 200; ++i) {
            paintStroke(store, i, 20.0 + double(i % 40) * 45.0);
            if (i == 49) printMemory("after 50 strokes", baseline);
            if (i == 199) printMemory("after 200 strokes", baseline);
        }
        std::printf("%-42s %8.1f MB in tiles (%zu tiles)\n", "store tile bytes",
                    double(store.allocatedPixelBytes()) / (1024.0 * 1024.0), store.allocatedTiles());
    }

    std::puts("\n--- large canvas, many layers (4096x4096, 8 layers) ---");
    {
        const std::size_t before = residentBytes();
        std::vector<std::unique_ptr<SparseTileStore>> layers;
        for (int i = 0; i < 8; ++i) {
            layers.push_back(std::make_unique<SparseTileStore>(4096, 4096));
            paintStroke(*layers.back(), i, 100.0 + 200.0 * i, 120);
        }
        printMemory("8 layers, one stroke each", before);
        layers.clear();
        printMemory("layers released", before);
    }

    std::puts("\n--- repeated project open/close (1600x1600) ---");
    {
        const std::size_t before = residentBytes();
        for (int cycle = 0; cycle < 30; ++cycle) {
            SparseTileStore store(1600, 1600);
            for (int i = 0; i < 6; ++i) paintStroke(store, cycle * 7 + i, 80.0 + 200.0 * i);
            if (cycle == 9) printMemory("after 10 cycles", before);
        }
        printMemory("after 30 cycles", before);
    }

    std::puts("\n--- undo-style snapshot churn (1024x1024) ---");
    {
        // What the undo stack keeps per stroke: the before/after tiles.
        const std::size_t before = residentBytes();
        std::vector<std::vector<std::uint32_t>> snapshots;
        SparseTileStore store(1024, 1024);
        for (int i = 0; i < 300; ++i) {
            paintStroke(store, i, 100.0 + double(i % 8) * 100.0, 30);
            // Snapshot the tiles the stroke touched, like a compact command.
            for (int t = 0; t < 4; ++t) {
                const auto pixels = store.tilePixels({t, (i % 4)});
                if (!pixels.empty()) snapshots.emplace_back(pixels.begin(), pixels.end());
            }
        }
        printMemory("300 stroke snapshots retained", before);
        snapshots.clear();
        snapshots.shrink_to_fit();
        printMemory("history released", before);
    }
    return 0;
}
