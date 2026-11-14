#pragma once
#include "canvas/SparseTileStore.h"
#include <cstdint>

namespace komapaint {
// Value settings can later select other renderers, sensor curves and textures.
enum class BrushTip { Round, Textured };
struct BrushDefinition {
    double size = 16;             // document pixels
    double opacity = 1;           // per dab; repeated dabs build up
    double flow = 1;
    double spacing = 0.15;        // fraction of diameter
    std::uint32_t color = 0xff000000; // straight ARGB
    BrushTip tip = BrushTip::Round;
    double hardness = 1;          // 1 = crisp edge; 0 = fades from the centre to the rim
    bool erase = false;           // remove paint from the layer instead of depositing colour
};
struct InputSample { double x, y, pressure = 1; };

// Stateless tip mask, sensor response and paint deposition helpers.
class BrushRasterizer {
public:
    static double sensorOpacity(double pressure);
    static double tipCoverage(BrushTip tip, double dx, double dy, double radius, std::int64_t x, std::int64_t y, double hardness = 1);
    static std::uint32_t deposit(std::uint32_t destination, std::uint32_t color, double amount);
    // Scales a premultiplied pixel towards transparent; amount 1 clears it.
    static std::uint32_t erase(std::uint32_t destination, double amount);
    static SparseTileStore::WriteResult dab(SparseTileStore& store, const BrushDefinition& brush, InputSample sample);
};

// Maintains spacing across input events. One instance is one stroke. Caller owns
// and keeps the store alive, and enforces layer lock/visibility and input mapping.
class BrushStroke {
public:
    BrushStroke(SparseTileStore& store, BrushDefinition definition);
    bool begin(InputSample sample);
    bool extend(InputSample sample);
    bool end(InputSample sample);
    bool failed() const { return m_failed; }
    SparseTileStore::WriteResult failureReason() const { return m_failureReason; }
private:
    bool stamp(InputSample sample);
    SparseTileStore& m_store;
    BrushDefinition m_brush;
    InputSample m_previous{};
    double m_remaining = 0;
    double m_spacing = 1;
    bool m_started = false;
    bool m_failed = false;
    SparseTileStore::WriteResult m_failureReason = SparseTileStore::WriteResult::Unchanged;
};
} // namespace komapaint
