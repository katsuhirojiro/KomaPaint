#include "TileGeometry.h"
#include <algorithm>

namespace komapaint {

TileGeometry::TileGeometry(std::int64_t width, std::int64_t height, int tileSize)
    : m_width(width), m_height(height), m_tileSize(tileSize),
      m_valid(width > 0 && height > 0 && tileSize > 0) {}

std::optional<TileAddress> TileGeometry::address(TilePoint pixel) const
{
    if (!m_valid || pixel.x < 0 || pixel.y < 0 || pixel.x >= m_width || pixel.y >= m_height)
        return std::nullopt;
    return TileAddress{{pixel.x / m_tileSize, pixel.y / m_tileSize},
                       {pixel.x % m_tileSize, pixel.y % m_tileSize}};
}

std::optional<TileRect> TileGeometry::clip(TileRect rect) const
{
    if (!m_valid || rect.left >= rect.right || rect.top >= rect.bottom) return std::nullopt;
    TileRect clipped{std::max<std::int64_t>(0, rect.left), std::max<std::int64_t>(0, rect.top),
                     std::min(m_width, rect.right), std::min(m_height, rect.bottom)};
    if (clipped.left >= clipped.right || clipped.top >= clipped.bottom) return std::nullopt;
    return clipped;
}

std::optional<TileRange> TileGeometry::tilesForRect(TileRect rect) const
{
    auto c = clip(rect);
    if (!c) return std::nullopt;
    return TileRange{{c->left / m_tileSize, c->top / m_tileSize},
                     {(c->right - 1) / m_tileSize, (c->bottom - 1) / m_tileSize}};
}

std::optional<TileRect> TileGeometry::tileBounds(TilePoint tile) const
{
    if (!m_valid || tile.x < 0 || tile.y < 0 ||
        tile.x > (m_width - 1) / m_tileSize || tile.y > (m_height - 1) / m_tileSize)
        return std::nullopt;
    const auto x = tile.x * m_tileSize;
    const auto y = tile.y * m_tileSize;
    // Addition is bounded by the remaining document extent, including at INT64_MAX.
    return TileRect{x, y, x + std::min<std::int64_t>(m_tileSize, m_width - x),
                    y + std::min<std::int64_t>(m_tileSize, m_height - y)};
}

std::optional<TileRect> TileGeometry::localIntersection(TilePoint tile, TileRect rect) const
{
    const auto b = tileBounds(tile);
    const auto c = clip(rect);
    if (!b || !c) return std::nullopt;
    const auto left = std::max(b->left, c->left);
    const auto top = std::max(b->top, c->top);
    const auto right = std::min(b->right, c->right);
    const auto bottom = std::min(b->bottom, c->bottom);
    if (left >= right || top >= bottom) return std::nullopt;
    return TileRect{left - b->left, top - b->top, right - b->left, bottom - b->top};
}

} // namespace komapaint
