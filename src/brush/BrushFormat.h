#pragma once
// The native KomaPaint brush file: `.kb`.
//
// A `.kb` holds one brush definition and everything it needs: components,
// groups, generators, dynamics, randomization, the exposed Sub-Tool settings
// and the pixel resources they share. It is the authoritative format; the
// library file stays a library.
//
// Layout (little-endian):
//
//   "KOMABRSH"  u16 version  u16 flags  u32 payloadCrc32  payload
//   payload = [ tag[4] u32 length bytes ]*
//
// Chunks are independent, so a newer build can add one and an older build
// preserves its bytes without interpreting them. A chunk we *do* know that is the
// wrong size, or that fails validation, is a hard error: a brush is never
// half-loaded.
#include "brush/BrushModel.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace komapaint::brush {

inline constexpr std::uint16_t kBrushFormatVersion = 1;
inline constexpr char kBrushFormatMagic[8] = {'K', 'O', 'M', 'A', 'B', 'R', 'S', 'H'};

struct DecodeResult {
    bool ok = false;
    std::string error;                    // empty when ok
    std::vector<std::string> ignoredChunks; // tags preserved opaquely, not interpreted
    std::uint16_t version = 0;
};

// Serialises deterministically: same document in, byte-identical file out.
std::vector<std::uint8_t> encodeBrush(const BrushDocument& document);

// Parses into `out` only on success; a failed decode leaves `out` untouched.
bool decodeBrush(const std::uint8_t* data, std::size_t size, BrushDocument* out,
                 DecodeResult* result);

// Validates and replaces the file atomically using a temporary sibling. A
// failed save leaves any existing destination untouched.
bool saveBrushFile(const std::filesystem::path& path, const BrushDocument& document,
                   std::string* error = nullptr);
bool loadBrushFile(const std::filesystem::path& path, BrushDocument* out, DecodeResult* result,
                   std::string* error = nullptr);

// Exposed for tests: CRC-32 (IEEE 802.3) over an arbitrary byte range.
std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

} // namespace komapaint::brush
