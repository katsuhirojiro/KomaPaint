#pragma once
#include "SparseTileStore.h"
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace komapaint {
// UI-independent view, ordered TOP-FIRST (same as the layer panel).
// Stores must remain alive and unmodified during a synchronous compose call.
struct CompositeLayer {
    SparseTileStore* store = nullptr;
    bool visible = true;
    double opacity = 1.0;
    bool operator==(const CompositeLayer&) const = default;
};
struct CompositeTile {
    std::int64_t width = 0, height = 0;
    std::vector<std::uint32_t> pixels; // tightly packed premultiplied ARGB
};
class CpuTileCompositor {
public:
    // Empty optional means invalid inputs or allocation failure. Never alters sources.
    static std::optional<CompositeTile> compose(const TileGeometry& geometry,
        TilePoint tile, const std::vector<CompositeLayer>& topFirst, std::uint32_t background);
};

// Single consumer / single thread. Owns no artwork or cache. Configuration
// change means the consumer MUST discard the entire cache, then call accept.
// Stores must stay alive throughout a plan; never use it after document replacement.
// Reset on document replacement, even if an allocator reuses a store address.
class CompositeInvalidation {
public:
    struct LayerTicket { SparseTileStore* store; std::uint64_t version; };
    struct Plan {
        bool invalidateAll = true;
        std::set<TilePoint, TilePointLess> tiles;
        std::vector<LayerTicket> tickets;
        std::vector<CompositeLayer> layers;
        std::uint32_t background = 0;
    };
    // On allocation failure an exception propagates; source dirty state is untouched.
    void reset() noexcept { m_hasConfiguration = false; m_layers.clear(); }
    Plan prepare(const std::vector<CompositeLayer>& layers, std::uint32_t background) const;
    // Call only after the cache was discarded when invalidateAll is true.
    bool accept(const Plan& plan, const std::vector<CompositeLayer>& current, std::uint32_t background);
    // Call only after successful composition AND publication of this dirty tile.
    // On false (stale configuration or pixel version), discard the output;
    // dirty data is untouched. Otherwise the ticketed tile is acknowledged.
    bool acknowledge(const Plan& plan, TilePoint tile, const std::vector<CompositeLayer>& current, std::uint32_t background);
private:
    bool m_hasConfiguration = false;
    std::vector<CompositeLayer> m_layers;
    std::uint32_t m_background = 0;
};
} // namespace komapaint
