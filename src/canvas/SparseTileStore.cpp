#include "SparseTileStore.h"
#include <functional>
#include <new>
#include <stdexcept>
#include <utility>

namespace komapaint {
std::size_t SparseTileStore::Hash::operator()(const TilePoint& p) const noexcept
{
    auto a = std::hash<std::int64_t>{}(p.x);
    auto b = std::hash<std::int64_t>{}(p.y);
    return a ^ (b + std::size_t{0x9e3779b9U} + (a << 6) + (a >> 2));
}
SparseTileStore::SparseTileStore(std::int64_t w, std::int64_t h, int size, std::size_t budget)
    : m_geometry(w, h, size), m_dirty(m_geometry), m_tileSizeValid(size > 0 && size <= 4096),
      m_tileBytes(m_tileSizeValid ? static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * sizeof(std::uint32_t) : 0),
      m_pixelBudget(budget) {}

std::span<const std::uint32_t> SparseTileStore::tilePixels(TilePoint tile) const noexcept
{
    const auto it = m_tiles.find(tile);
    if (it == m_tiles.end()) return {};
    return it->second.pixels;
}

std::optional<std::uint32_t> SparseTileStore::pixel(TilePoint point) const
{
    if (!isValid()) return std::nullopt;
    auto a = m_geometry.address(point);
    if (!a) return std::nullopt;
    auto it = m_tiles.find(a->tile);
    if (it == m_tiles.end()) return std::uint32_t{0};
    auto index = static_cast<std::size_t>(a->local.y) * static_cast<std::size_t>(m_geometry.tileSize()) + static_cast<std::size_t>(a->local.x);
    return it->second.pixels[index];
}

SparseTileStore::WriteResult SparseTileStore::setPixel(TilePoint point, std::uint32_t value)
{
    if (!isValid()) return WriteResult::OutOfBounds;
    auto a = m_geometry.address(point);
    if (!a) return WriteResult::OutOfBounds;
    auto alpha = (value >> 24) & 255U;
    if (((value >> 16) & 255U) > alpha || ((value >> 8) & 255U) > alpha || (value & 255U) > alpha)
        return WriteResult::InvalidPixel;
    auto it = m_tiles.find(a->tile);
    bool created = false;
    if (it == m_tiles.end()) {
        if (value == 0) return WriteResult::Unchanged;
        if (m_tileBytes > m_pixelBudget || allocatedPixelBytes() > m_pixelBudget - m_tileBytes)
            return WriteResult::MemoryLimit;
        try {
            Tile tile;
            tile.pixels.resize(m_tileBytes / sizeof(std::uint32_t), 0);
            it = m_tiles.emplace(a->tile, std::move(tile)).first;
            created = true;
        } catch (const std::bad_alloc&) { return WriteResult::AllocationFailed; }
          catch (const std::length_error&) { return WriteResult::AllocationFailed; }
    }
    auto index = static_cast<std::size_t>(a->local.y) * static_cast<std::size_t>(m_geometry.tileSize()) + static_cast<std::size_t>(a->local.x);
    auto& old = it->second.pixels[index];
    if (old == value) return WriteResult::Unchanged;
    const auto dirty = m_dirty.mark({point.x, point.y, point.x + 1, point.y + 1});
    if (dirty != DirtyTileTracker::Result::Marked) {
        if (created) m_tiles.erase(it);
        return (dirty == DirtyTileTracker::Result::LimitExceeded || dirty == DirtyTileTracker::Result::VersionExhausted)
            ? WriteResult::TrackingLimit : WriteResult::AllocationFailed;
    }
    if (old == 0) ++it->second.nonzero;
    if (value == 0) --it->second.nonzero;
    old = value;
    if (it->second.nonzero == 0) m_tiles.erase(it);
    return WriteResult::Changed;
}
bool SparseTileStore::resizeEmpty(std::int64_t width, std::int64_t height)
{
    TileGeometry replacement(width, height, m_geometry.tileSize());
    if (!m_tiles.empty() || !m_dirty.regions().empty() || !replacement.isValid() || !m_tileSizeValid) return false;
    if (!m_dirty.resetGeometry(replacement)) return false;
    m_geometry = replacement;
    return true;
}
} // namespace komapaint
