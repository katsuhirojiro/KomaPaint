#pragma once
#include "TileGeometry.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace komapaint {

// CPU-owned premultiplied ARGB pixels. Missing tiles are transparent.
// This low-level store is not an editing transaction or undo mechanism.
class SparseTileStore {
public:
    enum class WriteResult { Changed, Unchanged, OutOfBounds, InvalidPixel, MemoryLimit, AllocationFailed };
    static constexpr std::size_t DefaultPixelBudget = 256U * 1024U * 1024U;
    SparseTileStore(std::int64_t width, std::int64_t height,
                    int tileSize = TileGeometry::DefaultTileSize,
                    std::size_t pixelBudget = DefaultPixelBudget);
    bool isValid() const { return m_geometry.isValid() && m_tileSizeValid; }
    const TileGeometry& geometry() const { return m_geometry; }
    std::size_t allocatedTiles() const { return m_tiles.size(); }
    std::size_t allocatedPixelBytes() const { return m_tiles.size() * m_tileBytes; }
    std::optional<std::uint32_t> pixel(TilePoint point) const;
    WriteResult setPixel(TilePoint point, std::uint32_t argbPremultiplied);
    // Resizing is deliberately restricted until a real crop/resize operation exists.
    bool resizeEmpty(std::int64_t width, std::int64_t height);
private:
    struct Hash { std::size_t operator()(const TilePoint& p) const noexcept; };
    struct Tile { std::vector<std::uint32_t> pixels; std::size_t nonzero = 0; };
    TileGeometry m_geometry;
    bool m_tileSizeValid;
    std::size_t m_tileBytes;
    std::size_t m_pixelBudget;
    std::unordered_map<TilePoint, Tile, Hash> m_tiles;
};
} // namespace komapaint
