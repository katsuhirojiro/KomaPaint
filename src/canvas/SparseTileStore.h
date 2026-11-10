#pragma once
#include "TileGeometry.h"
#include "DirtyTileTracker.h"
#include "DocumentMemoryBudget.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace komapaint {

// CPU-owned premultiplied ARGB pixels. Missing tiles are transparent.
// This low-level store is not an editing transaction or undo mechanism.
class SparseTileStore {
public:
    enum class WriteResult { Changed, Unchanged, OutOfBounds, InvalidPixel, MemoryLimit, DocumentMemoryLimit, TrackingLimit, HistoryLimit, HistoryAllocationFailed, AllocationFailed };
    static constexpr std::size_t DefaultPixelBudget = 256U * 1024U * 1024U;
    SparseTileStore(std::int64_t width, std::int64_t height,
                    int tileSize = TileGeometry::DefaultTileSize,
                    std::size_t pixelBudget = DefaultPixelBudget);
    ~SparseTileStore();
    SparseTileStore(const SparseTileStore&) = delete;
    SparseTileStore& operator=(const SparseTileStore&) = delete;
    struct TileSnapshot { TilePoint tile; std::vector<std::uint32_t> pixels; }; // empty pixels = absent
    enum class CaptureResult { Ready, Limit, AllocationFailed };
    using BeforeChange = CaptureResult (*)(void*, TilePoint, bool, std::span<const std::uint32_t>) noexcept;
    void setBeforeChange(BeforeChange callback, void* context) noexcept { m_beforeChange = callback; m_beforeContext = context; }
    // All snapshots are applied atomically, or no pixels change.
    WriteResult restoreTiles(const std::vector<TileSnapshot>& snapshots);
    std::size_t tileBytes() const { return m_tileBytes; }
    std::size_t pixelBudget() const { return m_pixelBudget; }
    // Attach only before artwork or a stroke exists. Standalone stores may
    // continue without a document budget.
    bool setDocumentBudget(std::shared_ptr<DocumentMemoryBudget> budget) noexcept;
    std::shared_ptr<DocumentMemoryBudget> documentBudget() const noexcept { return m_documentBudget; }
    using ChangeCallback = void (*)(void*, TilePoint) noexcept;
    void setChangeCallback(ChangeCallback callback, void* context) noexcept { m_callback = callback; m_callbackContext = context; }
    bool isValid() const { return m_geometry.isValid() && m_tileSizeValid; }
    const TileGeometry& geometry() const { return m_geometry; }
    const DirtyTileTracker::Regions& dirtyRegions() const { return m_dirty.regions(); }
    std::uint64_t dirtyVersion() const { return m_dirty.version(); }
    bool acknowledgeDirty(TilePoint tile, std::uint64_t version) noexcept { return m_dirty.acknowledge(tile, version); }
    // View remains valid only until the next store mutation. Empty means absent.
    std::span<const std::uint32_t> tilePixels(TilePoint tile) const noexcept;
    std::size_t allocatedTiles() const { return m_tiles.size(); }
    std::size_t allocatedPixelBytes() const { return m_tiles.size() * m_tileBytes; }
    // All non-empty tiles ever allocated have nonzero content; returns their
    // points so callers (serialization) can enumerate artwork without probing.
    std::vector<TilePoint> allocatedTilePoints() const;
    std::optional<std::uint32_t> pixel(TilePoint point) const;
    // Coalesce change notifications for one bounded dab; pixel storage and dirty
    // tracking still update immediately. Must be paired on the owning thread.
    void beginBatch() noexcept;
    void endBatch() noexcept;
    WriteResult setPixel(TilePoint point, std::uint32_t argbPremultiplied);
    // Resizing is deliberately restricted until a real crop/resize operation exists.
    bool resizeEmpty(std::int64_t width, std::int64_t height);
private:
    struct Hash { std::size_t operator()(const TilePoint& p) const noexcept; };
    struct Tile { std::vector<std::uint32_t> pixels; std::size_t nonzero = 0; };
    unsigned m_batchDepth = 0;
    bool m_batchChanged = false;
    TilePoint m_batchFirst{}, m_batchLast{};
    BeforeChange m_beforeChange = nullptr;
    void* m_beforeContext = nullptr;
    ChangeCallback m_callback = nullptr;
    void* m_callbackContext = nullptr;
    TileGeometry m_geometry;
    DirtyTileTracker m_dirty;
    bool m_tileSizeValid;
    std::size_t m_tileBytes;
    std::size_t m_pixelBudget;
    std::shared_ptr<DocumentMemoryBudget> m_documentBudget;
    std::unordered_map<TilePoint, Tile, Hash> m_tiles;
};
} // namespace komapaint
