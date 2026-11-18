#include "brush/BrushParameters.h"

#include <algorithm>
#include <cmath>

namespace komapaint::brush {
namespace {

// The table is ordered by category so the settings panel can walk it in a
// stable, hand-authored order instead of sorting names alphabetically.
constexpr ParameterInfo kParameters[] = {
    // ---- size -------------------------------------------------------------
    {"size", "Size", ParameterScope::Brush, ParameterUnit::Pixels, 1, 8192, 16,
     "Diameter of the mark in document pixels at full pressure."},
    {"min-size-ratio", "Minimum", ParameterScope::Brush, ParameterUnit::Percent, 5, 100, 15,
     "Smallest diameter as a fraction of Size, reached at minimum pressure."},
    {"pressure-size", "Pressure to size", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How strongly pressure shrinks the tip."},
    {"tilt-size", "Tilt to size", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How strongly stylus tilt broadens the tip."},
    {"aspect", "Aspect", ParameterScope::Brush, ParameterUnit::Ratio, 25, 400, 100,
     "Tip width relative to height; 100 is circular, 400 is a chisel."},
    {"tip-angle", "Tip angle", ParameterScope::Brush, ParameterUnit::Degrees, -180, 180, 0,
     "Fixed rotation of the tip, in degrees."},
    {"hardness", "Hardness", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "Edge falloff of the tip: 100 is a crisp edge, 0 fades from the centre to the rim."},
    {"tilt-angle", "Tilt to angle", ParameterScope::Brush, ParameterUnit::Unit, 0, 1, 0,
     "Orient and flatten the tip along the stylus tilt direction when the device reports it."},

    // ---- opacity and flow --------------------------------------------------
    {"opacity", "Opacity", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "Coverage of a single dab before accumulation."},
    {"flow", "Flow", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "How much paint each dab deposits into the accumulation."},
    {"pressure-opacity", "Pressure to opacity", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "How strongly pressure reduces opacity."},
    {"pressure-flow", "Pressure to flow", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How strongly pressure reduces flow."},
    {"pressure-gamma", "Pressure curve", ParameterScope::Brush, ParameterUnit::Percent, 40, 200, 100,
     "Shape of the pressure response: below 100 lifts light pressure."},
    {"accumulate", "Accumulation", ParameterScope::Brush, ParameterUnit::Unit, 0, 1, 1,
     "Whether repeated dabs build up to full opacity."},

    // ---- stroke ------------------------------------------------------------
    {"spacing", "Spacing", ParameterScope::Brush, ParameterUnit::Percent, 5, 50, 15,
     "Distance between dabs as a fraction of the tip diameter."},
    {"spacing-jitter", "Spacing variation", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Random deviation applied to spacing, per dab."},
    {"taper-in", "Taper in", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 20,
     "Length of the opening taper as a fraction of the stroke."},
    {"taper-out", "Taper out", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 20,
     "Length of the closing taper as a fraction of the stroke."},
    {"follow-direction", "Follow direction", ParameterScope::Brush, ParameterUnit::Unit, 0, 1, 0,
     "Align the tip with the stroke tangent."},

    // ---- stabilization -----------------------------------------------------
    {"stabilization", "Stabilization", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Smoothing applied to the input path before it is stamped."},
    {"stroke-delay", "Stroke delay", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How far the painted line trails the cursor while drawing."},

    // ---- rotation ----------------------------------------------------------
    {"rotation-offset", "Rotation", ParameterScope::Brush, ParameterUnit::Degrees, -180, 180, 0,
     "Fixed rotation added to every stamp."},
    {"rotation-jitter", "Rotation variation", ParameterScope::Brush, ParameterUnit::Degrees, 0, 180, 0,
     "Random rotation spread applied to each stamp."},
    {"follow-rotation", "Follow pen rotation", ParameterScope::Brush, ParameterUnit::Unit, 0, 1, 0,
     "Use the stylus barrel rotation as the stamp angle."},

    // ---- scatter -----------------------------------------------------------
    {"scatter-x", "Scatter X", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Horizontal spread of stamps around the stroke, as a fraction of size."},
    {"scatter-y", "Scatter Y", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Vertical spread of stamps around the stroke, as a fraction of size."},
    {"scatter-count", "Stamps per dab", ParameterScope::Brush, ParameterUnit::Unit, 1, 64, 1,
     "How many stamps are placed at each spacing step."},
    {"cluster", "Clustering", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Pull scattered stamps into clusters rather than even noise."},

    // ---- texture -----------------------------------------------------------
    {"texture-strength", "Texture", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How strongly the texture component modulates coverage."},
    {"texture-scale", "Texture scale", ParameterScope::Brush, ParameterUnit::Percent, 1, 800, 100,
     "Size of the texture relative to the tip."},
    {"texture-rotation", "Texture rotation", ParameterScope::Brush, ParameterUnit::Degrees, -180, 180, 0,
     "Rotation of the texture within the tip."},
    {"texture-offset-x", "Texture offset X", ParameterScope::Brush, ParameterUnit::Percent, -100, 100, 0,
     "Horizontal shift of the texture within the tip."},
    {"texture-offset-y", "Texture offset Y", ParameterScope::Brush, ParameterUnit::Percent, -100, 100, 0,
     "Vertical shift of the texture within the tip."},
    {"texture-contrast", "Texture contrast", ParameterScope::Brush, ParameterUnit::Percent, 0, 200, 100,
     "Contrast applied to the texture before it modulates the mark."},
    {"texture-depth", "Texture depth", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "Whether texture cuts the mark (0) or only tints its edges (100)."},

    // ---- colour ------------------------------------------------------------
    {"hue-variation", "Hue variation", ParameterScope::Brush, ParameterUnit::Degrees, -180, 180, 0,
     "Random hue rotation applied per stroke or per stamp."},
    {"saturation-variation", "Saturation variation", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Random saturation change applied per stroke or per stamp."},
    {"brightness-variation", "Brightness variation", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Random value change applied per stroke or per stamp."},
    {"color-mix", "Colour mix", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "Blend between picked-up canvas colour and the foreground colour."},

    // ---- material (Realistic engine) ---------------------------------------
    {"paint-load", "Paint load", ParameterScope::Brush, ParameterUnit::Percent, 10, 100, 88,
     "How much paint the reservoir starts a stroke with."},
    {"pigment-concentration", "Pigment concentration", ParameterScope::Brush, ParameterUnit::Percent, 10, 100, 82,
     "Pigment mass relative to the batch carried by the brush."},
    {"water-amount", "Water", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 22,
     "Water in the reservoir; drives spreading and translucency."},
    {"paint-body", "Paint body", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 78,
     "Thickness of the paint: high body holds its shape, low body levels out."},
    {"pickup", "Pickup", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 16,
     "How much existing paint the brush lifts as it passes."},
    {"depletion", "Depletion", ParameterScope::Brush, ParameterUnit::Percent, 0, 200, 100,
     "Rate at which the reservoir empties along a stroke."},
    {"wetness", "Wetness", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "Surface wetness the stroke paints onto; affects bleeding and blending."},
    {"drying-speed", "Drying", ParameterScope::Brush, ParameterUnit::Percent, 25, 200, 115,
     "How quickly deposited paint stops being workable."},
    {"tooth-response", "Tooth", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 18,
     "How much the paper surface breaks up the mark."},
    {"granulation", "Granulation", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 5,
     "Pigment settling into the tooth of the surface."},
    {"edge-accumulation", "Edge accumulation", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 8,
     "Paint gathering at the rim of a drying stroke."},
    {"pressure-amount", "Pressure to load", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 62,
     "How strongly pressure changes how much paint is released."},
    {"pressure-contact", "Pressure to contact", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 35,
     "How strongly pressure changes how much of the tip touches the surface."},
    {"persistent-reservoir", "Keep the reservoir", ParameterScope::Brush, ParameterUnit::Unit, 0, 1, 0,
     "Keep the paint left on the brush between strokes instead of reloading each time."},
    {"bristle-count", "Bristle count", ParameterScope::Brush, ParameterUnit::Unit, 0, 512, 0,
     "Bristles in the tip; zero means a continuous tip rather than a bundle."},
    {"bristle-spread", "Bristle spread", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How far apart the bristles splay when pressure is applied."},

    // ---- dynamics / distribution ------------------------------------------
    {"particle-count", "Particles", ParameterScope::Brush, ParameterUnit::Unit, 1, 256, 3,
     "Particles emitted per spacing step by a particle generator."},
    {"particle-spread", "Particle spread", ParameterScope::Brush, ParameterUnit::Percent, 0, 400, 100,
     "Radius particles are emitted into, as a fraction of size."},
    {"sequence-step", "Sequence step", ParameterScope::Brush, ParameterUnit::Unit, 1, 999, 1,
     "How many stamps advance a sequence generator by one frame."},

    // ---- rendering ---------------------------------------------------------
    {"antialias", "Antialiasing", ParameterScope::Brush, ParameterUnit::Unit, 0, 1, 1,
     "Feather dab edges; off gives deliberately hard, aliased stamps."},
    {"blend-strength", "Blending", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Digital engine: how strongly existing canvas paint is smeared."},

    // ---- component scope ---------------------------------------------------
    {"component-opacity", "Opacity", ParameterScope::Component, ParameterUnit::Percent, 0, 100, 100,
     "How much this component contributes to the mark."},
    {"component-scale", "Scale", ParameterScope::Component, ParameterUnit::Percent, 5, 800, 100,
     "Size of this component relative to the tip."},
    {"component-rotation", "Rotation", ParameterScope::Component, ParameterUnit::Degrees, -180, 180, 0,
     "Additional rotation applied to this component."},
    {"component-aspect", "Aspect", ParameterScope::Component, ParameterUnit::Ratio, 25, 400, 100,
     "Width relative to height for this component."},
    {"component-offset-x", "Offset X", ParameterScope::Component, ParameterUnit::Percent, -200, 200, 0,
     "Horizontal placement of this component within the tip."},
    {"component-offset-y", "Offset Y", ParameterScope::Component, ParameterUnit::Percent, -200, 200, 0,
     "Vertical placement of this component within the tip."},
    {"component-weight", "Weight", ParameterScope::Component, ParameterUnit::Unit, 0, 1000, 1,
     "Relative probability of this component being chosen by a weighted group."},
    // Transitional: the production engines carry five built-in procedural tip
    // masks. A brush migrated from the flat record selects one of them here
    // until Brush Studio authors real tip resources (Stage 3).
    // 0 round, 1 textured, 2 bristle, 3 graphite, 4 stipple.
    {"tip-shape", "Tip shape", ParameterScope::Component, ParameterUnit::Unit, 0, 4, 0,
     "Built-in procedural tip mask: 0 round, 1 textured, 2 bristle, 3 graphite, 4 stipple."},
};

} // namespace

std::span<const ParameterInfo> parameterRegistry() {
    return std::span<const ParameterInfo>(kParameters, std::size(kParameters));
}

const ParameterInfo* findParameter(std::string_view name) {
    for (const auto& info : kParameters) {
        if (name == info.name) return &info;
    }
    return nullptr;
}

bool parameterInRange(const ParameterInfo& info, double value) {
    return std::isfinite(value) && value >= info.minimum && value <= info.maximum;
}

} // namespace komapaint::brush
