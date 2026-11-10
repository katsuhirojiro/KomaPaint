#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Experimental, in-memory tile-record prototype. Not yet used by UndoStack.
// Empty input means an absent tile; a present tile has exactly pixelCount pixels.
namespace komapaint::history_experiment {
struct TileRecord {
    enum class Encoding : std::uint8_t { Delta, RleSnapshots, RawSnapshots };
    Encoding encoding{};
    bool beforePresent = false, afterPresent = false;
    std::uint32_t pixelCount = 0;
    std::vector<std::uint8_t> payload;
};
TileRecord encode(std::span<const std::uint32_t> before,
                  std::span<const std::uint32_t> after, std::uint32_t pixelCount);
// Forward requires the before state; reverse requires the after state.
// Throws invalid_argument for malformed payloads or incompatible input, and
// bad_alloc on exhaustion. Returned empty vector represents an absent tile.
std::vector<std::uint32_t> apply(const TileRecord& record,
                                  std::span<const std::uint32_t> current, bool forward);
}
