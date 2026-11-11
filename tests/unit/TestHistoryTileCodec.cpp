#include "history-codec/HistoryTileCodec.h"
#include "drawing/BrushEngine.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <random>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
using namespace komapaint::history_experiment;
static int failAfter = -1;
void* operator new(std::size_t n) {
    if (failAfter == 0) { failAfter = -1; throw std::bad_alloc(); }
    if (failAfter > 0) --failAfter;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#define CHECK(...) do { if (!(__VA_ARGS__)) { failAfter = -1; std::cerr << "FAIL " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::exit(1); } } while (false)
static void roundtrip(const std::vector<std::uint32_t>& before, const std::vector<std::uint32_t>& after, std::uint32_t n) {
    const auto record = encode(before, after, n);
    CHECK(apply(record, before, true) == after);
    CHECK(apply(record, after, false) == before);
}
#ifdef _WIN32
struct Memory { unsigned long long privateBytes, working, peak; };
static Memory memory() {
    PROCESS_MEMORY_COUNTERS_EX p{}; p.cb = sizeof(p);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p), sizeof(p))) return {};
    return {p.PrivateUsage, p.WorkingSetSize, p.PeakWorkingSetSize};
}
#else
struct Memory { unsigned long long privateBytes, working, peak; };
static Memory memory() { struct rusage r{}; getrusage(RUSAGE_SELF, &r); return {0, 0, static_cast<unsigned long long>(r.ru_maxrss) * 1024}; }
#endif
// Differential replay against actual sparse-store tiles from a brush stroke.
static void realBrushRoundTrip() {
    using namespace komapaint;
    using R = SparseTileStore::WriteResult;
    SparseTileStore layer(768, 128), other(768, 128);
    CHECK(layer.setPixel({10, 20}, 0x80402010U) == R::Changed);
    CHECK(other.setPixel({10, 20}, 0xff123456U) == R::Changed);
    std::vector<std::vector<std::uint32_t>> before;
    for (int x = 0; x < 3; ++x) {
        auto pixels = layer.tilePixels({x, 0}); before.emplace_back(pixels.begin(), pixels.end());
    }
    BrushDefinition brush; brush.size = 16; brush.opacity = 0.5; brush.flow = 0.8;
    brush.color = 0xffd04580U; brush.tip = BrushTip::Textured;
    BrushStroke stroke(layer, brush);
    CHECK(stroke.begin({10, 20, 0.7})); CHECK(stroke.end({530, 20, 0.7}));
    std::vector<std::vector<std::uint32_t>> after;
    std::vector<TileRecord> records;
    std::size_t encoded = 0;
    for (int x = 0; x < 3; ++x) {
        auto pixels = layer.tilePixels({x, 0}); after.emplace_back(pixels.begin(), pixels.end());
        records.push_back(encode(before[x], after[x], 256 * 256));
        encoded += records.back().payload.size();
        CHECK(apply(records.back(), before[x], true) == after[x]);
        CHECK(apply(records.back(), after[x], false) == before[x]);
    }
    CHECK(encoded < 3 * 2 * 256 * 256 * sizeof(std::uint32_t));
    std::vector<SparseTileStore::TileSnapshot> undo;
    for (int x = 0; x < 3; ++x) undo.push_back({{x, 0}, apply(records[x], after[x], false)});
    CHECK(layer.restoreTiles(undo) == R::Changed);
    CHECK(layer.allocatedTiles() == 1);
    for (int x = 0; x < 3; ++x) {
        auto pixels = layer.tilePixels({x, 0});
        CHECK(std::equal(pixels.begin(), pixels.end(), before[x].begin(), before[x].end()));
    }
    CHECK(other.pixel({10, 20}) == 0xff123456U);
    std::vector<SparseTileStore::TileSnapshot> redo;
    for (int x = 0; x < 3; ++x) redo.push_back({{x, 0}, apply(records[x], before[x], true)});
    CHECK(layer.restoreTiles(redo) == R::Changed);
    CHECK(layer.allocatedTiles() == 3);
    for (int x = 0; x < 3; ++x) {
        auto pixels = layer.tilePixels({x, 0});
        CHECK(std::equal(pixels.begin(), pixels.end(), after[x].begin(), after[x].end()));
    }
    CHECK(other.pixel({10, 20}) == 0xff123456U);
    std::cout << "Real brush across three tiles, experimental encoded payload=" << encoded << " bytes\n";
}
int main() {
    realBrushRoundTrip();
    constexpr std::uint32_t n = 256 * 256;
    std::vector<std::uint32_t> empty, before(n, 0), after = before;
    after[42] = 0x80402010U;
    auto sparse = encode(before, after, n);
    CHECK(sparse.encoding == TileRecord::Encoding::Delta && sparse.payload.size() == 12);
    roundtrip(before, after, n); roundtrip(empty, after, n); roundtrip(after, empty, n); roundtrip(empty, empty, n);
    auto dense = std::vector<std::uint32_t>(n, 0x80402010U);
    auto solid = encode(before, dense, n);
    CHECK(solid.encoding == TileRecord::Encoding::RleSnapshots && solid.payload.size() == 16);
    roundtrip(before, dense, n); roundtrip(empty, dense, n); roundtrip(dense, empty, n);
    std::mt19937 rng(0x12345678);
    for (auto& p : before) p = rng();
    for (auto& p : after) p = rng();
    auto raw = encode(before, after, n);
    CHECK(raw.encoding == TileRecord::Encoding::RawSnapshots && raw.payload.size() == std::size_t(n) * 8);
    roundtrip(before, after, n);
    // Deterministic varied changes, including arbitrary bit patterns and alpha.
    for (int trial = 0; trial < 300; ++trial) {
        const std::uint32_t size = 1 + rng() % 1024;
        std::vector<std::uint32_t> a(size), b(size);
        for (auto& p : a) p = rng();
        b = a;
        for (unsigned i = 0; i < static_cast<unsigned>(trial % 40); ++i) b[rng() % size] = rng();
        if (trial % 7 == 0) a.clear();
        if (trial % 11 == 0) b.clear();
        roundtrip(a, b, size);
    }
    auto corrupt = sparse; corrupt.payload.pop_back();
    bool rejected = false;
    try { (void)apply(corrupt, std::vector<std::uint32_t>(n, 0), true); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    // Failure while constructing a record or replay output never modifies input.
    std::vector<std::uint32_t> original(n, 0); auto changed = original; changed[3] = 0x80800000;
    failAfter = 0; rejected = false;
    try { (void)encode(original, changed, n); } catch (const std::bad_alloc&) { rejected = true; }
    failAfter = -1; CHECK(rejected && original[3] == 0 && changed[3] == 0x80800000);
    auto sample = encode(original, changed, n);
    failAfter = 0; rejected = false;
    try { (void)apply(sample, original, true); } catch (const std::bad_alloc&) { rejected = true; }
    failAfter = -1; CHECK(rejected && original[3] == 0);
    auto t0 = std::chrono::steady_clock::now();
    std::size_t sum = 0;
    for (int i = 0; i < 100; ++i) sum += encode(original, changed, n).payload.size();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(sum == 1200);
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        auto replayed = apply(sample, original, true);
        CHECK(replayed[3] == 0x80800000);
    }
    auto replayElapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    const auto base = memory();
    std::vector<TileRecord> records; records.reserve(10000);
    for (int i = 0; i < 10000; ++i) records.push_back(sample);
    std::size_t payloadCapacity = 0;
    for (const auto& r : records) payloadCapacity += r.payload.capacity();
    auto afterMemory = memory();
    std::cout << "::notice title=Experimental compact codec::10000 copied one-pixel records payload capacity=" << payloadCapacity
              << " bytes; record vector capacity=" << records.capacity() * sizeof(TileRecord)
              << " bytes; 100 encodes=" << elapsed << " us; 100 forward decodes=" << replayElapsed
              << " us; process private before=" << base.privateBytes
              << " after=" << afterMemory.privateBytes << "; working after=" << afterMemory.working
              << "; peak working=" << afterMemory.peak << "; prototype only, not UndoStack\n";
    std::cout << "Experimental tile codec tests passed\n";
}
