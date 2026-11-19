#pragma once

#include <cstdint>
#include <optional>

namespace komapaint {

// Integer document pixels and half-open rectangles. Explicit edges avoid
// overflow from forming x + width for untrusted or out-of-document geometry.
struct TilePoint {
    std::int64_t x = 0;
    std::int64_t y = 0;
    bool operator==(const TilePoint&) const = default;
};
struct TileRect {
    std::int64_t left = 0;
    std::int64_t top = 0;
    std::int64_t right = 0;
    std::int64_t bottom = 0;
    bool operator==(const TileRect&) const = default;
};
struct TileAddress {
    TilePoint tile;
    TilePoint local;
    bool operator==(const TileAddress&) const = default;
};
// Both endpoints are inclusive tile indices, unlike pixel rectangles.
struct TileRange {
    TilePoint first;
    TilePoint last;
    bool operator==(const TileRange&) const = default;
};

// Limit a floating-point pixel edge to an integer interval without casting
// an out-of-range coordinate. The inclusive endpoints are already integral.
inline std::int64_t clampPixelEdge(double value, std::int64_t lo, std::int64_t hi) noexcept {
    if (!(value > static_cast<double>(lo))) return lo;
    if (value >= static_cast<double>(hi)) return hi;
    return static_cast<std::int64_t>(value);
}

class TileGeometry {
public:
    static constexpr int DefaultTileSize = 256;

    // Invalid dimensions or tile size produce an invalid grid, not undefined arithmetic.
    TileGeometry(std::int64_t width, std::int64_t height, int tileSize = DefaultTileSize);
    bool isValid() const { return m_valid; }
    int tileSize() const { return m_tileSize; }
    std::int64_t width() const { return m_width; }
    std::int64_t height() const { return m_height; }
    std::optional<TileAddress> address(TilePoint pixel) const;
    std::optional<TileRect> clip(TileRect rect) const;
    // Round a fractional footprint outwards, clipping in floating point
    // before any conversion to integer pixels.
    std::optional<TileRect> rasterBounds(double left, double top, double right, double bottom) const;
    std::optional<TileRange> tilesForRect(TileRect rect) const;
    // Returns the tile's document-space bounds clipped to the document.
    std::optional<TileRect> tileBounds(TilePoint tile) const;
    // Intersects a document rectangle with a tile and returns tile-local edges.
    std::optional<TileRect> localIntersection(TilePoint tile, TileRect rect) const;

private:
    std::int64_t m_width;
    std::int64_t m_height;
    int m_tileSize;
    bool m_valid;
};

} // namespace komapaint
