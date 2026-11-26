#include "brush/BrushParameters.h"

#include <algorithm>
#include <cmath>

namespace komapaint::brush {
namespace {

// The table is ordered by category so the settings panel can walk it in a
// stable, hand-authored order instead of sorting names alphabetically.
constexpr ParameterInfo kParameters[] = {
    // ---- size -------------------------------------------------------------
    {"size", "Size", ParameterScope::Brush, ParameterUnit::Pixels, MinBrushDiameter, MaxBrushDiameter, 16,
     "Overall width of the mark at full pressure. Raise it for broad washes and lower it for fine detail work."},
    {"min-size-ratio", "Minimum", ParameterScope::Brush, ParameterUnit::Percent, 5, 100, 15,
     "How small the brush becomes at the lightest pressure. Keep it high for a controlled taper and low for dramatic thin-to-thick strokes."},
    {"pressure-size", "Pressure to size", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How much pen pressure changes the width. Increase it for expressive line weight and leave it at zero for a constant-width marker."},
    {"tilt-size", "Tilt to size", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How much tilting the stylus widens the mark. Use it for the broad side of a chisel or a dry brush that opens as you lean the pen."},
    {"aspect", "Tip Shape", ParameterScope::Brush, ParameterUnit::Ratio, 25, 400, 100,
     "How wide the tip is compared with its height. Keep it near 100 for a round brush and raise it for a flat chisel or calligraphy nib."},
    {"tip-angle", "Tip angle", ParameterScope::Brush, ParameterUnit::Degrees, -180, 180, 0,
     "The fixed direction the tip points. Turn it to change the angle of a chisel edge without rotating your hand."},
    {"hardness", "Hardness", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "How crisp the edge of the mark is. Lower it for airbrush and soft blending, keep it high for ink and hard pencils."},
    {"tilt-angle", "Tilt to angle", ParameterScope::Brush, ParameterUnit::Unit, 0, 1, 0,
     "Turns the tip to follow the direction the stylus leans. Use it when a flat brush should open along the tilt of your hand."},

    // ---- opacity and flow --------------------------------------------------
    {"opacity", "Opacity", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "How solid each dab is before it builds up. Lower it for glazes and layered colour; keep it high for opaque ink."},
    {"flow", "Flow", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "How much paint each dab releases into the stroke. Reduce it for a dry, gradual build and raise it for a loaded brush."},
    {"pressure-opacity", "Pressure to opacity", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "How much pressure fades the paint. Raise it for light, airy pressure strokes and lower it when every touch should be solid."},
    {"pressure-flow", "Pressure to flow", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How much pressure changes the paint released. Use it for thin, skippy light marks that load up as you press."},
    {"pressure-gamma", "Pressure curve", ParameterScope::Brush, ParameterUnit::Percent, 40, 200, 100,
     "Shapes the pressure response between light and firm touch. Lower values respond sooner; higher values need a firmer press before paint appears."},
    {"accumulate", "Accumulation", ParameterScope::Brush, ParameterUnit::Unit, 0, 1, 1,
     "Whether overlapping dabs keep building toward full opacity. Turn it off for a stroke that stays at the chosen opacity no matter how many dabs overlap."},

    // ---- stroke ------------------------------------------------------------
    {"spacing", "Spacing", ParameterScope::Brush, ParameterUnit::Percent, 5, 50, 15,
     "The distance between individual brush marks as you draw. Small values make a smooth line; large values reveal the individual marks."},
    {"spacing-jitter", "Spacing variation", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Adds controlled irregularity to the distance between marks. Use it for a hand-made, uneven trail rather than a mechanical line."},
    {"taper-in", "Taper in", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How far the stroke takes to reach full width at its start, as a share of the brush size. Use a long taper for a natural pen entry and a short one for a decisive start."},
    {"taper-out", "Taper out", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How far the stroke fades out at its end, as a share of the brush size. A long taper gives a lifted pen finish; a short one leaves a blunt stop."},
    {"follow-direction", "Follow direction", ParameterScope::Brush, ParameterUnit::Unit, 0, 1, 0,
     "Turns the tip to line up with the direction you are drawing. Use it for chisel nibs and brushes whose flat edge should follow the stroke."},

    // ---- stabilization -----------------------------------------------------
    {"stabilization", "Stabilization", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Smooths small hand movements while you draw. Raise it for long clean curves and lower it when you need quick, responsive detail."},
    {"stroke-delay", "Stroke delay", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Lets the painted line trail slightly behind the pointer. A small delay can steady a stroke; too much feels disconnected."},

    // ---- rotation ----------------------------------------------------------
    {"rotation-offset", "Rotation", ParameterScope::Brush, ParameterUnit::Degrees, -180, 180, 0,
     "Turns every mark by a fixed angle. Use it to set the resting angle of a chisel or a textured stamp."},
    {"rotation-jitter", "Rotation variation", ParameterScope::Brush, ParameterUnit::Degrees, 0, 180, 0,
     "Adds random rotation from mark to mark. Use it to break up a repeated leaf, stone or bristle stamp."},
    {"follow-rotation", "Follow pen rotation", ParameterScope::Brush, ParameterUnit::Unit, 0, 1, 0,
     "Uses the barrel rotation of a supported stylus as the mark angle. Turn it on when you want the brush to rotate with your pen hand."},

    // ---- scatter -----------------------------------------------------------
    {"scatter-x", "Scatter X", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Spreads marks horizontally around your stroke. Use it for loose foliage, smoke and spray rather than a tight line."},
    {"scatter-y", "Scatter Y", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Spreads marks vertically around your stroke. Combine it with horizontal scatter to make a loose cloud of marks."},
    {"scatter-count", "Stamps per dab", ParameterScope::Brush, ParameterUnit::Unit, 1, 64, 1,
     "How many separate marks are placed at each spacing step. Raise it for denser foliage or spray; keep it low for a readable single mark."},
    {"cluster", "Clustering", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Pulls scattered marks into groups instead of an even spread. Use it for clumps of leaves, clouds and natural debris."},

    // ---- texture -----------------------------------------------------------
    {"texture-strength", "Texture", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How strongly an attached texture or grain mask breaks up the mark. Use a little for paper tooth and a lot for a dry, broken brush."},
    {"texture-scale", "Texture scale", ParameterScope::Brush, ParameterUnit::Percent, 1, 800, 100,
     "How large the texture appears inside the mark. Increase it for broad grain and decrease it for fine paper tooth."},
    {"texture-rotation", "Texture rotation", ParameterScope::Brush, ParameterUnit::Degrees, -180, 180, 0,
     "Turns the texture inside the mark. Use it to align grain with the direction of the surface or to break up repetition."},
    {"texture-offset-x", "Texture offset X", ParameterScope::Brush, ParameterUnit::Percent, -100, 100, 0,
     "Slides the texture sideways inside the mark. Use it to line up a grain pattern without moving the brush."},
    {"texture-offset-y", "Texture offset Y", ParameterScope::Brush, ParameterUnit::Percent, -100, 100, 0,
     "Slides the texture up or down inside the mark. Use it to place a grain or pattern deliberately within the tip."},
    {"texture-contrast", "Texture contrast", ParameterScope::Brush, ParameterUnit::Percent, 0, 200, 100,
     "Makes the texture gaps harder or softer. Raise it for a crisp dry-brush breakup and lower it for a subtle tooth."},
    {"texture-depth", "Texture depth", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "How fully dark parts of the texture remove paint. Lower it for a gentle grain that only tints the surface and raise it for a broken dry-media mark."},

    // ---- colour ------------------------------------------------------------
    {"hue-variation", "Hue variation", ParameterScope::Brush, ParameterUnit::Degrees, -180, 180, 0,
     "Shifts colour from mark to mark. Use a small amount to keep foliage or skin alive, and zero for flat graphic colour."},
    {"saturation-variation", "Saturation variation", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Changes how saturated each mark is. Use it to mix muted and vivid patches in natural textures."},
    {"brightness-variation", "Brightness variation", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "Changes how light or dark each mark is. Use it to add depth without changing hue."},
    {"color-mix", "Colour mix", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "How much the brush picks up colour already on the canvas. Raise it for smudging and blending, lower it for clean new paint."},

    // ---- material (Realistic engine) ---------------------------------------
    {"paint-load", "Paint load", ParameterScope::Brush, ParameterUnit::Percent, 10, 100, 88,
     "How much paint the brush carries at the start of a stroke. More load gives a longer wet stroke before the brush runs dry."},
    {"pigment-concentration", "Pigment concentration", ParameterScope::Brush, ParameterUnit::Percent, 10, 100, 82,
     "How much pigment is in the carried paint. Raise it for strong staining colour and lower it for watery washes."},
    {"water-amount", "Water", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 22,
     "How much water the paint carries. More water spreads and softens edges; less water keeps the mark in place."},
    {"paint-body", "Paint body", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 78,
     "How thick the paint feels. High body holds ridges and brush marks; low body settles into a flat wash."},
    {"pickup", "Pickup", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 16,
     "How much existing paint the brush lifts as it passes. Use it for wet-in-wet mixing and dirty-brush effects."},
    {"depletion", "Depletion", ParameterScope::Brush, ParameterUnit::Percent, 0, 200, 100,
     "How quickly the brush runs out of paint along a stroke. Raise it for a dry, fading stroke; lower it for a long loaded line."},
    {"wetness", "Wetness", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 100,
     "How wet the surface is under the stroke. Higher wetness lets paint bleed and blend after the brush has passed."},
    {"drying-speed", "Drying", ParameterScope::Brush, ParameterUnit::Percent, 25, 200, 115,
     "How quickly deposited paint stops moving. Faster drying holds edges; slower drying leaves more time for bleeding and lifting."},
    {"tooth-response", "Tooth", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 18,
     "How much the paper surface breaks up the paint. Use it on rough paper for a grainy, broken mark and lower it on smooth surfaces."},
    {"granulation", "Granulation", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 5,
     "How much pigment settles into the paper texture. Raise it for watercolour effects where colour gathers in the valleys."},
    {"edge-accumulation", "Edge accumulation", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 8,
     "How much paint gathers at the rim of a drying wash. Use it for the darker edge of a watercolour stroke."},
    {"pressure-amount", "Pressure to load", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 62,
     "How much pressure changes the amount of paint released. Raise it for a loaded press and a thin light touch."},
    {"pressure-contact", "Pressure to contact", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 35,
     "How much of the tip touches the surface at low pressure. Raise it for a broad contact even when you press lightly."},
    {"persistent-reservoir", "Keep the reservoir", ParameterScope::Brush, ParameterUnit::Unit, 0, 1, 0,
     "Keeps the paint left on the brush between strokes. Turn it on when you want a continuous loaded brush instead of a fresh reservoir each time."},
    {"bristle-count", "Bristle count", ParameterScope::Brush, ParameterUnit::Unit, 0, 512, 0,
     "How many bristles make up the tip. Zero gives a smooth continuous tip; higher counts make a separated bundle."},
    {"bristle-spread", "Bristle spread", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How far the bristles splay under pressure. Use it for a fan brush or a dry, separated bristle mark."},

    // ---- dynamics / distribution ------------------------------------------
    {"particle-count", "Particles", ParameterScope::Brush, ParameterUnit::Unit, 1, 256, 3,
     "How many particles a particle generator emits at each step. Raise it for dense spray and lower it for sparse droplets."},
    {"particle-spread", "Particle spread", ParameterScope::Brush, ParameterUnit::Percent, 0, 400, 100,
     "How far particles scatter from the stroke. Use it for a wide mist or a tight spatter."},
    {"sequence-step", "Sequence step", ParameterScope::Brush, ParameterUnit::Unit, 1, 999, 1,
     "How many stamps pass before an image sequence advances a frame. Use it to control the speed of an animated stamp."},

    // ---- rendering ---------------------------------------------------------
    {"antialias", "Antialiasing", ParameterScope::Brush, ParameterUnit::Unit, 0, 1, 1,
     "Softens the edge of each dab. Leave it on for smooth marks; turn it off for deliberately hard, pixel-sharp stamps."},
    {"blend-strength", "Blending", ParameterScope::Brush, ParameterUnit::Percent, 0, 100, 0,
     "How strongly the brush smears paint already on the layer. Raise it for a smudge tool and keep it at zero for clean deposits."},

    // ---- component scope ---------------------------------------------------
    {"component-opacity", "Opacity", ParameterScope::Component, ParameterUnit::Percent, 0, 100, 100,
     "How much this mark contributes to the combined brush. Lower it for a faint underlayer or a subtle supporting mark."},
    {"component-scale", "Scale", ParameterScope::Component, ParameterUnit::Percent, 5, 800, 100,
     "How large this mark is compared with the brush tip. Use it to make a leaf larger than the tip or a detail smaller."},
    {"component-rotation", "Rotation", ParameterScope::Component, ParameterUnit::Degrees, -180, 180, 0,
     "Turns this mark independently of the others. Use it to point leaves, hairs or stamps in a deliberate direction."},
    {"component-aspect", "Tip Shape", ParameterScope::Component, ParameterUnit::Ratio, 25, 400, 100,
     "How wide this mark is compared with its height. Keep it round for spots and stretch it for hair, grass or chisel marks."},
    {"component-offset-x", "Offset X", ParameterScope::Component, ParameterUnit::Percent, -200, 200, 0,
     "Moves this mark sideways inside the brush. Use it to build a cluster of marks around the centre."},
    {"component-offset-y", "Offset Y", ParameterScope::Component, ParameterUnit::Percent, -200, 200, 0,
     "Moves this mark up or down inside the brush. Use it to arrange marks into a branch, a tuft or a ring."},
    {"component-weight", "Weight", ParameterScope::Component, ParameterUnit::Unit, 0, 1000, 1,
     "How often this mark is chosen by a weighted group. Raise it for a common leaf and lower it for an occasional variation."},
    // Transitional: the production engines carry five built-in procedural tip
    // masks. A brush migrated from the flat record selects one of them here
    // until Brush Studio authors real tip resources (Stage 3).
    // 0 round, 1 textured, 2 bristle, 3 graphite, 4 stipple.
    {"tip-shape", "Built-in tip", ParameterScope::Component, ParameterUnit::Unit, 0, 4, 0,
     "The built-in procedural shape used by this tip. Choose round for a clean mark, or textured, bristle, graphite and stipple for ready-made grain."},
};

} // namespace

namespace {

// Where an artist finds each setting. Ordered like the registry; the pairing is
// checked by TestBrushParameters so a new parameter cannot arrive without a
// home.
struct SectionEntry {
    const char* name;
    const char* section;
};

constexpr const char* kSections[] = {
    "Brush Tip", "Size", "Opacity & Flow", "Stroke", "Stabilization", "Rotation",
    "Scatter", "Texture", "Color", "Material & Blending", "Dynamics", "Rendering",
    "Advanced",
};

constexpr SectionEntry kSectionOf[] = {
    {"size", "Size"},
    {"min-size-ratio", "Size"},
    {"pressure-size", "Dynamics"},
    {"tilt-size", "Dynamics"},
    {"aspect", "Brush Tip"},
    {"tip-angle", "Brush Tip"},
    {"hardness", "Brush Tip"},
    {"tilt-angle", "Brush Tip"},
    {"opacity", "Opacity & Flow"},
    {"flow", "Opacity & Flow"},
    {"pressure-opacity", "Dynamics"},
    {"pressure-flow", "Dynamics"},
    {"pressure-gamma", "Dynamics"},
    {"accumulate", "Opacity & Flow"},
    {"spacing", "Stroke"},
    {"spacing-jitter", "Stroke"},
    {"taper-in", "Stroke"},
    {"taper-out", "Stroke"},
    {"follow-direction", "Rotation"},
    {"stabilization", "Stabilization"},
    {"stroke-delay", "Stabilization"},
    {"rotation-offset", "Rotation"},
    {"rotation-jitter", "Rotation"},
    {"follow-rotation", "Rotation"},
    {"scatter-x", "Scatter"},
    {"scatter-y", "Scatter"},
    {"scatter-count", "Scatter"},
    {"cluster", "Scatter"},
    {"texture-strength", "Texture"},
    {"texture-scale", "Texture"},
    {"texture-rotation", "Texture"},
    {"texture-offset-x", "Texture"},
    {"texture-offset-y", "Texture"},
    {"texture-contrast", "Texture"},
    {"texture-depth", "Texture"},
    {"hue-variation", "Color"},
    {"saturation-variation", "Color"},
    {"brightness-variation", "Color"},
    {"color-mix", "Color"},
    {"paint-load", "Material & Blending"},
    {"pigment-concentration", "Material & Blending"},
    {"water-amount", "Material & Blending"},
    {"paint-body", "Material & Blending"},
    {"pickup", "Material & Blending"},
    {"depletion", "Material & Blending"},
    {"wetness", "Material & Blending"},
    {"drying-speed", "Material & Blending"},
    {"tooth-response", "Material & Blending"},
    {"granulation", "Material & Blending"},
    {"edge-accumulation", "Material & Blending"},
    {"pressure-amount", "Material & Blending"},
    {"pressure-contact", "Material & Blending"},
    {"persistent-reservoir", "Material & Blending"},
    {"bristle-count", "Brush Tip"},
    {"bristle-spread", "Brush Tip"},
    {"particle-count", "Advanced"},
    {"particle-spread", "Advanced"},
    {"sequence-step", "Advanced"},
    {"antialias", "Rendering"},
    {"blend-strength", "Rendering"},
    {"component-opacity", "Advanced"},
    {"component-scale", "Advanced"},
    {"component-rotation", "Advanced"},
    {"component-aspect", "Advanced"},
    {"component-offset-x", "Advanced"},
    {"component-offset-y", "Advanced"},
    {"component-weight", "Advanced"},
    {"tip-shape", "Brush Tip"},
};

} // namespace

std::span<const char* const> parameterSections() {
    return std::span<const char* const>(kSections, std::size(kSections));
}

const char* parameterSection(std::string_view name) {
    for (const auto& entry : kSectionOf) {
        if (name == entry.name) return entry.section;
    }
    return nullptr;
}

int sectionRank(std::string_view sectionName) {
    for (std::size_t i = 0; i < std::size(parameterSections()); ++i) {
        if (sectionName == parameterSections()[i]) return int(i);
    }
    return -1;
}

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
