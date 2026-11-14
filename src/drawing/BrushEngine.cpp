#include "BrushEngine.h"
#include <algorithm>
#include <cmath>

namespace komapaint {
namespace {
double clamp01(double n) { return std::clamp(n, 0.0, 1.0); }
unsigned scale(unsigned value, unsigned factor) { return (value * factor + 127U) / 255U; }
bool valid(InputSample p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.pressure); }
// Clip a segment to a rectangle using parametric intervals.
bool clipAxis(double position, double delta, double lo, double hi, double& start, double& end) {
    if (delta == 0) return position >= lo && position <= hi;
    double a = (lo - position) / delta, b = (hi - position) / delta;
    if (a > b) std::swap(a, b);
    start = std::max(start, a); end = std::min(end, b);
    return start <= end;
}
struct Batch { SparseTileStore& store; explicit Batch(SparseTileStore& s) : store(s) { s.beginBatch(); } ~Batch() { store.endBatch(); } };
}
double BrushRasterizer::sensorOpacity(double pressure) { return clamp01(pressure); }
double BrushRasterizer::tipCoverage(BrushTip tip, double dx, double dy, double radius, std::int64_t x, std::int64_t y, double hardness)
{
    // The edge fades over a band that is one pixel wide at full hardness (plain
    // anti-aliasing, unchanged from the hard tip) and the whole radius at zero.
    const double softness = 1.0 - clamp01(hardness);
    const double band = std::max(1.0, softness * (radius + 0.5));
    double coverage = clamp01((radius + 0.5 - std::hypot(dx, dy)) / band);
    // Soft tips ease in and out instead of forming a cone.
    if (softness > 0) coverage += softness * (coverage * coverage * (3.0 - 2.0 * coverage) - coverage);
    if (tip == BrushTip::Textured) {
        // Stable, deterministic grain anchored in document pixels.
        std::uint32_t n = static_cast<std::uint32_t>(x) * 0x8da6b343U ^ static_cast<std::uint32_t>(y) * 0xd8163841U;
        n ^= n >> 13; n *= 0x85ebca6bU; n ^= n >> 16;
        coverage *= 0.25 + 0.75 * (n & 255U) / 255.0;
    }
    return coverage;
}
std::uint32_t BrushRasterizer::deposit(std::uint32_t dst, std::uint32_t color, double amount)
{
    const auto alpha = static_cast<unsigned>(std::lround(clamp01(amount) * ((color >> 24) & 255U)));
    const auto inverse = 255U - alpha;
    auto channel = [&](unsigned shift) { return std::min(255U, scale((color >> shift) & 255U, alpha) + scale((dst >> shift) & 255U, inverse)); };
    const auto outAlpha = std::min(255U, alpha + scale((dst >> 24) & 255U, inverse));
    return (outAlpha << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}
std::uint32_t BrushRasterizer::erase(std::uint32_t dst, double amount)
{
    const auto keep = 255U - static_cast<unsigned>(std::lround(clamp01(amount) * 255.0));
    auto channel = [&](unsigned shift) { return scale((dst >> shift) & 255U, keep); };
    return (channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}
SparseTileStore::WriteResult BrushRasterizer::dab(SparseTileStore& store, const BrushDefinition& b, InputSample p)
{
    using R = SparseTileStore::WriteResult;
    if (!valid(p) || !std::isfinite(b.size) || !std::isfinite(b.opacity) || !std::isfinite(b.flow) || b.size < 1 || b.size > 256 || b.opacity < 0 || b.opacity > 1 || b.flow < 0 || b.flow > 1 || !std::isfinite(b.hardness) || b.hardness < 0 || b.hardness > 1) return R::InvalidPixel;
    const auto& g = store.geometry();
    if (!store.isValid()) return R::OutOfBounds;
    const double radius = b.size / 2;
    const double left = std::max(0.0, std::floor(p.x - radius - 1));
    const double top = std::max(0.0, std::floor(p.y - radius - 1));
    const double right = std::min(static_cast<double>(g.width()), std::ceil(p.x + radius + 1));
    const double bottom = std::min(static_cast<double>(g.height()), std::ceil(p.y + radius + 1));
    if (left >= right || top >= bottom) return R::Unchanged;
    Batch batch(store);
    bool changed = false;
    for (auto y = static_cast<std::int64_t>(top); y < static_cast<std::int64_t>(bottom); ++y) {
        for (auto x = static_cast<std::int64_t>(left); x < static_cast<std::int64_t>(right); ++x) {
            const double coverage = tipCoverage(b.tip, x + 0.5 - p.x, y + 0.5 - p.y, radius, x, y, b.hardness);
            if (coverage <= 0) continue;
            const auto old = store.pixel({x, y}).value();
            const double amount = coverage * b.opacity * b.flow * sensorOpacity(p.pressure);
            const auto next = b.erase ? erase(old, amount) : deposit(old, b.color, amount);
            auto result = store.setPixel({x, y}, next);
            if (result == R::Changed) changed = true;
            else if (result != R::Unchanged) return result; // partial dab remains visible; caller reports failure
        }
    }
    return changed ? R::Changed : R::Unchanged;
}
BrushStroke::BrushStroke(SparseTileStore& store, BrushDefinition brush) : m_store(store), m_brush(brush) {
    if (std::isfinite(brush.size) && std::isfinite(brush.spacing) && brush.size >= 1 && brush.size <= 256 && brush.spacing >= 0.05 && brush.spacing <= 0.5)
        m_spacing = std::max(0.25, brush.size * brush.spacing);
    else { m_failed = true; m_failureReason = SparseTileStore::WriteResult::InvalidPixel; }
}
bool BrushStroke::stamp(InputSample p) {
    auto result = BrushRasterizer::dab(m_store, m_brush, p);
    if (result != SparseTileStore::WriteResult::Changed && result != SparseTileStore::WriteResult::Unchanged) { m_failed = true; m_failureReason = result; }
    return !m_failed;
}
bool BrushStroke::begin(InputSample p) {
    if (m_started || m_failed || !valid(p)) {
        if (!m_failed) { m_failed = true; m_failureReason = SparseTileStore::WriteResult::InvalidPixel; }
        return false;
    }
    m_started = true; m_previous = p; m_remaining = m_spacing;
    return stamp(p);
}
bool BrushStroke::extend(InputSample p) {
    if (!m_started || m_failed || !valid(p)) {
        if (!m_failed) { m_failed = true; m_failureReason = SparseTileStore::WriteResult::InvalidPixel; }
        return false;
    }
    const double dx = p.x - m_previous.x, dy = p.y - m_previous.y;
    const double distance = std::hypot(dx, dy);
    if (!std::isfinite(distance)) { m_failed = true; m_failureReason = SparseTileStore::WriteResult::InvalidPixel; return false; }
    if (distance == 0) { m_previous = p; return true; }
    double start = 0, end = 1;
    const double radius = m_brush.size / 2 + 1;
    const auto& g = m_store.geometry();
    if (clipAxis(m_previous.x, dx, -radius, static_cast<double>(g.width()) + radius, start, end) &&
        clipAxis(m_previous.y, dy, -radius, static_cast<double>(g.height()) + radius, start, end)) {
        double at = m_remaining;
        if (at < start * distance) at += std::ceil((start * distance - at) / m_spacing) * m_spacing;
        for (; at <= end * distance && at <= distance; at += m_spacing) {
            const double t = at / distance;
            if (!stamp({m_previous.x + dx * t, m_previous.y + dy * t, m_previous.pressure + (p.pressure - m_previous.pressure) * t})) return false;
        }
    }
    const double remainder = std::fmod(distance - m_remaining, m_spacing);
    m_remaining = distance < m_remaining ? m_remaining - distance : (remainder == 0 ? m_spacing : m_spacing - remainder);
    m_previous = p;
    return true;
}
bool BrushStroke::end(InputSample p) {
    if (!extend(p)) return false;
    m_started = false;
    // Stamp the endpoint only if it was not already covered at the same location.
    if (m_remaining < m_spacing - 1e-9) return stamp(p);
    return true;
}
} // namespace komapaint
