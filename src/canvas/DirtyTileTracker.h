#pragma once
#include "TileGeometry.h"
#include <cstddef>
#include <map>

namespace komapaint {

struct TilePointLess {
    bool operator()(TilePoint a, TilePoint b) const noexcept {
        return a.y < b.y || (a.y == b.y && a.x < b.x);
    }
};

// Single-threaded pending invalidations, independent of pixels and rendering.
// Rectangles are half-open and stored in tile-local coordinates. Disjoint
// changes are conservatively combined into a bounding rectangle per tile.
class DirtyTileTracker {
public:
    using Regions = std::map<TilePoint, TileRect, TilePointLess>;
    enum class Result { Marked, NoIntersection, LimitExceeded, AllocationFailed };
    explicit DirtyTileTracker(TileGeometry geometry, std::size_t maxTiles = 65536);
    // Strong guarantee: failures leave the pending regions unchanged.
    Result mark(TileRect documentRect);
    const Regions& regions() const { return m_regions; }
    void clear() noexcept { m_regions.clear(); }
private:
    static void merge(Regions& regions, TilePoint tile, TileRect rect);
    TileGeometry m_geometry;
    std::size_t m_maxTiles;
    Regions m_regions;
};
} // namespace komapaint
