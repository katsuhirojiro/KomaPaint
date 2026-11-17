#pragma once

#include "../canvas/DirtyTileTracker.h"
#include "../canvas/TileGeometry.h"

#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace komapaint::region {

enum class SelectionOp {
    Replace,
    Add,
    Subtract,
    Intersect
};

enum class SelectionShape {
    Rectangle,
    Ellipse
};

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct PixelBounds {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t width = 0;
    std::int64_t height = 0;
    bool empty() const noexcept { return width <= 0 || height <= 0; }
};

// Sparse 8-bit coverage mask (0..255 per pixel) backed by 64x64 tiles.
// Supports arbitrary canvas dimensions up to 32768x32768 without full-canvas
// allocations, including O(allocated tiles) inversion via `m_defaultCoverage`.
class SelectionMask {
public:
    static constexpr std::int64_t kTileSize = 64;

    SelectionMask() = default;
    SelectionMask(std::int64_t docWidth, std::int64_t docHeight);

    void reset(std::int64_t docWidth, std::int64_t docHeight);

    std::int64_t width() const noexcept { return m_width; }
    std::int64_t height() const noexcept { return m_height; }
    std::uint64_t revision() const noexcept { return m_revision; }

    bool empty() const noexcept;
    bool hasSelection() const noexcept { return !empty(); }
    std::uint8_t defaultCoverage() const noexcept { return m_defaultCoverage; }

    // Tight bounding box of selected pixels (or full doc when inverted with default > 0).
    PixelBounds bounds() const noexcept;

    std::uint8_t at(std::int64_t x, std::int64_t y) const noexcept;
    void setPixel(std::int64_t x, std::int64_t y, std::uint8_t value);

    void clear();
    void selectAll();
    void invert();

    void applyRectMask(std::int64_t ox, std::int64_t oy,
                       std::int64_t w, std::int64_t h,
                       std::span<const std::uint8_t> coverage,
                       SelectionOp op);

    void applyShape(SelectionShape shape,
                    double x0, double y0, double x1, double y1,
                    bool antialias, int featherPx, SelectionOp op);

    void applyPolygon(std::span<const Vec2> points,
                      bool antialias, int featherPx, SelectionOp op);

    void expand(int radiusPx);
    void contract(int radiusPx);
    void feather(int radiusPx);

    // Snapshot / restore for undoable selection operations.
    struct Snapshot {
        std::int64_t width = 0;
        std::int64_t height = 0;
        std::uint8_t defaultCoverage = 0;
        std::map<TilePoint, std::vector<std::uint8_t>, TilePointLess> tiles;
    };
    Snapshot snapshot() const;
    void restore(const Snapshot& snap);

    std::size_t allocatedTiles() const noexcept { return m_tiles.size(); }

    // Fills a caller-provided 64x64 tile buffer with selection coverage (returns false if uniform).
    bool readTile(TilePoint tile, std::uint8_t* out4096) const noexcept;

private:
    static constexpr std::size_t kTilePixels = static_cast<std::size_t>(kTileSize * kTileSize);

    TilePoint tileFor(std::int64_t x, std::int64_t y) const noexcept;
    std::vector<std::uint8_t>& ensureTile(TilePoint tile);
    void pruneUniformTiles();
    void morphRadius(int radiusPx, bool dilate);

    std::int64_t m_width = 0;
    std::int64_t m_height = 0;
    std::uint8_t m_defaultCoverage = 0;
    std::uint64_t m_revision = 1;
    std::map<TilePoint, std::vector<std::uint8_t>, TilePointLess> m_tiles;
    mutable std::uint64_t m_cachedRevision = 0;
    mutable TilePoint m_cachedTile{};
    mutable const std::uint8_t* m_cachedTilePtr = nullptr;
};

} // namespace komapaint::region
