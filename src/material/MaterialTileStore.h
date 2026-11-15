#pragma once

#include "MaterialEngine.h"
#include "../canvas/DirtyTileTracker.h"
#include "../canvas/DocumentMemoryBudget.h"
#include "../canvas/TileGeometry.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace komapaint::material {

// Sparse per-pixel quantities refer to an immutable shared pigment composition.
// The composition ID is local to a MaterialTileStore and remains stable for the
// lifetime of that store. Zero is reserved for an empty cell.
struct MaterialCell {
    std::size_t compositionId = 0;
    float pigmentMass = 0.0F;
    float water = 0.0F;
    float binder = 0.0F;
    float filler = 0.0F;
    float wetness = 0.0F;
    float consistency = 0.0F;
    bool empty() const noexcept;
    bool operator==(const MaterialCell&) const = default;
};

struct MaterialEntry {
    std::size_t localIndex = 0;
    MaterialCell cell{};
    bool operator==(const MaterialEntry&) const = default;
};

struct MaterialTileSnapshot {
    TilePoint tile{};
    // Sorted by localIndex; empty means the tile had no material allocation.
    std::vector<MaterialEntry> entries;
    bool operator==(const MaterialTileSnapshot&) const = default;
};

struct MaterialComposition {
    std::string mediumId;
    // Pigment masses are normalized to sum to one. Residual RGB components
    // remain explicitly unidentified and are never converted to named pigment.
    std::vector<PigmentComponent> pigments;
};

class MaterialTileStore {
public:
    enum class Result { Changed, Unchanged, OutOfBounds, InvalidData,
                        DocumentMemoryLimit, TrackingLimit, AllocationFailed };
    using BeforeChange = bool (*)(void*, TilePoint, std::span<const MaterialEntry>) noexcept;
    using ChangeCallback = void (*)(void*, TilePoint) noexcept;

    explicit MaterialTileStore(std::int64_t width, std::int64_t height,
                               int tileSize = TileGeometry::DefaultTileSize,
                               std::shared_ptr<DocumentMemoryBudget> budget = {});
    ~MaterialTileStore();
    MaterialTileStore(const MaterialTileStore&) = delete;
    MaterialTileStore& operator=(const MaterialTileStore&) = delete;

    const TileGeometry& geometry() const noexcept { return m_geometry; }
    bool isValid() const noexcept { return m_geometry.isValid(); }
    const DirtyTileTracker::Regions& dirtyRegions() const noexcept { return m_dirty.regions(); }
    std::uint64_t dirtyVersion() const noexcept { return m_dirty.version(); }
    bool acknowledgeDirty(TilePoint tile, std::uint64_t version) noexcept { return m_dirty.acknowledge(tile, version); }
    std::size_t allocatedTiles() const noexcept { return m_tiles.size(); }
    std::vector<TilePoint> allocatedTilePoints() const;
    std::size_t allocatedBytes() const noexcept { return m_allocatedBytes + m_compositionBytes; }
    std::shared_ptr<DocumentMemoryBudget> documentBudget() const noexcept { return m_budget; }
    bool setDocumentBudget(std::shared_ptr<DocumentMemoryBudget> budget) noexcept;
    bool resizeEmpty(std::int64_t width, std::int64_t height) noexcept;

    std::optional<MaterialCell> cellAt(TilePoint point) const noexcept;
    std::span<const MaterialEntry> tileEntries(TilePoint tile) const noexcept;
    std::optional<PaintBatch> batchAt(TilePoint point) const;
    const MaterialComposition* composition(std::size_t id) const noexcept;
    const std::vector<MaterialComposition>& compositions() const noexcept { return m_compositions; }

    Result deposit(TilePoint point, const PaintBatch& paint);
    Result setBatch(TilePoint point, const PaintBatch& paint);
    Result scaleCell(TilePoint point, double factor);
    Result erase(TilePoint point);
    MaterialTileSnapshot snapshot(TilePoint tile) const;
    Result restoreTiles(std::span<const MaterialTileSnapshot> snapshots, bool notify = true);
    // Import only into an empty store before material tile data. Composition
    // IDs in the file are retained exactly; malformed or duplicate data fails.
    bool importCompositions(const std::vector<MaterialComposition>& compositions);

    void setBeforeChange(BeforeChange callback, void* context) noexcept { m_beforeChange = callback; m_beforeContext = context; }
    void setChangeCallback(ChangeCallback callback, void* context) noexcept { m_callback = callback; m_callbackContext = context; }
    bool notifyingChange() const noexcept { return m_notifying; }
    void beginBatch() noexcept { ++m_batchDepth; }
    void endBatch() noexcept;

private:
    struct Hash { std::size_t operator()(TilePoint p) const noexcept; };
    struct Tile { std::vector<MaterialEntry> entries; std::size_t chargedBytes = 0; };
    std::optional<std::size_t> intern(const PaintBatch& paint, bool* inserted);
    Result write(TilePoint point, const PaintBatch& paint, bool combine);
    Result writeCell(TilePoint point, const MaterialCell& cell);
    std::optional<PaintBatch> expand(const MaterialCell& cell) const;
    std::size_t compositionHash(const MaterialComposition& composition) const noexcept;
    bool sameComposition(const MaterialComposition& a, const MaterialComposition& b) const noexcept;
    void eraseUnreferencedComposition(std::size_t id) noexcept;
    bool validateCell(const MaterialCell& cell) const noexcept;
    void notify(TilePoint tile) noexcept;
    void releaseAll() noexcept;

    TileGeometry m_geometry;
    DirtyTileTracker m_dirty;
    std::shared_ptr<DocumentMemoryBudget> m_budget;
    std::unordered_map<TilePoint, Tile, Hash> m_tiles;
    std::vector<MaterialComposition> m_compositions; // index zero is the empty sentinel
    std::vector<std::size_t> m_compositionHashes;
    std::unordered_multimap<std::size_t, std::size_t> m_compositionIndex;
    std::size_t m_allocatedBytes = 0;
    std::size_t m_compositionBytes = 0;
    unsigned m_batchDepth = 0;
    std::unordered_set<TilePoint, Hash> m_batchTiles;
    bool m_notifying = false;
    BeforeChange m_beforeChange = nullptr;
    void* m_beforeContext = nullptr;
    ChangeCallback m_callback = nullptr;
    void* m_callbackContext = nullptr;
};

// Material pixels are rendered on demand into disposable output. The source
// batch and cell data are never altered by rendering.
class MaterialRenderer {
public:
    explicit MaterialRenderer(const MaterialEngine& engine) : m_engine(engine) {}
    bool renderTile(const MaterialTileStore& material, TilePoint tile,
                    std::span<const std::uint32_t> digitalBase,
                    const MediumProfile& defaultMedium, const SurfaceProfile& surface,
                    std::vector<std::uint32_t>* output) const;
private:
    const MaterialEngine& m_engine;
};

} // namespace komapaint::material
