#include "DirtyTileTracker.h"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace komapaint {
DirtyTileTracker::DirtyTileTracker(TileGeometry geometry, std::size_t maxTiles)
    : m_geometry(geometry), m_maxTiles(maxTiles) {}

void DirtyTileTracker::merge(Regions& regions, TilePoint tile, TileRect rect)
{
    auto [it, inserted] = regions.try_emplace(tile, rect);
    if (!inserted) {
        auto& r = it->second;
        r = {std::min(r.left, rect.left), std::min(r.top, rect.top),
             std::max(r.right, rect.right), std::max(r.bottom, rect.bottom)};
    }
}

DirtyTileTracker::Result DirtyTileTracker::mark(TileRect rect)
{
    const auto range = m_geometry.tilesForRect(rect);
    if (!range) return Result::NoIntersection;
    const auto first = range->first;
    const auto last = range->last;
    // Check counts before iterating, including huge user-supplied rectangles.
    const auto columns = static_cast<std::uint64_t>(last.x - first.x) + 1;
    const auto rows = static_cast<std::uint64_t>(last.y - first.y) + 1;
    if (columns > m_maxTiles || rows > m_maxTiles || columns > m_maxTiles / rows)
        return Result::LimitExceeded;
    try {
        // Common pixel-write path: no copy of the entire pending set.
        if (first == last) {
            if (m_regions.find(first) == m_regions.end() && m_regions.size() >= m_maxTiles)
                return Result::LimitExceeded;
            merge(m_regions, first, *m_geometry.localIntersection(first, rect));
            return Result::Marked;
        }
        // Work on a copy so a failure halfway across tiles cannot lose changes.
        auto pending = m_regions;
        for (auto y = first.y;; ++y) {
            for (auto x = first.x;; ++x) {
                TilePoint tile{x, y};
                if (pending.find(tile) == pending.end() && pending.size() >= m_maxTiles)
                    return Result::LimitExceeded;
                merge(pending, tile, *m_geometry.localIntersection(tile, rect));
                if (x == last.x) break;
            }
            if (y == last.y) break;
        }
        m_regions.swap(pending);
        return Result::Marked;
    } catch (const std::bad_alloc&) { return Result::AllocationFailed; }
      catch (const std::length_error&) { return Result::AllocationFailed; }
}
} // namespace komapaint
