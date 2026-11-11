#include "HistoryTileCodec.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace komapaint::history_experiment {
namespace {
void put(std::vector<std::uint8_t>& out, std::uint32_t x) {
    for (unsigned shift = 0; shift != 32; shift += 8) out.push_back(static_cast<std::uint8_t>(x >> shift));
}
std::uint32_t get(const std::vector<std::uint8_t>& in, std::size_t& pos) {
    if (pos > in.size() || in.size() - pos < 4) throw std::invalid_argument("truncated tile record");
    std::uint32_t x = 0;
    for (unsigned shift = 0; shift != 32; shift += 8) x |= std::uint32_t(in[pos++]) << shift;
    return x;
}
std::size_t runs(std::span<const std::uint32_t> pixels) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < pixels.size();) {
        const auto value = pixels[i++];
        while (i < pixels.size() && pixels[i] == value) ++i;
        ++n;
    }
    return n;
}
void writeRle(std::vector<std::uint8_t>& out, std::span<const std::uint32_t> pixels) {
    for (std::size_t i = 0; i < pixels.size();) {
        const auto start = i;
        const auto value = pixels[i++];
        while (i < pixels.size() && pixels[i] == value) ++i;
        put(out, static_cast<std::uint32_t>(i - start));
        put(out, value);
    }
}
void readSnapshot(const TileRecord& r, bool present, std::size_t& pos, std::vector<std::uint32_t>& out) {
    if (!present) return;
    out.reserve(r.pixelCount);
    if (r.encoding == TileRecord::Encoding::RawSnapshots) {
        for (std::uint32_t i = 0; i < r.pixelCount; ++i) out.push_back(get(r.payload, pos));
    } else {
        while (out.size() < r.pixelCount) {
            const auto count = get(r.payload, pos);
            const auto value = get(r.payload, pos);
            if (!count || count > r.pixelCount - out.size()) throw std::invalid_argument("bad RLE run");
            out.insert(out.end(), count, value);
        }
    }
}
}
TileRecord encode(std::span<const std::uint32_t> before, std::span<const std::uint32_t> after, std::uint32_t count) {
    if (!count || (!before.empty() && before.size() != count) || (!after.empty() && after.size() != count))
        throw std::invalid_argument("invalid tile size");
    // The worst candidate is two RLE runs per pixel, eight bytes each.
    if (count > std::numeric_limits<std::size_t>::max() / 16) throw std::length_error("tile too large");
    std::size_t changes = 0;
    for (std::size_t i = 0; i < count; ++i)
        if ((before.empty() ? 0 : before[i]) != (after.empty() ? 0 : after[i])) ++changes;
    const std::size_t deltaBytes = changes * 12;
    const std::size_t rawBytes = (before.size() + after.size()) * 4;
    const std::size_t rleBytes = (runs(before) + runs(after)) * 8;
    TileRecord result;
    result.beforePresent = !before.empty(); result.afterPresent = !after.empty(); result.pixelCount = count;
    result.encoding = TileRecord::Encoding::Delta;
    auto bytes = deltaBytes;
    if (rleBytes < bytes) { bytes = rleBytes; result.encoding = TileRecord::Encoding::RleSnapshots; }
    if (rawBytes < bytes) { bytes = rawBytes; result.encoding = TileRecord::Encoding::RawSnapshots; }
    result.payload.reserve(bytes);
    if (result.encoding == TileRecord::Encoding::Delta) {
        for (std::size_t i = 0; i < count; ++i) {
            const auto a = before.empty() ? 0 : before[i];
            const auto b = after.empty() ? 0 : after[i];
            if (a != b) { put(result.payload, static_cast<std::uint32_t>(i)); put(result.payload, a); put(result.payload, b); }
        }
    } else if (result.encoding == TileRecord::Encoding::RleSnapshots) {
        writeRle(result.payload, before); writeRle(result.payload, after);
    } else {
        for (auto p : before) put(result.payload, p);
        for (auto p : after) put(result.payload, p);
    }
    return result;
}
std::vector<std::uint32_t> apply(const TileRecord& r, std::span<const std::uint32_t> current, bool forward) {
    if (!r.pixelCount) throw std::invalid_argument("invalid tile size");
    const bool currentPresent = forward ? r.beforePresent : r.afterPresent;
    const bool targetPresent = forward ? r.afterPresent : r.beforePresent;
    if (currentPresent ? current.size() != r.pixelCount : !current.empty()) throw std::invalid_argument("incompatible tile");
    std::size_t pos = 0;
    if (r.encoding == TileRecord::Encoding::Delta) {
        if (r.payload.size() % 12) throw std::invalid_argument("bad delta size");
        std::vector<std::uint32_t> out(r.pixelCount, 0);
        if (currentPresent) std::copy(current.begin(), current.end(), out.begin());
        std::uint32_t prev = 0; bool first = true;
        while (pos < r.payload.size()) {
            const auto index = get(r.payload, pos), before = get(r.payload, pos), after = get(r.payload, pos);
            if (index >= r.pixelCount || (!first && index <= prev) || out[index] != (forward ? before : after)) throw std::invalid_argument("bad delta");
            out[index] = forward ? after : before; first = false; prev = index;
        }
        if (!targetPresent) {
            if (std::any_of(out.begin(), out.end(), [](std::uint32_t p) { return p != 0; })) throw std::invalid_argument("non-empty absent tile");
            out.clear();
        }
        return out;
    }
    if (r.encoding != TileRecord::Encoding::RleSnapshots && r.encoding != TileRecord::Encoding::RawSnapshots) throw std::invalid_argument("unknown encoding");
    std::vector<std::uint32_t> before, after;
    readSnapshot(r, r.beforePresent, pos, before); readSnapshot(r, r.afterPresent, pos, after);
    if (pos != r.payload.size()) throw std::invalid_argument("trailing tile record data");
    const auto& expected = forward ? before : after;
    if (!std::equal(expected.begin(), expected.end(), current.begin(), current.end())) throw std::invalid_argument("incompatible pixels");
    return forward ? std::move(after) : std::move(before);
}
}
