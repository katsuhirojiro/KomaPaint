#include "CpuTileCompositor.h"
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
    const unsigned a = static_cast<unsigned>(std::lround(outAlpha * 255.0));
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
        return std::min(a, static_cast<unsigned>(std::lround(std::clamp(result, 0.0, 1.0) * 255.0)));
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
std::optional<CompositeTile> CpuTileCompositor::compose(const TileGeometry& geometry, TilePoint tile,
        const std::vector<CompositeLayer>& layers, std::uint32_t background)
{
    auto bounds = geometry.tileBounds(tile);
    if (!bounds || !validPixel(background) || geometry.tileSize() > 4096) return std::nullopt;
    for (const auto& layer : layers) {
        if (!layer.store || !layer.store->isValid() || !std::isfinite(layer.opacity) || layer.opacity < 0 || layer.opacity > 1 || !validBlendMode(layer.blendMode))
            return std::nullopt;
        const auto& other = layer.store->geometry();
        if (other.width() != geometry.width() || other.height() != geometry.height() || other.tileSize() != geometry.tileSize())
            return std::nullopt;
    }
    try {
        CompositeTile result;
        result.width = bounds->right - bounds->left;
        result.height = bounds->bottom - bounds->top;
        const auto width = static_cast<std::size_t>(result.width);
        const auto height = static_cast<std::size_t>(result.height);
        result.pixels.assign(width * height, background);
        for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
            if (!it->visible || it->opacity == 0.0) continue;
            const auto pixels = it->store->tilePixels(tile);
            if (pixels.empty()) continue;
            const auto opacity = static_cast<unsigned>(std::lround(it->opacity * 255.0));
            const auto stride = static_cast<std::size_t>(geometry.tileSize());
            for (std::size_t y = 0; y < height; ++y)
                for (std::size_t x = 0; x < width; ++x)
                    result.pixels[y * width + x] = blendOver(pixels[y * stride + x], result.pixels[y * width + x], opacity, it->blendMode);
        }
        return result;
    } catch (const std::bad_alloc&) { return std::nullopt; }
      catch (const std::length_error&) { return std::nullopt; }
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
