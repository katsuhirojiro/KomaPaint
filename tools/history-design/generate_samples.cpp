// Offline design experiment only: generate before/after tiles with the actual
// CPU brush and sparse store. Not linked into the application.
#include "drawing/BrushEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <span>
#include <vector>
using namespace komapaint;
struct Sample { TilePoint point; bool existed; std::vector<std::uint32_t> before; };
static SparseTileStore::CaptureResult capture(void* context, TilePoint point, bool existed, std::span<const std::uint32_t> pixels) noexcept {
    try {
        auto& samples = *static_cast<std::vector<Sample>*>(context);
        for (auto& s : samples) if (s.point == point) return SparseTileStore::CaptureResult::Ready;
        samples.push_back({point, existed, {pixels.begin(), pixels.end()}});
        return SparseTileStore::CaptureResult::Ready;
    } catch (...) { return SparseTileStore::CaptureResult::AllocationFailed; }
}
static std::uint32_t random32(std::uint32_t& state) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
}
int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const int scenario = std::atoi(argv[1]);
    const int background = std::atoi(argv[2]); // 0 absent, 1 uniform, 2 noise
    SparseTileStore store(3000, 2000);
    if (background) {
        std::vector<SparseTileStore::TileSnapshot> initial;
        std::uint32_t rng = 0x12345678;
        // Only the tiles touched by the sample strokes need a background.
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x) {
            SparseTileStore::TileSnapshot tile{{x,y}, std::vector<std::uint32_t>(65536)};
            for (auto& pixel : tile.pixels) {
                if (background == 1) pixel = 0x80402010U;
                else {
                    // Opaque random colors: valid premultiplied pixels.
                    pixel = 0xff000000U | (random32(rng) & 0x00ffffffU);
                }
            }
            initial.push_back(std::move(tile));
        }
        if (store.restoreTiles(initial) != SparseTileStore::WriteResult::Changed) return 3;
    }
    std::vector<Sample> samples;
    store.setBeforeChange(capture, &samples);
    BrushDefinition b;
    b.color = 0xffd04580U; b.opacity = 0.7; b.flow = 0.8;
    b.size = scenario == 0 ? 4 : scenario == 1 ? 16 : 96;
    BrushStroke stroke(store, b);
    const double x1 = 64, y = 400;
    const double x2 = scenario == 0 ? 64 : scenario == 1 ? 514 : 1264;
    if (!stroke.begin({x1, y, 1}) || !stroke.end({x2, y, 1})) return 4;
    store.setBeforeChange(nullptr, nullptr);
    FILE* f = std::fopen(argv[3], "wb"); if (!f) return 5;
    const auto write = [f](const void* p, std::size_t n) { return std::fwrite(p, 1, n, f) == n; };
    auto count = static_cast<std::uint32_t>(samples.size());
    if (!write(&count, 4)) return 6;
    std::vector<std::uint32_t> zeros(65536, 0);
    for (const auto& s : samples) {
        const auto after = store.tilePixels(s.point);
        std::uint8_t flags = (s.existed ? 1 : 0) | (!after.empty() ? 2 : 0);
        if (!write(&flags, 1) || !write(s.existed ? s.before.data() : zeros.data(), 262144)
            || !write(after.empty() ? zeros.data() : after.data(), 262144)) return 7;
    }
    return std::fclose(f) ? 8 : 0;
}
