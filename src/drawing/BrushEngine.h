#pragma once
#include "canvas/SparseTileStore.h"
#include <cstdint>

namespace komapaint {
namespace brush { struct BrushResource; }
// Digital brush tips: crisp/soft round, anchored grain texture, multi-tuft
// bristle, directional pencil/graphite, and organic stipple/scatter.
enum class BrushTip { Round, Textured, Bristle, Graphite, Stipple };

struct BrushDefinition {
    double size = 16;             // document pixels
    double opacity = 1;           // per dab; repeated dabs build up
    double flow = 1;
    double spacing = 0.15;        // fraction of diameter
    std::uint32_t color = 0xff000000; // straight ARGB
    BrushTip tip = BrushTip::Round;
    double hardness = 1;          // 1 = crisp edge; 0 = fades from the centre to the rim
    bool erase = false;           // remove paint from the layer instead of depositing colour

    // Tip geometry & directionality
    double aspectRatio = 1.0;     // 1.0 = circular; > 1.0 = flat/chisel ellipse
    double angleRadians = 0.0;    // base tip rotation
    bool followDirection = false; // align tip along stroke tangent

    // Pressure & stylus dynamics (defaults preserve fixed-size linear opacity behaviour)
    double pressureSize = 0.0;    // 0 = fixed size, 1 = full pressure->size modulation
    double pressureOpacity = 1.0; // 0 = fixed opacity, 1 = full pressure->opacity modulation
    double pressureFlow = 0.0;    // 0 = fixed flow, 1 = full pressure->flow modulation
    double pressureGamma = 1.0;   // < 1 lifts light pressure; > 1 requires firmer press
    double minSizeRatio = 0.15;   // minimum diameter fraction when pressureSize > 0
    double tiltSize = 0.0;        // 0 = fixed size, 1 = tilt broadens tip when device has tilt
    bool tiltAngle = false;       // orient/flatten tip along stylus tilt azimuth when supported
    bool stylusRotation = false;  // rotate tip by stylus barrel rotation when supported

    // Digital blending & smudging (0 = pure deposit; > 0 = blend/smudge existing pixels)
    double blendStrength = 0.0;   // 0..1 how strongly existing canvas paint blends/smears
    double colorMix = 1.0;        // 0 = pure smudge/blend, 1 = full foreground colour

    // Optional per-pixel mask callback (e.g. active SelectionMask or Boundary-Aware protection).
    double (*pixelMask)(void*, std::int64_t, std::int64_t) noexcept = nullptr;
    void* pixelMaskContext = nullptr;

    // Optional immutable source for a single dab, owned by the DocumentStroke.
    // The original procedural rasterizer remains the legacy/default route.
    const brush::BrushResource* bitmapTip = nullptr;
    bool bitmapColour = false;
    bool flipX = false;
    bool flipY = false;
    bool nativeAspect = false;
    double stampReach = 1.0;       // conservative outer footprint in base radii for stroke clipping
};

struct InputSample {
    double x = 0.0;
    double y = 0.0;
    double pressure = 1.0;
    double tiltX = 0.0;
    double tiltY = 0.0;
    double rotationRadians = 0.0;
    double tangentialPressure = 0.0;
    bool hasTilt = false;
    bool hasRotation = false;
};

// Stateless tip mask, sensor response and paint deposition helpers.
class BrushRasterizer {
public:
    static double sensorOpacity(double pressure);
    static double shapedPressure(double pressure, double gamma);
    static double tipCoverage(BrushTip tip, double dx, double dy, double radius,
                              std::int64_t x, std::int64_t y, double hardness = 1,
                              double aspectRatio = 1.0, double angleRadians = 0.0);
    static std::uint32_t deposit(std::uint32_t destination, std::uint32_t color, double amount);
    // Blends a premultiplied destination pixel toward a carried/mixed premultiplied sample.
    static std::uint32_t blend(std::uint32_t destination, std::uint32_t carryPremul, double amount);
    // Scales a premultiplied pixel towards transparent; amount 1 clears it.
    static std::uint32_t erase(std::uint32_t destination, double amount);
    static SparseTileStore::WriteResult dab(SparseTileStore& store, const BrushDefinition& brush,
                                            InputSample sample, double strokeAngle = 0.0,
                                            std::uint32_t carryPremul = 0, bool hasCarry = false);
    // Diameter the dab will actually occupy after pressure-to-size and tilt
    // broadening. The stroke loop uses the same value to advance its spacing,
    // so a shrinking dab also gets a proportionally shorter step.
    static double effectiveDiameter(const BrushDefinition& brush, const InputSample& sample) noexcept;
};

// Maintains spacing, directional angle, and smudge carry across input events.
// One instance is one stroke. Caller owns and keeps the store alive.
//
// The stamp sink is the seam between this loop and what a stamp consists of.
// The default sink is one dab of one tip, which is what every brush did before
// the document model existed. A document brush supplies a sink that paints
// several components per stamp, and it inherits this spacing, angle, clipping
// and carry logic instead of reimplementing it (see BrushDocumentAdapter).
class BrushStroke {
public:
    using StampSink = SparseTileStore::WriteResult (*)(
        SparseTileStore& store, const BrushDefinition& brush, InputSample sample,
        double strokeAngle, std::uint32_t carryPremul, bool hasCarry, void* context);

    BrushStroke(SparseTileStore& store, BrushDefinition definition);
    BrushStroke(SparseTileStore& store, BrushDefinition definition, StampSink sink,
                void* context);
    bool begin(InputSample sample);
    bool extend(InputSample sample);
    bool end(InputSample sample);
    bool failed() const { return m_failed; }
    SparseTileStore::WriteResult failureReason() const { return m_failureReason; }
private:
    bool stamp(InputSample sample);
    double spacingFor(const InputSample& sample) const;
    void updateCarry(InputSample sample);
    SparseTileStore& m_store;
    BrushDefinition m_brush;
    StampSink m_sink = nullptr;
    void* m_sinkContext = nullptr;
    InputSample m_previous{};
    double m_remaining = 0;
    double m_strokeAngle = 0;
    bool m_hasAngle = false;
    std::uint32_t m_carryPremul = 0;
    bool m_hasCarry = false;
    bool m_started = false;
    bool m_endpointStamped = false;
    bool m_moved = false;
    bool m_failed = false;
    SparseTileStore::WriteResult m_failureReason = SparseTileStore::WriteResult::Unchanged;
};
} // namespace komapaint
