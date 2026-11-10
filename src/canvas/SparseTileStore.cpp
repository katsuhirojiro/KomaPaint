#include "SparseTileStore.h"
#include <functional>
#include <algorithm>
#include <new>
#include <limits>
#include <stdexcept>
#include <utility>

namespace komapaint {
namespace {
// Release an uncommitted pixel-buffer reservation on every exit path.
class PixelReservation {
public:
    PixelReservation(DocumentMemoryBudget* budget, DocumentMemoryBudget::Category category, std::size_t bytes)
        : m_budget(budget), m_category(category), m_bytes(bytes),
          m_ok(!budget || budget->reserve(category, bytes)) {}
    ~PixelReservation() { if (m_budget && m_ok && !m_committed) m_budget->release(m_category, m_bytes); }
    bool ok() const noexcept { return m_ok; }
    void commitArtwork() noexcept {
        if (m_budget && m_category == DocumentMemoryBudget::Category::RestoreStaging)
            m_budget->transferToArtwork(m_bytes);
        m_committed = true;
    }
private:
    DocumentMemoryBudget* m_budget;
    DocumentMemoryBudget::Category m_category;
    std::size_t m_bytes;
    bool m_ok, m_committed = false;
};
}
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
SparseTileStore::~SparseTileStore()
{
    if (m_documentBudget) m_documentBudget->release(DocumentMemoryBudget::Category::Artwork, allocatedPixelBytes());
}
bool SparseTileStore::setDocumentBudget(std::shared_ptr<DocumentMemoryBudget> budget) noexcept
{
    if (!m_tiles.empty() || m_beforeChange || m_batchDepth) return false;
    m_documentBudget = std::move(budget);
    return true;
}

std::vector<TilePoint> SparseTileStore::allocatedTilePoints() const
{
    std::vector<TilePoint> result;
    result.reserve(m_tiles.size());
    for (const auto& entry : m_tiles) result.push_back(entry.first);
    return result;
}

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
    const bool needsTile = it == m_tiles.end() && value != 0;
    PixelReservation reservation(m_documentBudget.get(), DocumentMemoryBudget::Category::Artwork,
                                 needsTile ? m_tileBytes : 0);
    if (!reservation.ok()) return WriteResult::DocumentMemoryLimit;
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
    if (m_beforeChange) {
        const auto capture = m_beforeChange(m_beforeContext, a->tile, !created,
            created ? std::span<const std::uint32_t>{} : std::span<const std::uint32_t>{it->second.pixels});
        if (capture != CaptureResult::Ready) {
            if (created) m_tiles.erase(it);
            return capture == CaptureResult::Limit ? WriteResult::HistoryLimit : WriteResult::HistoryAllocationFailed;
        }
    }
    const auto dirty = m_dirty.mark({point.x, point.y, point.x + 1, point.y + 1});
    if (dirty != DirtyTileTracker::Result::Marked) {
        if (created) m_tiles.erase(it);
        return (dirty == DirtyTileTracker::Result::LimitExceeded || dirty == DirtyTileTracker::Result::VersionExhausted)
            ? WriteResult::TrackingLimit : WriteResult::AllocationFailed;
    }
    if (old == 0) ++it->second.nonzero;
    if (value == 0) --it->second.nonzero;
    old = value;
    if (it->second.nonzero == 0) {
        m_tiles.erase(it);
        if (m_documentBudget) m_documentBudget->release(DocumentMemoryBudget::Category::Artwork, m_tileBytes);
    }
    reservation.commitArtwork();
    if (m_batchDepth) {
        if (!m_batchChanged) { m_batchFirst = m_batchLast = a->tile; m_batchChanged = true; }
        else {
            m_batchFirst.x = std::min(m_batchFirst.x, a->tile.x); m_batchFirst.y = std::min(m_batchFirst.y, a->tile.y);
            m_batchLast.x = std::max(m_batchLast.x, a->tile.x); m_batchLast.y = std::max(m_batchLast.y, a->tile.y);
        }
    } else if (m_callback) m_callback(m_callbackContext, a->tile);
    return WriteResult::Changed;
}
SparseTileStore::WriteResult SparseTileStore::restoreTiles(const std::vector<TileSnapshot>& snapshots)
{
    if (!isValid() || m_beforeChange || m_batchDepth) return WriteResult::InvalidPixel;
    std::size_t stagedBytes = 0;
    for (const auto& snapshot : snapshots) {
        if (!snapshot.pixels.empty()) {
            if (snapshot.pixels.size() != m_tileBytes / sizeof(std::uint32_t)) return WriteResult::InvalidPixel;
            if (stagedBytes > std::numeric_limits<std::size_t>::max() - m_tileBytes) return WriteResult::AllocationFailed;
            stagedBytes += m_tileBytes;
        }
    }
    PixelReservation reservation(m_documentBudget.get(), DocumentMemoryBudget::Category::RestoreStaging, stagedBytes);
    if (!reservation.ok()) return WriteResult::DocumentMemoryLimit;
    try {
        // All allocations and dirty tracking happen before changing artwork.
        auto dirty = m_dirty;
        std::unordered_map<TilePoint, Tile, Hash> staged;
        staged.reserve(snapshots.size());
        std::size_t count = m_tiles.size();
        std::size_t replacedBytes = 0;
        for (const auto& snapshot : snapshots) {
            auto bounds = m_geometry.tileBounds(snapshot.tile);
            if (!bounds || staged.find(snapshot.tile) != staged.end()) return WriteResult::InvalidPixel;
            Tile tile;
            if (!snapshot.pixels.empty()) {
                if (snapshot.pixels.size() != m_tileBytes / sizeof(std::uint32_t)) return WriteResult::InvalidPixel;
                tile.pixels = snapshot.pixels;
                const auto edgeWidth = static_cast<std::size_t>(bounds->right - bounds->left);
                const auto edgeHeight = static_cast<std::size_t>(bounds->bottom - bounds->top);
                std::size_t index = 0;
                for (auto value : tile.pixels) {
                    if (value && (index % static_cast<std::size_t>(m_geometry.tileSize()) >= edgeWidth || index / static_cast<std::size_t>(m_geometry.tileSize()) >= edgeHeight)) return WriteResult::InvalidPixel;
                    ++index;
                    auto alpha = value >> 24;
                    if (((value >> 16) & 255U) > alpha || ((value >> 8) & 255U) > alpha || (value & 255U) > alpha) return WriteResult::InvalidPixel;
                    if (value) ++tile.nonzero;
                }
                if (!tile.nonzero) return WriteResult::InvalidPixel;
            }
            const bool exists = m_tiles.find(snapshot.tile) != m_tiles.end();
            if (exists) replacedBytes += m_tileBytes;
            if (exists && tile.pixels.empty()) --count;
            if (!exists && !tile.pixels.empty()) ++count;
            if (dirty.mark(*bounds) != DirtyTileTracker::Result::Marked) return WriteResult::TrackingLimit;
            staged.emplace(snapshot.tile, std::move(tile));
        }
        if (count > m_pixelBudget / m_tileBytes) return WriteResult::MemoryLimit;
        // Reserve for the maximum number of insertions regardless of removals.
        m_tiles.reserve(m_tiles.size() + staged.size());
        m_dirty = std::move(dirty);
        for (const auto& snapshot : snapshots) {
            auto incoming = staged.extract(snapshot.tile);
            auto current = m_tiles.find(snapshot.tile);
            if (incoming.mapped().pixels.empty()) {
                if (current != m_tiles.end()) m_tiles.erase(current);
            } else if (current != m_tiles.end()) {
                current->second = std::move(incoming.mapped());
            } else {
                m_tiles.insert(std::move(incoming));
            }
        }
        if (m_documentBudget) m_documentBudget->release(DocumentMemoryBudget::Category::Artwork, replacedBytes);
        reservation.commitArtwork();
        if (m_callback) for (const auto& snapshot : snapshots) m_callback(m_callbackContext, snapshot.tile);
        return WriteResult::Changed;
    } catch (const std::bad_alloc&) { return WriteResult::AllocationFailed; }
      catch (const std::length_error&) { return WriteResult::AllocationFailed; }
}
void SparseTileStore::beginBatch() noexcept { ++m_batchDepth; }
void SparseTileStore::endBatch() noexcept
{
    if (!m_batchDepth || --m_batchDepth || !m_batchChanged) return;
    const auto first = m_batchFirst, last = m_batchLast;
    m_batchChanged = false;
    if (!m_callback) return;
    for (auto y = first.y;; ++y) {
        for (auto x = first.x;; ++x) {
            m_callback(m_callbackContext, {x, y});
            if (x == last.x) break;
        }
        if (y == last.y) break;
    }
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
