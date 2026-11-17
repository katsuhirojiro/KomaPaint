#include "RegionEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace komapaint::region {
namespace {

inline std::uint64_t hashMix(std::uint64_t h, std::uint64_t v) noexcept
{
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

inline bool layerIncluded(const ReferenceLayerDesc& layer, ReferenceMode mode) noexcept
{
    switch (mode) {
    case ReferenceMode::CurrentLayer:
        return layer.active && layer.visible;
    case ReferenceMode::AllVisible:
        return layer.visible;
    case ReferenceMode::SelectedLayers:
        return layer.visible && (layer.selected || layer.active);
    case ReferenceMode::ExcludeSelected:
        return layer.visible && !layer.selected && !layer.active;
    }
    return layer.visible;
}

inline void unpremultiply(std::uint32_t argb, double& r, double& g, double& b, double& a) noexcept
{
    const std::uint8_t ia = static_cast<std::uint8_t>((argb >> 24) & 0xFFU);
    if (ia == 0) {
        r = g = b = a = 0.0;
        return;
    }
    a = static_cast<double>(ia) / 255.0;
    const double invA = 255.0 / static_cast<double>(ia);
    r = std::min(1.0, static_cast<double>((argb >> 16) & 0xFFU) / 255.0 * invA);
    g = std::min(1.0, static_cast<double>((argb >> 8) & 0xFFU) / 255.0 * invA);
    b = std::min(1.0, static_cast<double>(argb & 0xFFU) / 255.0 * invA);
}

// Fast Oklab-like perceptual coordinates for RGB [0..1]
inline void rgbToPerceptualLab(double r, double g, double b, double& L, double& A, double& B) noexcept
{
    // Linearize approx (gamma ~ 2.0 fast)
    const double lr = r * r;
    const double lg = g * g;
    const double lb = b * b;
    const double l = std::cbrt(0.4122214708 * lr + 0.5363325363 * lg + 0.0514459929 * lb);
    const double m = std::cbrt(0.2119034982 * lr + 0.6806995451 * lg + 0.1073969566 * lb);
    const double s = std::cbrt(0.0883024619 * lr + 0.2817188376 * lg + 0.6299787005 * lb);
    L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}

inline double pixelLuminance(std::uint32_t argb) noexcept
{
    const double a = static_cast<double>((argb >> 24) & 0xFFU) / 255.0;
    if (a <= 1e-4) return 0.0;
    const double r = static_cast<double>((argb >> 16) & 0xFFU) / 255.0;
    const double g = static_cast<double>((argb >> 8) & 0xFFU) / 255.0;
    const double b = static_cast<double>(argb & 0xFFU) / 255.0;
    return (0.2126 * r + 0.7152 * g + 0.0722 * b) / a;
}

inline void dilateBinary(const std::uint8_t* src, std::uint8_t* dst,
                         std::int64_t w, std::int64_t h, int radius)
{
    if (radius <= 0) {
        std::copy_n(src, static_cast<std::size_t>(w * h), dst);
        return;
    }
    const int r2 = radius * radius;
    std::fill_n(dst, static_cast<std::size_t>(w * h), 0);
    for (std::int64_t y = 0; y < h; ++y) {
        for (std::int64_t x = 0; x < w; ++x) {
            if (!src[static_cast<std::size_t>(y * w + x)]) continue;
            for (int dy = -radius; dy <= radius; ++dy) {
                const std::int64_t ny = y + dy;
                if (ny < 0 || ny >= h) continue;
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dy * dy > r2) continue;
                    const std::int64_t nx = x + dx;
                    if (nx < 0 || nx >= w) continue;
                    dst[static_cast<std::size_t>(ny * w + nx)] = 1;
                }
            }
        }
    }
}

inline void erodeBinary(const std::uint8_t* src, std::uint8_t* dst,
                        std::int64_t w, std::int64_t h, int radius)
{
    if (radius <= 0) {
        std::copy_n(src, static_cast<std::size_t>(w * h), dst);
        return;
    }
    const int r2 = radius * radius;
    for (std::int64_t y = 0; y < h; ++y) {
        for (std::int64_t x = 0; x < w; ++x) {
            if (!src[static_cast<std::size_t>(y * w + x)]) {
                dst[static_cast<std::size_t>(y * w + x)] = 0;
                continue;
            }
            std::uint8_t keep = 1;
            for (int dy = -radius; dy <= radius && keep; ++dy) {
                const std::int64_t ny = y + dy;
                if (ny < 0 || ny >= h) { keep = 0; break; }
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dy * dy > r2) continue;
                    const std::int64_t nx = x + dx;
                    if (nx < 0 || nx >= w || !src[static_cast<std::size_t>(ny * w + nx)]) {
                        keep = 0;
                        break;
                    }
                }
            }
            dst[static_cast<std::size_t>(y * w + x)] = keep;
        }
    }
}

void drawBarrierSegment(std::uint8_t* barrier, std::uint8_t* uncertain,
                        std::int64_t w, std::int64_t h,
                        int x0, int y0, int x1, int y1,
                        std::uint8_t uncertainWeight)
{
    int dx = std::abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    int x = x0;
    int y = y0;
    for (;;) {
        if (x >= 0 && y >= 0 && x < w && y < h) {
            const std::size_t idx = static_cast<std::size_t>(y * w + x);
            barrier[idx] = 1;
            if (uncertain) uncertain[idx] = std::max(uncertain[idx], uncertainWeight);
            // 4-connected thickening by 1 px along diagonal steps so flood cannot leak diagonally
            if (x + sx >= 0 && x + sx < w && y >= 0 && y < h) {
                const std::size_t sidx = static_cast<std::size_t>(y * w + (x + sx));
                barrier[sidx] = 1;
                if (uncertain) uncertain[sidx] = std::max(uncertain[sidx], uncertainWeight);
            }
        }
        if (x == x1 && y == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

inline void boxFeatherMask(std::vector<std::uint8_t>& buf, std::int64_t w, std::int64_t h, int radius)
{
    if (radius <= 0 || w <= 0 || h <= 0) return;
    std::vector<std::uint8_t> tmp(buf.size());
    const int win = 2 * radius + 1;
    for (std::int64_t y = 0; y < h; ++y) {
        int sum = 0;
        for (int k = -radius; k <= radius; ++k) {
            sum += buf[static_cast<std::size_t>(y * w + std::clamp<std::int64_t>(k, 0, w - 1))];
        }
        for (std::int64_t x = 0; x < w; ++x) {
            tmp[static_cast<std::size_t>(y * w + x)] = static_cast<std::uint8_t>((sum + win / 2) / win);
            const std::int64_t rx = std::clamp<std::int64_t>(x - radius, 0, w - 1);
            const std::int64_t ax = std::clamp<std::int64_t>(x + radius + 1, 0, w - 1);
            sum += static_cast<int>(buf[static_cast<std::size_t>(y * w + ax)]) -
                   static_cast<int>(buf[static_cast<std::size_t>(y * w + rx)]);
        }
    }
    for (std::int64_t x = 0; x < w; ++x) {
        int sum = 0;
        for (int k = -radius; k <= radius; ++k) {
            sum += tmp[static_cast<std::size_t>(std::clamp<std::int64_t>(k, 0, h - 1) * w + x)];
        }
        for (std::int64_t y = 0; y < h; ++y) {
            buf[static_cast<std::size_t>(y * w + x)] = static_cast<std::uint8_t>((sum + win / 2) / win);
            const std::int64_t ry = std::clamp<std::int64_t>(y - radius, 0, h - 1);
            const std::int64_t ay = std::clamp<std::int64_t>(y + radius + 1, 0, h - 1);
            sum += static_cast<int>(tmp[static_cast<std::size_t>(ay * w + x)]) -
                   static_cast<int>(tmp[static_cast<std::size_t>(ry * w + x)]);
        }
    }
}

void scaleCoverageMask(std::vector<std::uint8_t>& cov,
                       const std::uint8_t* hardBarrier,
                       std::int64_t w, std::int64_t h,
                       int areaScaling, bool respectHardBarrier)
{
    if (areaScaling == 0 || w <= 0 || h <= 0) return;
    const int steps = std::min(16, std::abs(areaScaling));
    std::vector<std::uint8_t> tmp(cov.size());
    const bool expand = areaScaling > 0;
    for (int s = 0; s < steps; ++s) {
        tmp = cov;
        for (std::int64_t y = 0; y < h; ++y) {
            for (std::int64_t x = 0; x < w; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y * w + x);
                if (expand) {
                    std::uint8_t m = cov[idx];
                    if (x > 0) m = std::max(m, cov[idx - 1]);
                    if (x + 1 < w) m = std::max(m, cov[idx + 1]);
                    if (y > 0) m = std::max(m, cov[idx - static_cast<std::size_t>(w)]);
                    if (y + 1 < h) m = std::max(m, cov[idx + static_cast<std::size_t>(w)]);
                    if (respectHardBarrier && hardBarrier && hardBarrier[idx] && cov[idx] == 0) {
                        // Allow partial fringe bleed (anti-halo) without wiping solid ink center.
                        m = static_cast<std::uint8_t>(std::min<int>(m, 140));
                    }
                    tmp[idx] = std::max(tmp[idx], m);
                } else {
                    std::uint8_t m = cov[idx];
                    if (x > 0) m = std::min(m, cov[idx - 1]);
                    else m = 0;
                    if (x + 1 < w) m = std::min(m, cov[idx + 1]);
                    else m = 0;
                    if (y > 0) m = std::min(m, cov[idx - static_cast<std::size_t>(w)]);
                    else m = 0;
                    if (y + 1 < h) m = std::min(m, cov[idx + static_cast<std::size_t>(w)]);
                    else m = 0;
                    tmp[idx] = m;
                }
            }
        }
        cov.swap(tmp);
    }
}

// Tighten a raw [0..w*h) buffer to non-zero coverage bounding box.
RegionResult packResult(const std::vector<std::uint8_t>& fullCov,
                        const std::vector<std::uint8_t>& fullBound,
                        const std::vector<std::uint8_t>& fullUncertain,
                        std::int64_t winX, std::int64_t winY,
                        std::int64_t winW, std::int64_t winH,
                        RegionStatus status, double confidence,
                        std::uint64_t sig)
{
    RegionResult res;
    res.status = status;
    res.confidence = std::clamp(confidence, 0.0, 1.0);
    res.sourceSignature = sig;

    std::int64_t minX = winW, minY = winH, maxX = -1, maxY = -1;
    std::size_t filled = 0;
    std::size_t uncertainCount = 0;
    for (std::int64_t y = 0; y < winH; ++y) {
        for (std::int64_t x = 0; x < winW; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y * winW + x);
            const std::uint8_t c = fullCov[idx];
            const std::uint8_t u = (idx < fullUncertain.size()) ? fullUncertain[idx] : 0;
            if (c > 0) ++filled;
            if (u > 0) ++uncertainCount;
            if (c > 0 || u > 0) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (maxX < minX || maxY < minY || filled == 0) {
        res.status = RegionStatus::Empty;
        return res;
    }

    const std::int64_t rw = maxX - minX + 1;
    const std::int64_t rh = maxY - minY + 1;
    res.bounds = {winX + minX, winY + minY, rw, rh};
    res.filledPixels = filled;
    res.uncertainPixels = uncertainCount;
    res.coverage.resize(static_cast<std::size_t>(rw * rh));
    res.boundaryConfidence.resize(static_cast<std::size_t>(rw * rh));
    res.uncertainMask.resize(static_cast<std::size_t>(rw * rh));

    for (std::int64_t y = 0; y < rh; ++y) {
        const std::size_t srcOff = static_cast<std::size_t>((minY + y) * winW + minX);
        const std::size_t dstOff = static_cast<std::size_t>(y * rw);
        std::copy_n(fullCov.data() + srcOff, static_cast<std::size_t>(rw), res.coverage.data() + dstOff);
        if (!fullBound.empty()) {
            std::copy_n(fullBound.data() + srcOff, static_cast<std::size_t>(rw), res.boundaryConfidence.data() + dstOff);
        }
        if (!fullUncertain.empty()) {
            std::copy_n(fullUncertain.data() + srcOff, static_cast<std::size_t>(rw), res.uncertainMask.data() + dstOff);
        }
    }
    return res;
}

} // namespace

std::uint64_t ReferenceSet::signature() const noexcept
{
    std::uint64_t h = 0x123456789abcdef0ULL;
    h = hashMix(h, static_cast<std::uint64_t>(docWidth));
    h = hashMix(h, static_cast<std::uint64_t>(docHeight));
    h = hashMix(h, backgroundArgb);
    h = hashMix(h, transparentBackground ? 1ULL : 0ULL);
    h = hashMix(h, static_cast<std::uint64_t>(mode));
    for (const auto& layer : layersTopFirst) {
        if (!layerIncluded(layer, mode)) continue;
        h = hashMix(h, reinterpret_cast<std::uintptr_t>(layer.layerKey));
        h = hashMix(h, static_cast<std::uint64_t>(std::lround(layer.opacity * 1000.0)));
        h = hashMix(h, static_cast<std::uint64_t>(layer.blendMode));
        h = hashMix(h, layer.visible ? 1ULL : 0ULL);
        h = hashMix(h, layer.selected ? 2ULL : 0ULL);
        h = hashMix(h, layer.active ? 4ULL : 0ULL);
    }
    return h;
}

std::uint8_t RegionResult::coverageAt(std::int64_t x, std::int64_t y) const noexcept
{
    if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height)
        return 0;
    const std::int64_t lx = x - bounds.x;
    const std::int64_t ly = y - bounds.y;
    const std::size_t idx = static_cast<std::size_t>(ly * bounds.width + lx);
    return (idx < coverage.size()) ? coverage[idx] : 0;
}

std::uint8_t RegionResult::uncertainAt(std::int64_t x, std::int64_t y) const noexcept
{
    if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height)
        return 0;
    const std::int64_t lx = x - bounds.x;
    const std::int64_t ly = y - bounds.y;
    const std::size_t idx = static_cast<std::size_t>(ly * bounds.width + lx);
    return (idx < uncertainMask.size()) ? uncertainMask[idx] : 0;
}

void RegionCache::invalidateAll() noexcept
{
    m_signature = 0;
    m_tiles.clear();
}

void RegionCache::syncWith(const ReferenceSet& refs)
{
    const std::uint64_t sig = refs.signature();
    if (sig != m_signature || m_docWidth != refs.docWidth || m_docHeight != refs.docHeight) {
        m_tiles.clear();
        m_signature = sig;
        m_docWidth = refs.docWidth;
        m_docHeight = refs.docHeight;
    }
}

std::uint64_t RegionCache::computeTileRevision(const ReferenceSet& refs, TilePoint tile) const noexcept
{
    std::uint64_t h = refs.signature();
    const std::int64_t rx0 = tile.x * kTileSize;
    const std::int64_t ry0 = tile.y * kTileSize;
    const std::int64_t rx1 = std::min(refs.docWidth, rx0 + kTileSize);
    const std::int64_t ry1 = std::min(refs.docHeight, ry0 + kTileSize);

    for (const auto& layer : refs.layersTopFirst) {
        if (!layerIncluded(layer, refs.mode)) continue;
        h = hashMix(h, layer.revision);
        if (layer.digital && rx1 > rx0 && ry1 > ry0) {
            const auto& dgeom = layer.digital->geometry();
            if (dgeom.tileSize() == static_cast<int>(kTileSize)) {
                const auto span = layer.digital->tilePixels(tile);
                h = hashMix(h, span.empty() ? 0ULL : layer.digital->dirtyVersion());
            } else if (auto range = dgeom.tilesForRect({rx0, ry0, rx1, ry1})) {
                bool anyAllocated = false;
                for (auto ty = range->first.y; ty <= range->last.y && !anyAllocated; ++ty) {
                    for (auto tx = range->first.x; tx <= range->last.x; ++tx) {
                        if (!layer.digital->tilePixels({tx, ty}).empty()) {
                            anyAllocated = true;
                            break;
                        }
                    }
                }
                h = hashMix(h, anyAllocated ? layer.digital->dirtyVersion() : 0ULL);
            }
        }
    }
    return h;
}

void RegionCache::buildTile(const ReferenceSet& refs, TilePoint tile, CachedTile& out)
{
    out.argb.assign(kTilePixels, 0U);
    out.edgeStrength.assign(kTilePixels, 0);
    out.strokeScore.assign(kTilePixels, 0);
    out.gradX.assign(kTilePixels, 0);
    out.gradY.assign(kTilePixels, 0);

    int storeTileSize = static_cast<int>(kTileSize);
    for (const auto& layer : refs.layersTopFirst) {
        if (!layerIncluded(layer, refs.mode)) continue;
        if (layer.digital) {
            storeTileSize = layer.digital->geometry().tileSize();
            break;
        }
    }

    const TileGeometry docGeom(refs.docWidth, refs.docHeight, storeTileSize);
    const std::uint32_t bg = refs.transparentBackground ? 0x00000000U : refs.backgroundArgb;
    const std::size_t expectedOverridePixels = static_cast<std::size_t>(storeTileSize) * static_cast<std::size_t>(storeTileSize);

    auto composeDocTile = [&](TilePoint docTile, std::int64_t& outW, std::int64_t& outH) -> bool {
        std::vector<CompositeLayer> stack;
        std::vector<std::span<const std::uint32_t>> overrides;
        stack.reserve(refs.layersTopFirst.size());
        m_scratchMaterial.clear();
        for (const auto& layer : refs.layersTopFirst) {
            if (!layerIncluded(layer, refs.mode)) continue;
            CompositeLayer cl;
            cl.store = layer.digital;
            cl.opacity = layer.opacity;
            cl.blendMode = layer.blendMode;
            cl.visible = layer.visible;
            std::span<const std::uint32_t> ov;
            if (layer.renderMaterialTile) {
                m_scratchMaterial.emplace_back();
                auto& matBuf = m_scratchMaterial.back();
                if (layer.renderMaterialTile(docTile, matBuf) && matBuf.size() == expectedOverridePixels) {
                    ov = matBuf;
                }
            }
            stack.push_back(cl);
            overrides.push_back(ov);
        }
        return CpuTileCompositor::composeInto(docGeom, docTile, stack, bg, overrides, m_scratchCompose, outW, outH);
    };

    if (storeTileSize == static_cast<int>(kTileSize)) {
        std::int64_t outW = 0, outH = 0;
        if (composeDocTile(tile, outW, outH) && outW > 0 && outH > 0) {
            for (std::int64_t y = 0; y < std::min<std::int64_t>(kTileSize, outH); ++y) {
                std::copy_n(m_scratchCompose.data() + y * outW,
                            static_cast<std::size_t>(std::min<std::int64_t>(kTileSize, outW)),
                            out.argb.data() + y * kTileSize);
            }
        }
    } else {
        const std::int64_t rx0 = tile.x * kTileSize;
        const std::int64_t ry0 = tile.y * kTileSize;
        const std::int64_t rx1 = std::min(refs.docWidth, rx0 + kTileSize);
        const std::int64_t ry1 = std::min(refs.docHeight, ry0 + kTileSize);
        if (rx1 > rx0 && ry1 > ry0) {
            if (auto range = docGeom.tilesForRect({rx0, ry0, rx1, ry1})) {
                for (auto dty = range->first.y; dty <= range->last.y; ++dty) {
                    for (auto dtx = range->first.x; dtx <= range->last.x; ++dtx) {
                        const TilePoint docTile{dtx, dty};
                        const auto tb = docGeom.tileBounds(docTile);
                        if (!tb) continue;
                        std::int64_t outW = 0, outH = 0;
                        if (!composeDocTile(docTile, outW, outH) || outW <= 0 || outH <= 0) continue;
                        const std::int64_t ix0 = std::max(rx0, tb->left);
                        const std::int64_t iy0 = std::max(ry0, tb->top);
                        const std::int64_t ix1 = std::min(rx1, tb->right);
                        const std::int64_t iy1 = std::min(ry1, tb->bottom);
                        for (std::int64_t gy = iy0; gy < iy1; ++gy) {
                            const std::int64_t srcY = gy - tb->top;
                            const std::int64_t srcX = ix0 - tb->left;
                            const std::int64_t dstY = gy - ry0;
                            const std::int64_t dstX = ix0 - rx0;
                            const std::size_t count = static_cast<std::size_t>(ix1 - ix0);
                            std::copy_n(m_scratchCompose.data() + srcY * outW + srcX,
                                        count,
                                        out.argb.data() + dstY * kTileSize + dstX);
                        }
                    }
                }
            }
        }
    }

    // Extract edgeStrength, strokeScore, and quantized gradient inside this tile.
    auto lumaAt = [&](int lx, int ly) -> double {
        const int cx = std::clamp(lx, 0, 63);
        const int cy = std::clamp(ly, 0, 63);
        return pixelLuminance(out.argb[static_cast<std::size_t>(cy * 64 + cx)]);
    };
    auto alphaAt = [&](int lx, int ly) -> double {
        const int cx = std::clamp(lx, 0, 63);
        const int cy = std::clamp(ly, 0, 63);
        return static_cast<double>((out.argb[static_cast<std::size_t>(cy * 64 + cx)] >> 24) & 0xFFU) / 255.0;
    };

    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y * 64 + x);
            const std::uint32_t c = out.argb[idx];
            const double a = static_cast<double>((c >> 24) & 0xFFU) / 255.0;

            // Sobel on both luminance and alpha so transparent-bg ink & opaque-bg ink both register strongly.
            const double gxL = (-lumaAt(x - 1, y - 1) + lumaAt(x + 1, y - 1)
                              - 2.0 * lumaAt(x - 1, y) + 2.0 * lumaAt(x + 1, y)
                              - lumaAt(x - 1, y + 1) + lumaAt(x + 1, y + 1)) * 0.25;
            const double gyL = (-lumaAt(x - 1, y - 1) - 2.0 * lumaAt(x, y - 1) - lumaAt(x + 1, y - 1)
                              + lumaAt(x - 1, y + 1) + 2.0 * lumaAt(x, y + 1) + lumaAt(x + 1, y + 1)) * 0.25;

            const double gxA = (-alphaAt(x - 1, y - 1) + alphaAt(x + 1, y - 1)
                              - 2.0 * alphaAt(x - 1, y) + 2.0 * alphaAt(x + 1, y)
                              - alphaAt(x - 1, y + 1) + alphaAt(x + 1, y + 1)) * 0.25;
            const double gyA = (-alphaAt(x - 1, y - 1) - 2.0 * alphaAt(x, y - 1) - alphaAt(x + 1, y - 1)
                              + alphaAt(x - 1, y + 1) + 2.0 * alphaAt(x, y + 1) + alphaAt(x + 1, y + 1)) * 0.25;

            const double magL = std::hypot(gxL, gyL);
            const double magA = std::hypot(gxA, gyA);
            const double edge = std::clamp(std::max(magL * 1.4, magA * 1.6), 0.0, 1.0);
            out.edgeStrength[idx] = static_cast<std::uint8_t>(std::lround(edge * 255.0));

            const double gx = (magL >= magA) ? gxL : gxA;
            const double gy = (magL >= magA) ? gyL : gyA;
            const double gmag = std::hypot(gx, gy);
            if (gmag > 1e-4) {
                out.gradX[idx] = static_cast<std::int8_t>(std::lround(std::clamp(gx / gmag, -1.0, 1.0) * 127.0));
                out.gradY[idx] = static_cast<std::int8_t>(std::lround(std::clamp(gy / gmag, -1.0, 1.0) * 127.0));
            }

            // Stroke ridge / ink presence:
            // - On transparent reference layers, alpha itself is strong ink evidence!
            // - On opaque layers, local contrast vs 5x5 neighborhood OR strong edge marks lines.
            double localAvgL = 0.0;
            int cnt = 0;
            for (int dy = -2; dy <= 2; dy += 2) {
                for (int dx = -2; dx <= 2; dx += 2) {
                    localAvgL += lumaAt(x + dx, y + dy);
                    ++cnt;
                }
            }
            localAvgL /= std::max(1, cnt);
            const double contrast = std::abs(lumaAt(x, y) - localAvgL);
            double score = edge * 0.75 + contrast * 1.1;
            if (refs.transparentBackground) {
                score = std::max(score, a * 0.95);
            }
            out.strokeScore[idx] = static_cast<std::uint8_t>(std::lround(std::clamp(score, 0.0, 1.0) * 255.0));
        }
    }
}

const RegionCache::CachedTile* RegionCache::acquireTile(const ReferenceSet& refs, TilePoint tile)
{
    syncWith(refs);
    const std::uint64_t rev = computeTileRevision(refs, tile);
    auto it = m_tiles.find(tile);
    if (it != m_tiles.end() && it->second.tileRevision == rev) {
        return &it->second;
    }
    CachedTile& cached = m_tiles[tile];
    buildTile(refs, tile, cached);
    cached.tileRevision = rev;
    return &cached;
}

std::uint32_t RegionCache::pixelAt(const ReferenceSet& refs, std::int64_t x, std::int64_t y)
{
    if (x < 0 || y < 0 || x >= refs.docWidth || y >= refs.docHeight) return 0;
    const TilePoint tp{x / kTileSize, y / kTileSize};
    const auto* t = acquireTile(refs, tp);
    if (!t) return 0;
    return t->argb[static_cast<std::size_t>((y % kTileSize) * kTileSize + (x % kTileSize))];
}

std::uint8_t RegionCache::edgeAt(const ReferenceSet& refs, std::int64_t x, std::int64_t y)
{
    if (x < 0 || y < 0 || x >= refs.docWidth || y >= refs.docHeight) return 0;
    const TilePoint tp{x / kTileSize, y / kTileSize};
    const auto* t = acquireTile(refs, tp);
    if (!t) return 0;
    return t->edgeStrength[static_cast<std::size_t>((y % kTileSize) * kTileSize + (x % kTileSize))];
}

double RegionEngine::perceptualDistance(std::uint32_t argbA, std::uint32_t argbB,
                                        bool perceptual, bool alphaAware) noexcept
{
    if (argbA == argbB) return 0.0;
    double ra, ga, ba, aa;
    double rb, gb, bb, ab;
    unpremultiply(argbA, ra, ga, ba, aa);
    unpremultiply(argbB, rb, gb, bb, ab);

    if (alphaAware && aa <= 1e-4 && ab <= 1e-4) return 0.0;

    const double alphaDiff = alphaAware ? std::abs(aa - ab) * 255.0 : 0.0;
    if (alphaAware && (aa <= 1e-4 || ab <= 1e-4)) {
        return alphaDiff;
    }

    double colorDist = 0.0;
    if (perceptual) {
        double La, Aa, Ba, Lb, Ab, Bb;
        rgbToPerceptualLab(ra, ga, ba, La, Aa, Ba);
        rgbToPerceptualLab(rb, gb, bb, Lb, Ab, Bb);
        const double dL = (La - Lb) * 255.0;
        const double dA = (Aa - Ab) * 320.0;
        const double dB = (Ba - Bb) * 320.0;
        colorDist = std::sqrt(dL * dL + dA * dA + dB * dB);
    } else {
        const double dr = (ra - rb) * 255.0;
        const double dg = (ga - gb) * 255.0;
        const double db = (ba - bb) * 255.0;
        colorDist = std::sqrt((dr * dr + dg * dg + db * db) / 3.0);
    }

    const double minAlpha = alphaAware ? std::min(aa, ab) : 1.0;
    return std::max(alphaDiff, colorDist * std::clamp(minAlpha * 1.25, 0.25, 1.0));
}

RegionResult RegionEngine::computeConventionalFill(const ReferenceSet& refs,
                                                   std::int64_t seedX, std::int64_t seedY,
                                                   const FillParams& params,
                                                   const SelectionMask* selection)
{
    if (refs.docWidth <= 0 || refs.docHeight <= 0 ||
        seedX < 0 || seedY < 0 || seedX >= refs.docWidth || seedY >= refs.docHeight) {
        return {};
    }
    if (selection && selection->hasSelection() && selection->at(seedX, seedY) == 0) {
        return {};
    }

    m_cache.syncWith(refs);
    const std::uint32_t seedColor = m_cache.pixelAt(refs, seedX, seedY);
    const double tol = static_cast<double>(std::clamp(params.tolerance, 0, 255));

    // Scope working window to selection bounds if active, otherwise full document
    // (capped around seed on huge >2048px canvases to avoid multi-hundred-MB scratch buffers).
    std::int64_t winX = 0, winY = 0, winW = refs.docWidth, winH = refs.docHeight;
    if (selection && selection->hasSelection() && selection->defaultCoverage() == 0) {
        const PixelBounds sb = selection->bounds();
        if (sb.empty()) return {};
        winX = std::max<std::int64_t>(0, sb.x - 2);
        winY = std::max<std::int64_t>(0, sb.y - 2);
        winW = std::min<std::int64_t>(refs.docWidth, sb.x + sb.width + 2) - winX;
        winH = std::min<std::int64_t>(refs.docHeight, sb.y + sb.height + 2) - winY;
    } else if (refs.docWidth * refs.docHeight > 2048LL * 2048LL) {
        const std::int64_t halfWin = 1024;
        winX = std::max<std::int64_t>(0, seedX - halfWin);
        winY = std::max<std::int64_t>(0, seedY - halfWin);
        const std::int64_t x1 = std::min<std::int64_t>(refs.docWidth, winX + 2 * halfWin);
        const std::int64_t y1 = std::min<std::int64_t>(refs.docHeight, winY + 2 * halfWin);
        winW = x1 - winX;
        winH = y1 - winY;
    }

    const std::size_t total = static_cast<std::size_t>(winW * winH);
    m_workVisited.assign(total, 0);
    m_workBarrier.assign(total, 0);
    m_workBoundary.assign(total, 0);
    m_workUncertain.clear();

    // Populate reference colors & optional gap-closing barrier tile-by-tile.
    std::vector<std::uint8_t> strokeBin;
    std::vector<std::uint8_t> closedBin;
    if (params.gapClosing > 0) {
        strokeBin.assign(total, 0);
        closedBin.assign(total, 0);
    }

    const std::int64_t tx0 = winX / RegionCache::kTileSize;
    const std::int64_t ty0 = winY / RegionCache::kTileSize;
    const std::int64_t tx1 = (winX + winW - 1) / RegionCache::kTileSize;
    const std::int64_t ty1 = (winY + winH - 1) / RegionCache::kTileSize;

    for (std::int64_t ty = ty0; ty <= ty1; ++ty) {
        for (std::int64_t tx = tx0; tx <= tx1; ++tx) {
            const auto* tile = m_cache.acquireTile(refs, {tx, ty});
            if (!tile) continue;
            const std::int64_t bx = tx * RegionCache::kTileSize;
            const std::int64_t by = ty * RegionCache::kTileSize;
            const std::int64_t ix0 = std::max(winX, bx);
            const std::int64_t iy0 = std::max(winY, by);
            const std::int64_t ix1 = std::min(winX + winW, bx + RegionCache::kTileSize);
            const std::int64_t iy1 = std::min(winY + winH, by + RegionCache::kTileSize);
            for (std::int64_t gy = iy0; gy < iy1; ++gy) {
                for (std::int64_t gx = ix0; gx < ix1; ++gx) {
                    const std::size_t tidx = static_cast<std::size_t>((gy - by) * 64 + (gx - bx));
                    const std::size_t widx = static_cast<std::size_t>((gy - winY) * winW + (gx - winX));
                    const double dist = perceptualDistance(seedColor, tile->argb[tidx],
                                                           params.perceptualColor, params.alphaAware);
                    m_workBoundary[widx] = static_cast<std::uint8_t>(std::clamp(dist, 0.0, 255.0));
                    if (dist > tol) {
                        m_workBarrier[widx] = 1;
                    }
                    if (!strokeBin.empty() && (tile->strokeScore[tidx] >= 90 || dist > std::max(tol, 40.0))) {
                        strokeBin[widx] = 1;
                    }
                }
            }
        }
    }

    if (params.gapClosing > 0 && !strokeBin.empty()) {
        const int r = std::max(1, params.gapClosing / 2);
        std::vector<std::uint8_t> dilated(total, 0);
        dilateBinary(strokeBin.data(), dilated.data(), winW, winH, r);
        erodeBinary(dilated.data(), closedBin.data(), winW, winH, r);
        for (std::size_t i = 0; i < total; ++i) {
            if (closedBin[i]) m_workBarrier[i] = 1;
        }
    }

    // Enforce active selection mask on barrier.
    if (selection && selection->hasSelection()) {
        for (std::int64_t y = 0; y < winH; ++y) {
            for (std::int64_t x = 0; x < winW; ++x) {
                if (selection->at(winX + x, winY + y) == 0) {
                    m_workBarrier[static_cast<std::size_t>(y * winW + x)] = 1;
                }
            }
        }
    }

    const std::int64_t sx = seedX - winX;
    const std::int64_t sy = seedY - winY;
    const std::size_t seedIdx = static_cast<std::size_t>(sy * winW + sx);
    m_workBarrier[seedIdx] = 0; // Seed itself is always fillable if inside selection.

    if (params.contiguous) {
        m_workQueue.clear();
        m_workQueue.push_back(static_cast<std::int64_t>(seedIdx));
        m_workVisited[seedIdx] = 255;
        std::size_t head = 0;
        while (head < m_workQueue.size()) {
            const std::int64_t cur = m_workQueue[head++];
            const std::int64_t cx = cur % winW;
            const std::int64_t cy = cur / winW;
            const std::int64_t nx[4] = {cx - 1, cx + 1, cx, cx};
            const std::int64_t ny[4] = {cy, cy, cy - 1, cy + 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= winW || ny[k] >= winH) continue;
                const std::size_t nidx = static_cast<std::size_t>(ny[k] * winW + nx[k]);
                if (m_workVisited[nidx] || m_workBarrier[nidx]) continue;
                m_workVisited[nidx] = 255;
                m_workQueue.push_back(static_cast<std::int64_t>(nidx));
            }
        }
    } else {
        for (std::size_t i = 0; i < total; ++i) {
            if (!m_workBarrier[i]) m_workVisited[i] = 255;
        }
    }

    // Soft antialiased edge + optional color bleeding into antialiased fringe.
    if (params.antialiasing || params.colorBleeding) {
        std::vector<std::uint8_t> soft = m_workVisited;
        for (std::int64_t y = 0; y < winH; ++y) {
            for (std::int64_t x = 0; x < winW; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y * winW + x);
                if (m_workVisited[idx] > 0) continue;
                if (selection && selection->hasSelection() && selection->at(winX + x, winY + y) == 0) continue;
                bool touchesFill = false;
                if (x > 0 && m_workVisited[idx - 1] == 255) touchesFill = true;
                else if (x + 1 < winW && m_workVisited[idx + 1] == 255) touchesFill = true;
                else if (y > 0 && m_workVisited[idx - static_cast<std::size_t>(winW)] == 255) touchesFill = true;
                else if (y + 1 < winH && m_workVisited[idx + static_cast<std::size_t>(winW)] == 255) touchesFill = true;
                if (!touchesFill) continue;

                const double dist = static_cast<double>(m_workBoundary[idx]);
                const double falloff = std::clamp(1.0 - (dist - tol) / std::max(28.0, tol * 1.4), 0.0, 1.0);
                if (falloff > 0.05) {
                    const double weight = params.colorBleeding ? std::max(falloff, 0.45) : falloff * 0.75;
                    soft[idx] = static_cast<std::uint8_t>(std::lround(weight * 255.0));
                }
            }
        }
        m_workVisited.swap(soft);
    }

    if (params.areaScaling != 0) {
        scaleCoverageMask(m_workVisited, m_workBarrier.data(), winW, winH, params.areaScaling, params.colorBleeding);
    }

    // Re-clamp coverage to selection coverage!
    if (selection && selection->hasSelection()) {
        for (std::int64_t y = 0; y < winH; ++y) {
            for (std::int64_t x = 0; x < winW; ++x) {
                const std::uint8_t sc = selection->at(winX + x, winY + y);
                auto& v = m_workVisited[static_cast<std::size_t>(y * winW + x)];
                v = static_cast<std::uint8_t>((static_cast<int>(v) * static_cast<int>(sc) + 127) / 255);
            }
        }
    }

    return packResult(m_workVisited, m_workBoundary, m_workUncertain,
                      winX, winY, winW, winH,
                      RegionStatus::Enclosed, 1.0, m_cache.currentSignature());
}

RegionResult RegionEngine::computeSmartFill(const ReferenceSet& refs,
                                            std::int64_t seedX, std::int64_t seedY,
                                            const SmartFillParams& params,
                                            const SelectionMask* selection)
{
    if (refs.docWidth <= 0 || refs.docHeight <= 0 ||
        seedX < 0 || seedY < 0 || seedX >= refs.docWidth || seedY >= refs.docHeight) {
        return {};
    }
    if (selection && selection->hasSelection() && selection->at(seedX, seedY) == 0) {
        return {};
    }

    m_cache.syncWith(refs);
    const std::uint32_t seedColor = m_cache.pixelAt(refs, seedX, seedY);

    // Scope working window to selection bounds if active, otherwise full document
    // (capped at reasonable analysis tile window when huge? Sparse documents remain fast,
    // and we track leakage if touching outer bounds!).
    std::int64_t winX = 0, winY = 0, winW = refs.docWidth, winH = refs.docHeight;
    if (selection && selection->hasSelection() && selection->defaultCoverage() == 0) {
        const PixelBounds sb = selection->bounds();
        if (sb.empty()) return {};
        winX = std::max<std::int64_t>(0, sb.x - 4);
        winY = std::max<std::int64_t>(0, sb.y - 4);
        winW = std::min<std::int64_t>(refs.docWidth, sb.x + sb.width + 4) - winX;
        winH = std::min<std::int64_t>(refs.docHeight, sb.y + sb.height + 4) - winY;
    } else if (refs.docWidth * refs.docHeight > 2048LL * 2048LL) {
        const std::int64_t halfWin = 1024;
        winX = std::max<std::int64_t>(0, seedX - halfWin);
        winY = std::max<std::int64_t>(0, seedY - halfWin);
        const std::int64_t x1 = std::min<std::int64_t>(refs.docWidth, winX + 2 * halfWin);
        const std::int64_t y1 = std::min<std::int64_t>(refs.docHeight, winY + 2 * halfWin);
        winW = x1 - winX;
        winH = y1 - winY;
    }

    const std::size_t total = static_cast<std::size_t>(winW * winH);
    m_workVisited.assign(total, 0);
    m_workBarrier.assign(total, 0);
    m_workBoundary.assign(total, 0);
    m_workUncertain.assign(total, 0);

    const double sens = std::clamp(params.boundarySensitivity, 0, 100) / 100.0;
    const double tol = static_cast<double>(std::clamp(params.tolerance, 0, 255));
    // Higher boundarySensitivity lowers the stroke/edge threshold needed to form a boundary.
    const std::uint8_t strongStrokeThresh = static_cast<std::uint8_t>(std::lround(140.0 - sens * 75.0)); // ~65..140
    const std::uint8_t weakStrokeThresh = static_cast<std::uint8_t>(std::lround(strongStrokeThresh * 0.52));

    std::vector<std::int8_t> gxMap(total, 0);
    std::vector<std::int8_t> gyMap(total, 0);

    const std::int64_t tx0 = winX / RegionCache::kTileSize;
    const std::int64_t ty0 = winY / RegionCache::kTileSize;
    const std::int64_t tx1 = (winX + winW - 1) / RegionCache::kTileSize;
    const std::int64_t ty1 = (winY + winH - 1) / RegionCache::kTileSize;

    for (std::int64_t ty = ty0; ty <= ty1; ++ty) {
        for (std::int64_t tx = tx0; tx <= tx1; ++tx) {
            const auto* tile = m_cache.acquireTile(refs, {tx, ty});
            if (!tile) continue;
            const std::int64_t bx = tx * RegionCache::kTileSize;
            const std::int64_t by = ty * RegionCache::kTileSize;
            const std::int64_t ix0 = std::max(winX, bx);
            const std::int64_t iy0 = std::max(winY, by);
            const std::int64_t ix1 = std::min(winX + winW, bx + RegionCache::kTileSize);
            const std::int64_t iy1 = std::min(winY + winH, by + RegionCache::kTileSize);
            for (std::int64_t gy = iy0; gy < iy1; ++gy) {
                for (std::int64_t gx = ix0; gx < ix1; ++gx) {
                    const std::size_t tidx = static_cast<std::size_t>((gy - by) * 64 + (gx - bx));
                    const std::size_t widx = static_cast<std::size_t>((gy - winY) * winW + (gx - winX));

                    const double colorDist = perceptualDistance(seedColor, tile->argb[tidx],
                                                                params.perceptualColor, params.alphaAware);
                    const std::uint8_t sScore = tile->strokeScore[tidx];
                    const std::uint8_t eScore = tile->edgeStrength[tidx];
                    gxMap[widx] = tile->gradX[tidx];
                    gyMap[widx] = tile->gradY[tidx];

                    // Combined boundary confidence: line-art ridge + perceptual color diff
                    const double bVal = std::max({
                        static_cast<double>(sScore),
                        static_cast<double>(eScore) * (0.75 + 0.45 * sens),
                        (colorDist > tol) ? std::min(255.0, 120.0 + (colorDist - tol) * 2.0) : 0.0
                    });
                    m_workBoundary[widx] = static_cast<std::uint8_t>(std::lround(std::clamp(bVal, 0.0, 255.0)));

                    if (sScore >= strongStrokeThresh || colorDist > tol) {
                        m_workBarrier[widx] = 1;
                    }
                }
            }
        }
    }

    // Step 2: Detect boundary endpoints and infer tangent-guided gap bridges
    // up to params.gapTolerance pixels (distinguishing accidental gaps from wide openings!).
    const int gapMax = std::clamp(params.gapTolerance, 0, 32);
    std::size_t bridgedGaps = 0;
    if (gapMax >= 2) {
        struct Endpoint {
            int x = 0;
            int y = 0;
            double tx = 0.0;
            double ty = 0.0;
        };
        std::vector<Endpoint> endpoints;

        // Find pixels on m_workBarrier that lie at the tip of a contour (have barrier support
        // predominantly on one side / <= 2 barrier neighbors in 8-neighborhood, or directional tip).
        for (std::int64_t y = 2; y < winH - 2; ++y) {
            for (std::int64_t x = 2; x < winW - 2; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y * winW + x);
                if (!m_workBarrier[idx]) continue;

                // Gather nearby barrier centroid in 5x5 window to determine stroke direction.
                double sumDx = 0.0, sumDy = 0.0;
                int count5 = 0;
                int count3 = 0;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        if (m_workBarrier[static_cast<std::size_t>((y + dy) * winW + (x + dx))]) {
                            sumDx += static_cast<double>(dx);
                            sumDy += static_cast<double>(dy);
                            ++count5;
                            if (std::abs(dx) <= 1 && std::abs(dy) <= 1) ++count3;
                        }
                    }
                }
                if (count5 == 0 || count3 > 4) continue;

                const double mag = std::hypot(sumDx, sumDy);
                // Asymmetric neighborhood: contour ends here and points away from centroid (-sumDx, -sumDy)!
                if (mag > 0.65 * static_cast<double>(count5) && count5 >= 2) {
                    const double tx = -sumDx / mag;
                    const double ty = -sumDy / mag;
                    // Verify there is no immediate barrier 1..2 px ahead in the forward tip direction.
                    const int fx1 = static_cast<int>(std::lround(static_cast<double>(x) + tx * 1.5));
                    const int fy1 = static_cast<int>(std::lround(static_cast<double>(y) + ty * 1.5));
                    if (fx1 >= 0 && fy1 >= 0 && fx1 < winW && fy1 < winH &&
                        !m_workBarrier[static_cast<std::size_t>(fy1 * winW + fx1)]) {
                        endpoints.push_back({static_cast<int>(x), static_cast<int>(y), tx, ty});
                    }
                }
            }
        }

        // Match compatible facing endpoints within gapMax distance OR trace along weak stroke residuals.
        std::vector<bool> used(endpoints.size(), false);
        for (std::size_t i = 0; i < endpoints.size(); ++i) {
            if (used[i]) continue;
            const auto& a = endpoints[i];
            int bestJ = -1;
            double bestScore = 1e9;
            for (std::size_t j = i + 1; j < endpoints.size(); ++j) {
                if (used[j]) continue;
                const auto& b = endpoints[j];
                const double vx = static_cast<double>(b.x - a.x);
                const double vy = static_cast<double>(b.y - a.y);
                const double dist = std::hypot(vx, vy);
                if (dist < 2.0 || dist > static_cast<double>(gapMax)) continue;
                const double ux = vx / dist;
                const double uy = vy / dist;
                // Endpoint A must point generally toward B, and B generally toward A (or tangent alignment).
                const double alignA = a.tx * ux + a.ty * uy;
                const double alignB = -(b.tx * ux + b.ty * uy);
                if (alignA < 0.35 || alignB < 0.35) continue;
                const double score = dist - (alignA + alignB) * 2.5;
                if (score < bestScore) {
                    bestScore = score;
                    bestJ = static_cast<int>(j);
                }
            }
            if (bestJ >= 0) {
                const auto& b = endpoints[static_cast<std::size_t>(bestJ)];
                used[i] = true;
                used[static_cast<std::size_t>(bestJ)] = true;
                drawBarrierSegment(m_workBarrier.data(), m_workUncertain.data(),
                                   winW, winH, a.x, a.y, b.x, b.y, 180);
                ++bridgedGaps;
                continue;
            }

            // Second chance: connect endpoint to an opposite boundary wall if there's weak AA ink
            // along the ray OR a very tight accidental gap (<= gapMax * 0.75) hitting a facing wall.
            const int maxRay = std::max(2, static_cast<int>(std::lround(gapMax * 0.85)));
            for (int step = 2; step <= maxRay; ++step) {
                const int px = static_cast<int>(std::lround(static_cast<double>(a.x) + a.tx * step));
                const int py = static_cast<int>(std::lround(static_cast<double>(a.y) + a.ty * step));
                if (px < 0 || py < 0 || px >= winW || py >= winH) break;
                const std::size_t pidx = static_cast<std::size_t>(py * winW + px);
                if (m_workBarrier[pidx]) {
                    // Hit an opposing contour within gapMax! Verify weak evidence or close range.
                    int weakHits = 0;
                    for (int s = 1; s < step; ++s) {
                        const int rx = static_cast<int>(std::lround(static_cast<double>(a.x) + a.tx * s));
                        const int ry = static_cast<int>(std::lround(static_cast<double>(a.y) + a.ty * s));
                        if (rx >= 0 && ry >= 0 && rx < winW && ry < winH &&
                            m_workBoundary[static_cast<std::size_t>(ry * winW + rx)] >= weakStrokeThresh) {
                            ++weakHits;
                        }
                    }
                    if (step <= gapMax || weakHits > 0) {
                        drawBarrierSegment(m_workBarrier.data(), m_workUncertain.data(),
                                           winW, winH, a.x, a.y, px, py, 160);
                        ++bridgedGaps;
                    }
                    break;
                }
            }
        }
    }

    // Apply selection constraint to barrier
    if (selection && selection->hasSelection()) {
        for (std::int64_t y = 0; y < winH; ++y) {
            for (std::int64_t x = 0; x < winW; ++x) {
                if (selection->at(winX + x, winY + y) == 0) {
                    m_workBarrier[static_cast<std::size_t>(y * winW + x)] = 1;
                }
            }
        }
    }

    const std::int64_t sx = seedX - winX;
    const std::int64_t sy = seedY - winY;
    const std::size_t seedIdx = static_cast<std::size_t>(sy * winW + sx);
    m_workBarrier[seedIdx] = 0;

    bool touchedOuterEdge = false;
    if (params.contiguous) {
        m_workQueue.clear();
        m_workQueue.push_back(static_cast<std::int64_t>(seedIdx));
        m_workVisited[seedIdx] = 255;
        std::size_t head = 0;
        while (head < m_workQueue.size()) {
            const std::int64_t cur = m_workQueue[head++];
            const std::int64_t cx = cur % winW;
            const std::int64_t cy = cur / winW;
            if (cx == 0 || cy == 0 || cx == winW - 1 || cy == winH - 1) {
                touchedOuterEdge = true;
            }
            const std::int64_t nx[4] = {cx - 1, cx + 1, cx, cx};
            const std::int64_t ny[4] = {cy, cy, cy - 1, cy + 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= winW || ny[k] >= winH) continue;
                const std::size_t nidx = static_cast<std::size_t>(ny[k] * winW + nx[k]);
                if (m_workVisited[nidx] || m_workBarrier[nidx]) continue;
                m_workVisited[nidx] = 255;
                m_workQueue.push_back(static_cast<std::int64_t>(nidx));
            }
        }
    } else {
        for (std::size_t i = 0; i < total; ++i) {
            if (!m_workBarrier[i]) m_workVisited[i] = 255;
        }
    }

    // Consolidate tiny sketchy micro-pockets (e.g. 1..3 px holes completely trapped between
    // overlapping sketchy strokes inside/adjacent to the filled contour).
    if (params.edgeQuality >= 50) {
        std::vector<std::uint8_t> consolidated = m_workVisited;
        for (std::int64_t y = 1; y < winH - 1; ++y) {
            for (std::int64_t x = 1; x < winW - 1; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y * winW + x);
                if (m_workVisited[idx] > 0) continue;
                int filledNeighbors = 0;
                if (m_workVisited[idx - 1] == 255) ++filledNeighbors;
                if (m_workVisited[idx + 1] == 255) ++filledNeighbors;
                if (m_workVisited[idx - static_cast<std::size_t>(winW)] == 255) ++filledNeighbors;
                if (m_workVisited[idx + static_cast<std::size_t>(winW)] == 255) ++filledNeighbors;
                if (filledNeighbors >= 3 && m_workBoundary[idx] < strongStrokeThresh) {
                    consolidated[idx] = 255;
                }
            }
        }
        m_workVisited.swap(consolidated);
    }

    // Soft anti-aliased edge + color bleeding into line fringe.
    if (params.antialiasing || params.colorBleeding) {
        std::vector<std::uint8_t> soft = m_workVisited;
        for (std::int64_t y = 0; y < winH; ++y) {
            for (std::int64_t x = 0; x < winW; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y * winW + x);
                if (m_workVisited[idx] > 0) continue;
                if (selection && selection->hasSelection() && selection->at(winX + x, winY + y) == 0) continue;
                bool touchesFill = false;
                if (x > 0 && m_workVisited[idx - 1] == 255) touchesFill = true;
                else if (x + 1 < winW && m_workVisited[idx + 1] == 255) touchesFill = true;
                else if (y > 0 && m_workVisited[idx - static_cast<std::size_t>(winW)] == 255) touchesFill = true;
                else if (y + 1 < winH && m_workVisited[idx + static_cast<std::size_t>(winW)] == 255) touchesFill = true;
                if (!touchesFill) continue;

                const double bConf = static_cast<double>(m_workBoundary[idx]) / 255.0;
                const double alphaWeight = std::clamp(1.0 - bConf * 0.65, 0.15, 0.88);
                if (params.colorBleeding || alphaWeight > 0.2) {
                    soft[idx] = static_cast<std::uint8_t>(std::lround(alphaWeight * 255.0));
                }
            }
        }
        m_workVisited.swap(soft);
    }

    if (params.areaScaling != 0) {
        scaleCoverageMask(m_workVisited, m_workBarrier.data(), winW, winH, params.areaScaling, params.colorBleeding);
    }

    if (selection && selection->hasSelection()) {
        for (std::int64_t y = 0; y < winH; ++y) {
            for (std::int64_t x = 0; x < winW; ++x) {
                const std::uint8_t sc = selection->at(winX + x, winY + y);
                auto& v = m_workVisited[static_cast<std::size_t>(y * winW + x)];
                v = static_cast<std::uint8_t>((static_cast<int>(v) * static_cast<int>(sc) + 127) / 255);
            }
        }
    }

    RegionStatus status = RegionStatus::Enclosed;
    double confidence = 0.96;
    if (touchedOuterEdge && m_workQueue.size() > static_cast<std::size_t>(winW * winH) / 2) {
        // Flood escaped across outer border into most of the canvas: mark ambiguous leak!
        status = RegionStatus::AmbiguousLeak;
        confidence = 0.35;
    } else if (bridgedGaps > 0) {
        status = RegionStatus::PartiallyEnclosed;
        confidence = std::max(0.58, 0.90 - 0.04 * static_cast<double>(bridgedGaps));
    }

    return packResult(m_workVisited, m_workBoundary, m_workUncertain,
                      winX, winY, winW, winH,
                      status, confidence, m_cache.currentSignature());
}

RegionResult RegionEngine::computeMagicWand(const ReferenceSet& refs,
                                            std::int64_t seedX, std::int64_t seedY,
                                            const MagicWandParams& params,
                                            const SelectionMask* selection)
{
    FillParams fp;
    fp.tolerance = params.tolerance;
    fp.perceptualColor = params.perceptualColor;
    fp.alphaAware = params.alphaAware;
    fp.gapClosing = params.gapClosing;
    fp.areaScaling = params.areaScaling;
    fp.antialiasing = params.antialiasing;
    fp.colorBleeding = false;
    fp.contiguous = params.contiguous;

    RegionResult res = computeConventionalFill(refs, seedX, seedY, fp, selection);
    if (params.feather > 0 && !res.empty()) {
        // Pad by feather*2 and blur
        const int pad = params.feather * 2;
        const std::int64_t nx = std::max<std::int64_t>(0, res.bounds.x - pad);
        const std::int64_t ny = std::max<std::int64_t>(0, res.bounds.y - pad);
        const std::int64_t nx1 = std::min<std::int64_t>(refs.docWidth, res.bounds.x + res.bounds.width + pad);
        const std::int64_t ny1 = std::min<std::int64_t>(refs.docHeight, res.bounds.y + res.bounds.height + pad);
        const std::int64_t nw = nx1 - nx;
        const std::int64_t nh = ny1 - ny;
        if (nw > 0 && nh > 0) {
            std::vector<std::uint8_t> buf(static_cast<std::size_t>(nw * nh), 0);
            for (std::int64_t y = 0; y < res.bounds.height; ++y) {
                for (std::int64_t x = 0; x < res.bounds.width; ++x) {
                    const std::int64_t dx = res.bounds.x + x - nx;
                    const std::int64_t dy = res.bounds.y + y - ny;
                    buf[static_cast<std::size_t>(dy * nw + dx)] =
                        res.coverage[static_cast<std::size_t>(y * res.bounds.width + x)];
                }
            }
            boxFeatherMask(buf, nw, nh, params.feather);
            res.bounds = {nx, ny, nw, nh};
            res.coverage = std::move(buf);
            res.boundaryConfidence.assign(res.coverage.size(), 0);
            res.uncertainMask.assign(res.coverage.size(), 0);
        }
    }
    return res;
}

RegionResult RegionEngine::computeProtectedRegion(const ReferenceSet& refs,
                                                  std::int64_t seedX, std::int64_t seedY,
                                                  const ProtectedRegionParams& params,
                                                  const SelectionMask* selection)
{
    SmartFillParams sfp;
    sfp.tolerance = params.tolerance;
    sfp.boundarySensitivity = params.boundarySensitivity;
    sfp.gapTolerance = params.gapTolerance;
    sfp.areaScaling = std::max(0, params.rimProtectionPx);
    sfp.edgeQuality = 90;
    sfp.perceptualColor = true;
    sfp.alphaAware = true;
    sfp.antialiasing = true;
    sfp.colorBleeding = params.conservativeBoundary;
    sfp.contiguous = true;

    RegionResult res = computeSmartFill(refs, seedX, seedY, sfp, selection);
    if (res.status == RegionStatus::AmbiguousLeak && params.conservativeBoundary) {
        // Keep confidence explicitly low so UI/eraser reports partial/ambiguous contour,
        // while keeping detected protected pixels conservative rather than exposing interior.
        res.confidence = std::min(res.confidence, 0.45);
    }
    return res;
}

std::vector<Vec2> RegionEngine::snapMagneticPath(const ReferenceSet& refs,
                                                 Vec2 from, Vec2 to,
                                                 int magneticStrength)
{
    if (magneticStrength <= 0 || refs.docWidth <= 0 || refs.docHeight <= 0) {
        return {from, to};
    }

    const double dist = std::hypot(to.x - from.x, to.y - from.y);
    if (dist < 2.0) return {from, to};

    // Search inside a bounding corridor padded around segment [from -> to].
    const int pad = std::clamp(8 + magneticStrength / 6, 8, 28);
    const std::int64_t x0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(std::min(from.x, to.x))) - pad);
    const std::int64_t y0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(std::min(from.y, to.y))) - pad);
    const std::int64_t x1 = std::min<std::int64_t>(refs.docWidth, static_cast<std::int64_t>(std::ceil(std::max(from.x, to.x))) + pad + 1);
    const std::int64_t y1 = std::min<std::int64_t>(refs.docHeight, static_cast<std::int64_t>(std::ceil(std::max(from.y, to.y))) + pad + 1);
    const std::int64_t w = x1 - x0;
    const std::int64_t h = y1 - y0;
    if (w <= 1 || h <= 1 || w * h > 512 * 512) {
        return {from, to};
    }

    const std::int64_t sx = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::llround(from.x)), x0, x1 - 1) - x0;
    const std::int64_t sy = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::llround(from.y)), y0, y1 - 1) - y0;
    const std::int64_t gx = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::llround(to.x)), x0, x1 - 1) - x0;
    const std::int64_t gy = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::llround(to.y)), y0, y1 - 1) - y0;

    const std::size_t n = static_cast<std::size_t>(w * h);
    std::vector<float> bestCost(n, std::numeric_limits<float>::infinity());
    std::vector<std::int32_t> parent(n, -1);

    struct Node {
        float cost;
        std::int32_t idx;
        bool operator>(const Node& o) const noexcept { return cost > o.cost; }
    };
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;

    const std::int32_t startIdx = static_cast<std::int32_t>(sy * w + sx);
    const std::int32_t goalIdx = static_cast<std::int32_t>(gy * w + gx);
    bestCost[static_cast<std::size_t>(startIdx)] = 0.0f;
    pq.push({0.0f, startIdx});

    const double strength = static_cast<double>(std::clamp(magneticStrength, 0, 100)) / 100.0;
    const int dxs[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
    const int dys[8] = {0, 0, -1, 1, -1, -1, 1, 1};
    const float stepDist[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.414f, 1.414f, 1.414f, 1.414f};

    while (!pq.empty()) {
        const Node cur = pq.top();
        pq.pop();
        if (cur.cost > bestCost[static_cast<std::size_t>(cur.idx)]) continue;
        if (cur.idx == goalIdx) break;

        const std::int64_t cx = cur.idx % w;
        const std::int64_t cy = cur.idx / w;
        for (int k = 0; k < 8; ++k) {
            const std::int64_t nx = cx + dxs[k];
            const std::int64_t ny = cy + dys[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const std::int32_t nidx = static_cast<std::int32_t>(ny * w + nx);
            const double edge = static_cast<double>(m_cache.edgeAt(refs, x0 + nx, y0 + ny)) / 255.0;
            const float localCost = static_cast<float>(stepDist[k] / (1.0 + edge * strength * 10.0));
            const float nc = cur.cost + localCost;
            if (nc < bestCost[static_cast<std::size_t>(nidx)]) {
                bestCost[static_cast<std::size_t>(nidx)] = nc;
                parent[static_cast<std::size_t>(nidx)] = cur.idx;
                pq.push({nc, nidx});
            }
        }
    }

    if (parent[static_cast<std::size_t>(goalIdx)] < 0) {
        return {from, to};
    }

    std::vector<Vec2> rev;
    for (std::int32_t p = goalIdx; p >= 0; p = parent[static_cast<std::size_t>(p)]) {
        const std::int64_t px = x0 + (p % w);
        const std::int64_t py = y0 + (p / w);
        rev.push_back({static_cast<double>(px) + 0.5, static_cast<double>(py) + 0.5});
        if (p == startIdx) break;
    }
    std::reverse(rev.begin(), rev.end());
    return rev;
}

} // namespace komapaint::region
