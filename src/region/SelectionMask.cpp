#include "SelectionMask.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace komapaint::region {
namespace {

std::uint8_t combineOp(std::uint8_t existing, std::uint8_t incoming, SelectionOp op) noexcept
{
    switch (op) {
    case SelectionOp::Replace:
        return incoming;
    case SelectionOp::Add:
        return std::max(existing, incoming);
    case SelectionOp::Subtract: {
        const int v = static_cast<int>(existing) - static_cast<int>(incoming);
        return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
    }
    case SelectionOp::Intersect:
        return static_cast<std::uint8_t>((static_cast<int>(existing) * static_cast<int>(incoming) + 127) / 255);
    }
    return incoming;
}

void boxBlurHorizontal(const std::uint8_t* src, std::uint8_t* dst,
                       std::int64_t w, std::int64_t h, int radius)
{
    if (radius <= 0) {
        std::copy_n(src, static_cast<std::size_t>(w * h), dst);
        return;
    }
    const int win = 2 * radius + 1;
    for (std::int64_t y = 0; y < h; ++y) {
        const std::uint8_t* row = src + y * w;
        std::uint8_t* outRow = dst + y * w;
        int sum = 0;
        for (int k = -radius; k <= radius; ++k) {
            const std::int64_t sx = std::clamp<std::int64_t>(k, 0, w - 1);
            sum += row[sx];
        }
        for (std::int64_t x = 0; x < w; ++x) {
            outRow[x] = static_cast<std::uint8_t>(std::clamp((sum + win / 2) / win, 0, 255));
            const std::int64_t remX = std::clamp<std::int64_t>(x - radius, 0, w - 1);
            const std::int64_t addX = std::clamp<std::int64_t>(x + radius + 1, 0, w - 1);
            sum += static_cast<int>(row[addX]) - static_cast<int>(row[remX]);
        }
    }
}

void boxBlurVertical(const std::uint8_t* src, std::uint8_t* dst,
                     std::int64_t w, std::int64_t h, int radius)
{
    if (radius <= 0) {
        std::copy_n(src, static_cast<std::size_t>(w * h), dst);
        return;
    }
    const int win = 2 * radius + 1;
    for (std::int64_t x = 0; x < w; ++x) {
        int sum = 0;
        for (int k = -radius; k <= radius; ++k) {
            const std::int64_t sy = std::clamp<std::int64_t>(k, 0, h - 1);
            sum += src[sy * w + x];
        }
        for (std::int64_t y = 0; y < h; ++y) {
            dst[y * w + x] = static_cast<std::uint8_t>(std::clamp((sum + win / 2) / win, 0, 255));
            const std::int64_t remY = std::clamp<std::int64_t>(y - radius, 0, h - 1);
            const std::int64_t addY = std::clamp<std::int64_t>(y + radius + 1, 0, h - 1);
            sum += static_cast<int>(src[addY * w + x]) - static_cast<int>(src[remY * w + x]);
        }
    }
}

void featherBuffer(std::vector<std::uint8_t>& buf, std::int64_t w, std::int64_t h, int radiusPx)
{
    if (radiusPx <= 0 || w <= 0 || h <= 0) return;
    std::vector<std::uint8_t> tmp(static_cast<std::size_t>(w * h));
    // Two box passes approximate Gaussian feathering cleanly and fast.
    const int pass1 = std::max(1, radiusPx / 2);
    const int pass2 = std::max(1, (radiusPx + 1) / 2);
    boxBlurHorizontal(buf.data(), tmp.data(), w, h, pass1);
    boxBlurVertical(tmp.data(), buf.data(), w, h, pass1);
    boxBlurHorizontal(buf.data(), tmp.data(), w, h, pass2);
    boxBlurVertical(tmp.data(), buf.data(), w, h, pass2);
}

} // namespace

SelectionMask::SelectionMask(std::int64_t docWidth, std::int64_t docHeight)
    : m_width(std::max<std::int64_t>(0, docWidth))
    , m_height(std::max<std::int64_t>(0, docHeight))
{
}

void SelectionMask::reset(std::int64_t docWidth, std::int64_t docHeight)
{
    m_width = std::max<std::int64_t>(0, docWidth);
    m_height = std::max<std::int64_t>(0, docHeight);
    m_defaultCoverage = 0;
    m_tiles.clear();
    m_cachedRevision = 0;
    m_cachedTilePtr = nullptr;
    ++m_revision;
}

bool SelectionMask::empty() const noexcept
{
    if (m_width <= 0 || m_height <= 0) return true;
    if (m_defaultCoverage > 0) return false;
    for (const auto& [pt, data] : m_tiles) {
        (void)pt;
        for (std::uint8_t v : data) {
            if (v > 0) return false;
        }
    }
    return true;
}

PixelBounds SelectionMask::bounds() const noexcept
{
    if (m_width <= 0 || m_height <= 0) return {};
    if (m_defaultCoverage > 0) {
        return {0, 0, m_width, m_height};
    }
    std::int64_t minX = std::numeric_limits<std::int64_t>::max();
    std::int64_t minY = std::numeric_limits<std::int64_t>::max();
    std::int64_t maxX = -1;
    std::int64_t maxY = -1;
    for (const auto& [tp, data] : m_tiles) {
        const std::int64_t baseX = tp.x * kTileSize;
        const std::int64_t baseY = tp.y * kTileSize;
        for (std::int64_t ly = 0; ly < kTileSize; ++ly) {
            const std::int64_t gy = baseY + ly;
            if (gy < 0 || gy >= m_height) continue;
            for (std::int64_t lx = 0; lx < kTileSize; ++lx) {
                const std::int64_t gx = baseX + lx;
                if (gx < 0 || gx >= m_width) continue;
                if (data[static_cast<std::size_t>(ly * kTileSize + lx)] > 0) {
                    minX = std::min(minX, gx);
                    minY = std::min(minY, gy);
                    maxX = std::max(maxX, gx);
                    maxY = std::max(maxY, gy);
                }
            }
        }
    }
    if (maxX < minX || maxY < minY) return {};
    return {minX, minY, maxX - minX + 1, maxY - minY + 1};
}

TilePoint SelectionMask::tileFor(std::int64_t x, std::int64_t y) const noexcept
{
    return {x / kTileSize, y / kTileSize};
}

std::vector<std::uint8_t>& SelectionMask::ensureTile(TilePoint tile)
{
    auto [it, inserted] = m_tiles.try_emplace(tile);
    if (inserted) {
        it->second.assign(kTilePixels, m_defaultCoverage);
        m_cachedRevision = 0;
    }
    return it->second;
}

void SelectionMask::pruneUniformTiles()
{
    for (auto it = m_tiles.begin(); it != m_tiles.end();) {
        const auto& data = it->second;
        bool uniform = true;
        for (std::uint8_t v : data) {
            if (v != m_defaultCoverage) {
                uniform = false;
                break;
            }
        }
        if (uniform) {
            it = m_tiles.erase(it);
            m_cachedRevision = 0;
        } else {
            ++it;
        }
    }
}

std::uint8_t SelectionMask::at(std::int64_t x, std::int64_t y) const noexcept
{
    if (x < 0 || y < 0 || x >= m_width || y >= m_height) return 0;
    if (m_tiles.empty()) return m_defaultCoverage;
    const TilePoint tp = tileFor(x, y);
    const std::uint8_t* ptr = nullptr;
    if (m_cachedRevision == m_revision && m_cachedTile == tp) {
        ptr = m_cachedTilePtr;
    } else {
        const auto it = m_tiles.find(tp);
        ptr = (it == m_tiles.end()) ? nullptr : it->second.data();
        m_cachedTile = tp;
        m_cachedTilePtr = ptr;
        m_cachedRevision = m_revision;
    }
    if (!ptr) return m_defaultCoverage;
    const std::int64_t lx = x % kTileSize;
    const std::int64_t ly = y % kTileSize;
    return ptr[static_cast<std::size_t>(ly * kTileSize + lx)];
}

void SelectionMask::setPixel(std::int64_t x, std::int64_t y, std::uint8_t value)
{
    if (x < 0 || y < 0 || x >= m_width || y >= m_height) return;
    const TilePoint tp = tileFor(x, y);
    if (m_tiles.find(tp) == m_tiles.end() && value == m_defaultCoverage) return;
    auto& tile = ensureTile(tp);
    const std::int64_t lx = x % kTileSize;
    const std::int64_t ly = y % kTileSize;
    tile[static_cast<std::size_t>(ly * kTileSize + lx)] = value;
    ++m_revision;
}

void SelectionMask::clear()
{
    m_defaultCoverage = 0;
    m_tiles.clear();
    m_cachedRevision = 0;
    m_cachedTilePtr = nullptr;
    ++m_revision;
}

void SelectionMask::selectAll()
{
    if (m_width <= 0 || m_height <= 0) return;
    m_defaultCoverage = 255;
    m_tiles.clear();
    m_cachedRevision = 0;
    m_cachedTilePtr = nullptr;
    ++m_revision;
}

void SelectionMask::invert()
{
    if (m_width <= 0 || m_height <= 0) return;
    m_defaultCoverage = static_cast<std::uint8_t>(255 - m_defaultCoverage);
    for (auto& [tp, data] : m_tiles) {
        (void)tp;
        for (std::uint8_t& v : data) {
            v = static_cast<std::uint8_t>(255 - v);
        }
    }
    pruneUniformTiles();
    ++m_revision;
}

bool SelectionMask::readTile(TilePoint tile, std::uint8_t* out4096) const noexcept
{
    if (!out4096) return false;
    const auto it = m_tiles.find(tile);
    if (it == m_tiles.end()) {
        std::fill_n(out4096, kTilePixels, m_defaultCoverage);
        return m_defaultCoverage != 0;
    }
    std::copy_n(it->second.data(), kTilePixels, out4096);
    return true;
}

void SelectionMask::applyRectMask(std::int64_t ox, std::int64_t oy,
                                  std::int64_t w, std::int64_t h,
                                  std::span<const std::uint8_t> coverage,
                                  SelectionOp op)
{
    if (m_width <= 0 || m_height <= 0) return;
    const std::int64_t x0 = std::max<std::int64_t>(0, ox);
    const std::int64_t y0 = std::max<std::int64_t>(0, oy);
    const std::int64_t x1 = std::min<std::int64_t>(m_width, ox + w);
    const std::int64_t y1 = std::min<std::int64_t>(m_height, oy + h);

    if (op == SelectionOp::Replace) {
        m_defaultCoverage = 0;
        m_tiles.clear();
    } else if (op == SelectionOp::Intersect) {
        // Everything outside [x0, x1) x [y0, y1) becomes 0.
        for (auto it = m_tiles.begin(); it != m_tiles.end();) {
            const std::int64_t bx = it->first.x * kTileSize;
            const std::int64_t by = it->first.y * kTileSize;
            if (bx + kTileSize <= x0 || bx >= x1 || by + kTileSize <= y0 || by >= y1) {
                it = m_tiles.erase(it);
            } else {
                for (std::int64_t ly = 0; ly < kTileSize; ++ly) {
                    const std::int64_t gy = by + ly;
                    for (std::int64_t lx = 0; lx < kTileSize; ++lx) {
                        const std::int64_t gx = bx + lx;
                        if (gx < x0 || gx >= x1 || gy < y0 || gy >= y1) {
                            it->second[static_cast<std::size_t>(ly * kTileSize + lx)] = 0;
                        }
                    }
                }
                ++it;
            }
        }
        m_defaultCoverage = 0;
    }

    if (x0 >= x1 || y0 >= y1 || static_cast<std::int64_t>(coverage.size()) < w * h) {
        pruneUniformTiles();
        ++m_revision;
        return;
    }

    const std::int64_t tX0 = x0 / kTileSize;
    const std::int64_t tY0 = y0 / kTileSize;
    const std::int64_t tX1 = (x1 - 1) / kTileSize;
    const std::int64_t tY1 = (y1 - 1) / kTileSize;

    for (std::int64_t ty = tY0; ty <= tY1; ++ty) {
        for (std::int64_t tx = tX0; tx <= tX1; ++tx) {
            const TilePoint tp{tx, ty};
            const std::int64_t bx = tx * kTileSize;
            const std::int64_t by = ty * kTileSize;
            const std::int64_t ix0 = std::max(x0, bx);
            const std::int64_t iy0 = std::max(y0, by);
            const std::int64_t ix1 = std::min(x1, bx + kTileSize);
            const std::int64_t iy1 = std::min(y1, by + kTileSize);

            const bool tileExists = m_tiles.find(tp) != m_tiles.end();
            std::vector<std::uint8_t>* tilePtr = tileExists ? &m_tiles[tp] : nullptr;

            for (std::int64_t gy = iy0; gy < iy1; ++gy) {
                const std::int64_t ly = gy - by;
                const std::int64_t sy = gy - oy;
                for (std::int64_t gx = ix0; gx < ix1; ++gx) {
                    const std::int64_t lx = gx - bx;
                    const std::int64_t sx = gx - ox;
                    const std::uint8_t inc = coverage[static_cast<std::size_t>(sy * w + sx)];
                    const std::uint8_t prev = tilePtr
                        ? (*tilePtr)[static_cast<std::size_t>(ly * kTileSize + lx)]
                        : m_defaultCoverage;
                    const std::uint8_t next = combineOp(prev, inc, op);
                    if (next != prev) {
                        if (!tilePtr) tilePtr = &ensureTile(tp);
                        (*tilePtr)[static_cast<std::size_t>(ly * kTileSize + lx)] = next;
                    }
                }
            }
        }
    }

    pruneUniformTiles();
    ++m_revision;
}

void SelectionMask::applyShape(SelectionShape shape,
                               double x0, double y0, double x1, double y1,
                               bool antialias, int featherPx, SelectionOp op)
{
    if (m_width <= 0 || m_height <= 0) return;
    const double minX = std::min(x0, x1);
    const double maxX = std::max(x0, x1);
    const double minY = std::min(y0, y1);
    const double maxY = std::max(y0, y1);

    const int pad = std::max(2, featherPx * 2 + 2);
    const std::int64_t bx0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(minX)) - pad);
    const std::int64_t by0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(minY)) - pad);
    const std::int64_t bx1 = std::min<std::int64_t>(m_width, static_cast<std::int64_t>(std::ceil(maxX)) + pad);
    const std::int64_t by1 = std::min<std::int64_t>(m_height, static_cast<std::int64_t>(std::ceil(maxY)) + pad);
    const std::int64_t bw = bx1 - bx0;
    const std::int64_t bh = by1 - by0;
    if (bw <= 0 || bh <= 0 || (maxX - minX) < 0.25 || (maxY - minY) < 0.25) {
        if (op == SelectionOp::Replace) clear();
        return;
    }

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(bw * bh), 0);
    const double cx = (minX + maxX) * 0.5;
    const double cy = (minY + maxY) * 0.5;
    const double rx = std::max(0.5, (maxX - minX) * 0.5);
    const double ry = std::max(0.5, (maxY - minY) * 0.5);

    for (std::int64_t y = by0; y < by1; ++y) {
        const double py = static_cast<double>(y) + 0.5;
        for (std::int64_t x = bx0; x < bx1; ++x) {
            const double px = static_cast<double>(x) + 0.5;
            std::uint8_t cov = 0;
            if (shape == SelectionShape::Rectangle) {
                if (antialias) {
                    const double dx = std::max(minX - px, px - maxX);
                    const double dy = std::max(minY - py, py - maxY);
                    const double dist = std::max(dx, dy);
                    const double alpha = std::clamp(0.5 - dist, 0.0, 1.0);
                    cov = static_cast<std::uint8_t>(std::lround(alpha * 255.0));
                } else {
                    cov = (px >= minX && px <= maxX && py >= minY && py <= maxY) ? 255 : 0;
                }
            } else {
                const double nx = (px - cx) / rx;
                const double ny = (py - cy) / ry;
                const double r = std::hypot(nx, ny);
                if (antialias) {
                    // Approximate pixel-space signed distance for ellipse.
                    const double grad = std::hypot((px - cx) / (rx * rx), (py - cy) / (ry * ry));
                    const double sdf = (grad > 1e-6) ? ((r - 1.0) / (grad * std::max(1.0, r))) : -std::min(rx, ry);
                    const double alpha = std::clamp(0.5 - sdf, 0.0, 1.0);
                    cov = static_cast<std::uint8_t>(std::lround(alpha * 255.0));
                } else {
                    cov = (r <= 1.0) ? 255 : 0;
                }
            }
            buf[static_cast<std::size_t>((y - by0) * bw + (x - bx0))] = cov;
        }
    }

    if (featherPx > 0) featherBuffer(buf, bw, bh, featherPx);
    applyRectMask(bx0, by0, bw, bh, buf, op);
}

void SelectionMask::applyPolygon(std::span<const Vec2> points,
                                 bool antialias, int featherPx, SelectionOp op)
{
    if (m_width <= 0 || m_height <= 0) return;
    if (points.size() < 3) {
        if (op == SelectionOp::Replace) clear();
        return;
    }

    double minX = points[0].x, maxX = points[0].x;
    double minY = points[0].y, maxY = points[0].y;
    for (const auto& p : points) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    const int pad = std::max(2, featherPx * 2 + 2);
    const std::int64_t bx0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(minX)) - pad);
    const std::int64_t by0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(minY)) - pad);
    const std::int64_t bx1 = std::min<std::int64_t>(m_width, static_cast<std::int64_t>(std::ceil(maxX)) + pad);
    const std::int64_t by1 = std::min<std::int64_t>(m_height, static_cast<std::int64_t>(std::ceil(maxY)) + pad);
    const std::int64_t bw = bx1 - bx0;
    const std::int64_t bh = by1 - by0;
    if (bw <= 0 || bh <= 0) {
        if (op == SelectionOp::Replace) clear();
        return;
    }

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(bw * bh), 0);
    const int subSamples = antialias ? 4 : 1;
    const double subStep = 1.0 / static_cast<double>(subSamples);
    const double subOffset = 0.5 * subStep;
    const int weightPerHit = 255 / (subSamples * subSamples);
    const int remainder = 255 - weightPerHit * (subSamples * subSamples);
    std::vector<double> xs;
    xs.reserve(64);

    for (std::int64_t y = by0; y < by1; ++y) {
        for (int sy = 0; sy < subSamples; ++sy) {
            const double sampleY = static_cast<double>(y) + subOffset + sy * subStep;
            xs.clear();
            const std::size_t n = points.size();
            for (std::size_t i = 0; i < n; ++i) {
                const Vec2& a = points[i];
                const Vec2& b = points[(i + 1) % n];
                if ((a.y <= sampleY && b.y > sampleY) || (b.y <= sampleY && a.y > sampleY)) {
                    const double t = (sampleY - a.y) / (b.y - a.y);
                    xs.push_back(a.x + t * (b.x - a.x));
                }
            }
            if (xs.size() < 2) continue;
            std::sort(xs.begin(), xs.end());
            for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
                const double spanL = xs[k];
                const double spanR = xs[k + 1];
                if (spanR <= static_cast<double>(bx0) || spanL >= static_cast<double>(bx1)) continue;
                const std::int64_t pxStart = std::max(bx0, static_cast<std::int64_t>(std::floor(spanL)));
                const std::int64_t pxEnd = std::min(bx1 - 1, static_cast<std::int64_t>(std::floor(spanR)));
                for (std::int64_t px = pxStart; px <= pxEnd; ++px) {
                    int hits = 0;
                    for (int sx = 0; sx < subSamples; ++sx) {
                        const double sampleX = static_cast<double>(px) + subOffset + sx * subStep;
                        if (sampleX >= spanL && sampleX < spanR) ++hits;
                    }
                    if (hits > 0) {
                        std::uint8_t& dst = buf[static_cast<std::size_t>((y - by0) * bw + (px - bx0))];
                        const int add = hits * weightPerHit + ((hits == subSamples && sy == subSamples - 1) ? remainder : 0);
                        dst = static_cast<std::uint8_t>(std::min(255, static_cast<int>(dst) + add));
                    }
                }
            }
        }
    }

    if (featherPx > 0) featherBuffer(buf, bw, bh, featherPx);
    applyRectMask(bx0, by0, bw, bh, buf, op);
}

void SelectionMask::morphRadius(int radiusPx, bool dilate)
{
    if (radiusPx <= 0 || m_width <= 0 || m_height <= 0 || empty()) return;
    const PixelBounds b = bounds();
    if (b.empty()) return;

    const int pad = radiusPx + 2;
    const std::int64_t x0 = std::max<std::int64_t>(0, b.x - pad);
    const std::int64_t y0 = std::max<std::int64_t>(0, b.y - pad);
    const std::int64_t x1 = std::min<std::int64_t>(m_width, b.x + b.width + pad);
    const std::int64_t y1 = std::min<std::int64_t>(m_height, b.y + b.height + pad);
    const std::int64_t w = x1 - x0;
    const std::int64_t h = y1 - y0;
    if (w <= 0 || h <= 0) return;

    std::vector<std::uint8_t> src(static_cast<std::size_t>(w * h));
    for (std::int64_t y = 0; y < h; ++y) {
        for (std::int64_t x = 0; x < w; ++x) {
            src[static_cast<std::size_t>(y * w + x)] = at(x0 + x, y0 + y);
        }
    }

    std::vector<std::uint8_t> dst(static_cast<std::size_t>(w * h), 0);
    const int r2 = radiusPx * radiusPx;
    for (std::int64_t y = 0; y < h; ++y) {
        for (std::int64_t x = 0; x < w; ++x) {
            std::uint8_t best = dilate ? 0 : 255;
            for (int dy = -radiusPx; dy <= radiusPx; ++dy) {
                const std::int64_t sy = y + dy;
                if (sy < 0 || sy >= h) {
                    if (!dilate) { best = 0; break; }
                    continue;
                }
                for (int dx = -radiusPx; dx <= radiusPx; ++dx) {
                    if (dx * dx + dy * dy > r2) continue;
                    const std::int64_t sx = x + dx;
                    const std::uint8_t v = (sx < 0 || sx >= w)
                        ? 0
                        : src[static_cast<std::size_t>(sy * w + sx)];
                    if (dilate) best = std::max(best, v);
                    else best = std::min(best, v);
                }
            }
            dst[static_cast<std::size_t>(y * w + x)] = best;
        }
    }

    applyRectMask(x0, y0, w, h, dst, SelectionOp::Replace);
}

void SelectionMask::expand(int radiusPx)
{
    morphRadius(radiusPx, true);
}

void SelectionMask::contract(int radiusPx)
{
    morphRadius(radiusPx, false);
}

void SelectionMask::feather(int radiusPx)
{
    if (radiusPx <= 0 || m_width <= 0 || m_height <= 0 || empty()) return;
    const PixelBounds b = bounds();
    if (b.empty()) return;

    const int pad = radiusPx * 2 + 2;
    const std::int64_t x0 = std::max<std::int64_t>(0, b.x - pad);
    const std::int64_t y0 = std::max<std::int64_t>(0, b.y - pad);
    const std::int64_t x1 = std::min<std::int64_t>(m_width, b.x + b.width + pad);
    const std::int64_t y1 = std::min<std::int64_t>(m_height, b.y + b.height + pad);
    const std::int64_t w = x1 - x0;
    const std::int64_t h = y1 - y0;
    if (w <= 0 || h <= 0) return;

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w * h));
    for (std::int64_t y = 0; y < h; ++y) {
        for (std::int64_t x = 0; x < w; ++x) {
            buf[static_cast<std::size_t>(y * w + x)] = at(x0 + x, y0 + y);
        }
    }
    featherBuffer(buf, w, h, radiusPx);
    applyRectMask(x0, y0, w, h, buf, SelectionOp::Replace);
}

SelectionMask::Snapshot SelectionMask::snapshot() const
{
    Snapshot snap;
    snap.width = m_width;
    snap.height = m_height;
    snap.defaultCoverage = m_defaultCoverage;
    snap.tiles = m_tiles;
    return snap;
}

void SelectionMask::restore(const Snapshot& snap)
{
    m_width = snap.width;
    m_height = snap.height;
    m_defaultCoverage = snap.defaultCoverage;
    m_tiles = snap.tiles;
    m_cachedRevision = 0;
    ++m_revision;
}

} // namespace komapaint::region
