#include "CpuTileCompositor.h"
#include "core/FastRounding.h"
#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>

namespace komapaint {
namespace {
bool validPixel(std::uint32_t p) {
    const auto a = p >> 24;
    return ((p >> 16) & 255U) <= a && ((p >> 8) & 255U) <= a && (p & 255U) <= a;
}
unsigned scale(unsigned value, unsigned factor) { return (value * factor + 127U) / 255U; }
std::uint32_t over(std::uint32_t src, std::uint32_t dst, unsigned opacity) {
    const unsigned sa = scale((src >> 24) & 255U, opacity);
    const unsigned inverse = 255U - sa;
    auto channel = [&](unsigned shift) {
        return std::min(255U, scale((src >> shift) & 255U, opacity) + scale((dst >> shift) & 255U, inverse));
    };
    return (channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}
// W3C source-over blend compositing, in premultiplied form:
// co = Cs*as*(1-ab) + Cb*ab*(1-as) + B(Cb,Cs)*as*ab.
// The existing integer Normal path is retained to avoid altering old projects.
std::uint32_t blendOver(std::uint32_t src, std::uint32_t dst, unsigned opacity, BlendMode mode) {
    if (mode == BlendMode::Normal) return over(src, dst, opacity);
    const double sourceAlpha = double((src >> 24) & 255U) / 255.0;
    const double backdropAlpha = double((dst >> 24) & 255U) / 255.0;
    const double effectiveAlpha = sourceAlpha * double(opacity) / 255.0;
    const double outAlpha = effectiveAlpha + backdropAlpha * (1.0 - effectiveAlpha);
    const unsigned a = roundHalfUpNonNegative(outAlpha * 255.0);
    auto channel = [&](unsigned shift) {
        const double sourcePremul = double((src >> shift) & 255U) / 255.0;
        const double backdropPremul = double((dst >> shift) & 255U) / 255.0;
        const double cs = sourceAlpha > 0 ? sourcePremul / sourceAlpha : 0;
        const double cb = backdropAlpha > 0 ? backdropPremul / backdropAlpha : 0;
        double blended = 0;
        switch (mode) {
        case BlendMode::Multiply: blended = cb * cs; break;
        case BlendMode::Screen: blended = cb + cs - cb * cs; break;
        case BlendMode::Overlay: blended = cb <= 0.5 ? 2 * cb * cs : 1 - 2 * (1 - cb) * (1 - cs); break;
        case BlendMode::Normal: break; // handled above
        }
        const double result = cs * effectiveAlpha * (1 - backdropAlpha)
                            + backdropPremul * (1 - effectiveAlpha)
                            + blended * effectiveAlpha * backdropAlpha;
        return std::min(a, roundHalfUpNonNegative(std::clamp(result, 0.0, 1.0) * 255.0));
    };
    return (a << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}
bool validBlendMode(BlendMode mode) {
    switch (mode) {
    case BlendMode::Normal: case BlendMode::Multiply: case BlendMode::Screen: case BlendMode::Overlay: return true;
    }
    return false;
}
bool same(const std::vector<CompositeLayer>& a, std::uint32_t ab,
          const std::vector<CompositeLayer>& b, std::uint32_t bb) { return ab == bb && a == b; }
}
bool CpuTileCompositor::composeInto(const TileGeometry& geometry, TilePoint tile,
        const std::vector<CompositeLayer>& layers, std::uint32_t background,
        std::span<const std::span<const std::uint32_t>> layerTileOverrides,
        std::vector<std::uint32_t>& outPixels, std::int64_t& outWidth, std::int64_t& outHeight)
{
    auto bounds = geometry.tileBounds(tile);
    if (!bounds || !validPixel(background) || geometry.tileSize() > 4096) return false;
    if (!layerTileOverrides.empty() && layerTileOverrides.size() != layers.size()) return false;
    const auto stride = static_cast<std::size_t>(geometry.tileSize());
    const std::size_t tilePixels = stride * stride;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const auto& layer = layers[i];
        if (!layer.store || !layer.store->isValid() || !std::isfinite(layer.opacity) || layer.opacity < 0 || layer.opacity > 1 || !validBlendMode(layer.blendMode))
            return false;
        const auto& other = layer.store->geometry();
        if (other.width() != geometry.width() || other.height() != geometry.height() || other.tileSize() != geometry.tileSize())
            return false;
        if (!layerTileOverrides.empty() && !layerTileOverrides[i].empty()
            && layerTileOverrides[i].size() != tilePixels)
            return false;
    }
    try {
        outWidth = bounds->right - bounds->left;
        outHeight = bounds->bottom - bounds->top;
        const auto width = static_cast<std::size_t>(outWidth);
        const auto height = static_cast<std::size_t>(outHeight);
        outPixels.assign(width * height, background);
        std::uint32_t* dstBase = outPixels.data();

        for (std::size_t rev = layers.size(); rev > 0; --rev) {
            const std::size_t idx = rev - 1;
            const auto& layer = layers[idx];
            if (!layer.visible || layer.opacity == 0.0) continue;
            std::span<const std::uint32_t> pixels;
            if (!layerTileOverrides.empty() && !layerTileOverrides[idx].empty()) {
                pixels = layerTileOverrides[idx];
            } else {
                pixels = layer.store->tilePixels(tile);
            }
            if (pixels.empty()) continue;
            const auto opacity = roundHalfUpNonNegative(layer.opacity * 255.0);
            if (opacity == 0U) continue;
            const std::uint32_t* srcBase = pixels.data();

            if (layer.blendMode == BlendMode::Normal) {
                if (opacity == 255U) {
                    for (std::size_t y = 0; y < height; ++y) {
                        const std::uint32_t* srcRow = srcBase + y * stride;
                        std::uint32_t* dstRow = dstBase + y * width;
                        for (std::size_t x = 0; x < width; ++x) {
                            const std::uint32_t src = srcRow[x];
                            if (src == 0U) continue;
                            const unsigned sa = src >> 24;
                            if (sa == 255U) {
                                dstRow[x] = src;
                            } else {
                                const std::uint32_t dst = dstRow[x];
                                const unsigned inv = 255U - sa;
                                const unsigned a = std::min(255U, sa + scale(dst >> 24, inv));
                                const unsigned r = std::min(255U, ((src >> 16) & 255U) + scale((dst >> 16) & 255U, inv));
                                const unsigned g = std::min(255U, ((src >> 8) & 255U) + scale((dst >> 8) & 255U, inv));
                                const unsigned b = std::min(255U, (src & 255U) + scale(dst & 255U, inv));
                                dstRow[x] = (a << 24) | (r << 16) | (g << 8) | b;
                            }
                        }
                    }
                } else {
                    for (std::size_t y = 0; y < height; ++y) {
                        const std::uint32_t* srcRow = srcBase + y * stride;
                        std::uint32_t* dstRow = dstBase + y * width;
                        for (std::size_t x = 0; x < width; ++x) {
                            const std::uint32_t src = srcRow[x];
                            if (src == 0U) continue;
                            dstRow[x] = over(src, dstRow[x], opacity);
                        }
                    }
                }
            } else {
                const BlendMode mode = layer.blendMode;
                for (std::size_t y = 0; y < height; ++y) {
                    const std::uint32_t* srcRow = srcBase + y * stride;
                    std::uint32_t* dstRow = dstBase + y * width;
                    for (std::size_t x = 0; x < width; ++x) {
                        const std::uint32_t src = srcRow[x];
                        if (src == 0U) continue;
                        const std::uint32_t dst = dstRow[x];
                        if (dst == 0U) {
                            dstRow[x] = (opacity == 255U) ? src : over(src, 0U, opacity);
                        } else {
                            dstRow[x] = blendOver(src, dst, opacity, mode);
                        }
                    }
                }
            }
        }
        return true;
    } catch (const std::bad_alloc&) { return false; }
      catch (const std::length_error&) { return false; }
}

std::optional<CompositeTile> CpuTileCompositor::compose(const TileGeometry& geometry, TilePoint tile,
        const std::vector<CompositeLayer>& layers, std::uint32_t background)
{
    CompositeTile result;
    if (!composeInto(geometry, tile, layers, background, {}, result.pixels, result.width, result.height))
        return std::nullopt;
    return result;
}
CompositeInvalidation::Plan CompositeInvalidation::prepare(const std::vector<CompositeLayer>& layers, std::uint32_t background) const
{
    Plan plan;
    plan.layers = layers;
    plan.background = background;
    plan.invalidateAll = !m_hasConfiguration || !same(m_layers, m_background, layers, background);
    for (const auto& layer : layers) {
        if (!layer.store) continue;
        plan.tickets.push_back({layer.store, layer.store->dirtyVersion()});
        for (const auto& [tile, rect] : layer.store->dirtyRegions()) {
            (void)rect;
            plan.tiles.insert(tile);
        }
    }
    return plan;
}
bool CompositeInvalidation::accept(const Plan& plan, const std::vector<CompositeLayer>& current, std::uint32_t background)
{
    if (!same(plan.layers, plan.background, current, background)) return false;
    m_layers = plan.layers;
    m_background = background;
    m_hasConfiguration = true;
    return true;
}
bool CompositeInvalidation::acknowledge(const Plan& plan, TilePoint tile, const std::vector<CompositeLayer>& current, std::uint32_t background)
{
    if (!m_hasConfiguration || !same(plan.layers, plan.background, current, background) ||
        !same(m_layers, m_background, current, background) || plan.tiles.find(tile) == plan.tiles.end()) return false;
    for (const auto& ticket : plan.tickets)
        if (ticket.store->dirtyVersion() != ticket.version) return false;
    for (const auto& ticket : plan.tickets) ticket.store->acknowledgeDirty(tile, ticket.version);
    return true;
}
} // namespace komapaint
