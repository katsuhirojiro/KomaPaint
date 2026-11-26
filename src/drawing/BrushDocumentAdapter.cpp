#include "drawing/BrushDocumentAdapter.h"
#include "drawing/DigitalBrushCapabilities.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string_view>

namespace komapaint {
namespace {

constexpr double kPi = 3.14159265358979323846;

// The flat record stores a tip style as a name; the model stores which of the
// engines' five procedural masks it uses. These two functions are the only
// place that mapping lives, so the migration and this adapter cannot disagree.
int tipShapeForStyle(const std::string& style) {
    if (style == "textured") return 1;
    if (style == "bristle") return 2;
    if (style == "graphite") return 3;
    if (style == "stipple") return 4;
    return 0;
}

BrushTip digitalTipForShape(double shape) noexcept {
    switch (static_cast<int>(std::lround(shape))) {
    case 1: return BrushTip::Textured;
    case 2: return BrushTip::Bristle;
    case 3: return BrushTip::Graphite;
    case 4: return BrushTip::Stipple;
    default: return BrushTip::Round;
    }
}

// The tip mask a document names, or 0 (round) when it names none. Only the
// procedural components carry this today; image and pattern components are
// painted by the evaluator, not by the tip selector.
double tipShape(const brush::BrushDocument& document) {
    for (const auto& component : document.components) {
        if (component.kind == brush::ComponentKind::ProceduralShape) {
            return component.parameters.value("tip-shape", 0.0);
        }
    }
    return 0.0;
}

} // namespace

namespace drawing {
namespace {

// Both engines read the same numbers from the same record, so there is one
// reader. A second copy is how the two paths would drift.
struct ResolvedValues {
    double size;
    double opacity;
    double flow;
    double spacing;
    double hardness;
    double aspectRatio;
    double tipAngle;
    double pressureSize;
    double pressureOpacity;
    double pressureFlow;
    double pressureGamma;
    double minSizeRatio;
    double tiltSize;
    bool tiltAngle;
    bool stylusRotation;
    double blendStrength;
    double colorMix;
    double taperIn;
    double taperOut;
    double spacingJitter;
    double strokeDelay;
    bool followDirection;
    bool erase;
    bool texturedTip;
    bool persistentReservoir;
    // material
    double paintLoad;
    double pigmentConcentration;
    double waterAmount;
    double paintBody;
    double pickup;
    double depletion;
    double edgeAccumulation;
    double toothResponse;
    double granulation;
    double dryingSpeed;
    double pressureToAmount;
    double pressureToContact;
};

ResolvedValues resolve(const brush::LegacyBrush& b) {
    ResolvedValues v{};
    v.size = b.size;
    v.opacity = b.opacity;
    v.flow = b.flow;
    v.spacing = b.spacing;
    v.hardness = b.hardness;
    v.aspectRatio = b.aspectRatio;
    v.tipAngle = b.tipAngle;
    v.pressureSize = b.pressureSize;
    v.pressureOpacity = b.pressureOpacity;
    v.pressureFlow = b.pressureFlow;
    v.pressureGamma = b.pressureGamma;
    v.minSizeRatio = b.minSizeRatio;
    v.tiltSize = b.tiltSize;
    v.tiltAngle = b.tiltAngle;
    v.stylusRotation = b.stylusRotation;
    v.blendStrength = b.blendStrength;
    v.colorMix = b.colorMix;
    v.taperIn = 0.0;
    v.taperOut = 0.0;
    v.spacingJitter = 0.0;
    v.strokeDelay = 0.0;
    v.followDirection = b.followDirection;
    v.erase = b.erase;
    v.texturedTip = b.tipStyle != "round";
    v.persistentReservoir = b.persistentReservoir;
    v.paintLoad = b.paintLoad;
    v.pigmentConcentration = b.pigmentConcentration;
    v.waterAmount = b.waterAmount;
    v.paintBody = b.paintBody;
    v.pickup = b.pickup;
    v.depletion = b.depletion;
    v.edgeAccumulation = b.edgeAccumulation;
    v.toothResponse = b.toothResponse;
    v.granulation = b.granulation;
    v.dryingSpeed = b.dryingSpeed;
    v.pressureToAmount = b.pressureToAmount;
    v.pressureToContact = b.pressureToContact;
    return v;
}

// A stored value, or the registry default. Never a bare zero: a missing
// parameter must mean "the documented default", not "off".
double stored(const brush::BrushDocument& doc, const char* key) {
    const brush::ParameterInfo* info = brush::findParameter(key);
    return doc.parameters.value(key, info ? info->defaultValue : 0.0);
}

// The strength of a dynamics binding, or 0 when the brush has none.
double bindingAmount(const brush::BrushDocument& doc, const char* parameter,
                     brush::Signal signal) {
    for (const auto& binding : doc.dynamics) {
        if (binding.parameter == parameter && binding.signal == signal) {
            return std::clamp(binding.amount, 0.0, 1.0);
        }
    }
    return 0.0;
}

// An explicit value wins; otherwise the binding the migration wrote is the
// source. `fromLegacy` expresses pressure response as bindings, so reading
// both keeps old and new documents on one path.
double storedOrBound(const brush::BrushDocument& doc, const char* key,
                     const char* parameter, brush::Signal signal, double scale) {
    if (doc.parameters.contains(key)) return stored(doc, key);
    return bindingAmount(doc, parameter, signal) * scale;
}

ResolvedValues resolve(const brush::BrushDocument& doc) {
    ResolvedValues v{};
    v.size = stored(doc, "size");
    v.opacity = stored(doc, "opacity");
    v.flow = stored(doc, "flow");
    v.spacing = stored(doc, "spacing");
    v.hardness = stored(doc, "hardness");
    v.aspectRatio = stored(doc, "aspect");
    v.tipAngle = stored(doc, "tip-angle");
    v.pressureSize = storedOrBound(doc, "pressure-size", "size", brush::Signal::Pressure, 100.0);
    // `pressure-opacity` reads the other way round: 100 means "opacity does not
    // follow pressure at all" in the flat record, and the migration encodes the
    // deviation as the binding's amount.
    v.pressureOpacity = doc.parameters.contains("pressure-opacity")
        ? stored(doc, "pressure-opacity")
        : 100.0 - bindingAmount(doc, "opacity", brush::Signal::Pressure) * 100.0;
    v.pressureFlow = storedOrBound(doc, "pressure-flow", "flow", brush::Signal::Pressure, 100.0);
    v.pressureGamma = stored(doc, "pressure-gamma");
    v.minSizeRatio = stored(doc, "min-size-ratio");
    v.tiltSize = storedOrBound(doc, "tilt-size", "size", brush::Signal::Tilt, 100.0);
    v.tiltAngle = stored(doc, "tilt-angle") >= 0.5;
    v.stylusRotation = stored(doc, "follow-rotation") >= 0.5;
    v.blendStrength = stored(doc, "blend-strength");
    v.colorMix = stored(doc, "color-mix");
    v.taperIn = stored(doc, "taper-in");
    v.taperOut = stored(doc, "taper-out");
    v.spacingJitter = stored(doc, "spacing-jitter");
    v.strokeDelay = stored(doc, "stroke-delay");
    v.followDirection = stored(doc, "follow-direction") >= 0.5;
    v.erase = false;   // the tool decides; a component's own eraser is a request
    v.texturedTip = static_cast<int>(std::lround(tipShape(doc))) != 0;
    v.persistentReservoir = stored(doc, "persistent-reservoir") >= 0.5;
    v.paintLoad = stored(doc, "paint-load");
    v.pigmentConcentration = stored(doc, "pigment-concentration");
    v.waterAmount = stored(doc, "water-amount");
    v.paintBody = stored(doc, "paint-body");
    v.pickup = stored(doc, "pickup");
    v.depletion = stored(doc, "depletion");
    v.edgeAccumulation = stored(doc, "edge-accumulation");
    v.toothResponse = stored(doc, "tooth-response");
    v.granulation = stored(doc, "granulation");
    v.dryingSpeed = stored(doc, "drying-speed");
    v.pressureToAmount = stored(doc, "pressure-amount");
    v.pressureToContact = stored(doc, "pressure-contact");
    return v;
}

BrushDefinition digitalFrom(const ResolvedValues& v, double tipShapeValue,
                            const StrokeRuntime& runtime, bool erase) {
    BrushDefinition definition;
    definition.size = v.size;
    definition.opacity = v.opacity / 100.0;
    definition.flow = v.flow / 100.0;
    definition.spacing = v.spacing / 100.0;
    definition.tip = digitalTipForShape(tipShapeValue);
    definition.hardness = v.hardness / 100.0;
    definition.aspectRatio = v.aspectRatio / 100.0;
    definition.angleRadians = v.tipAngle * (kPi / 180.0);
    definition.followDirection = v.followDirection;
    definition.pressureSize = v.pressureSize / 100.0;
    definition.pressureOpacity = v.pressureOpacity / 100.0;
    definition.pressureFlow = v.pressureFlow / 100.0;
    definition.pressureGamma = v.pressureGamma / 100.0;
    definition.minSizeRatio = v.minSizeRatio / 100.0;
    definition.tiltSize = v.tiltSize / 100.0;
    definition.tiltAngle = v.tiltAngle;
    definition.stylusRotation = v.stylusRotation;
    definition.blendStrength = v.blendStrength / 100.0;
    definition.colorMix = v.colorMix / 100.0;
    definition.taperIn = std::clamp(v.taperIn / 100.0, 0.0, 1.0);
    definition.taperOut = std::clamp(v.taperOut / 100.0, 0.0, 1.0);
    definition.spacingJitter = std::clamp(v.spacingJitter / 100.0, 0.0, 1.0);
    definition.strokeDelay = std::clamp(v.strokeDelay / 100.0, 0.0, 1.0);
    definition.erase = erase;
    definition.color = runtime.color;
    return definition;
}

// The material record, from resolved values. Shared by both inputs for the
// same reason as the digital one.
material::BrushDefinition materialFrom(material::BrushDefinition preset,
                                       const ResolvedValues& v, bool erase) {
    const bool wasRound = preset.tip == material::TipGeometry::Round;
    preset.size = v.size;
    preset.reservoirCapacity = preset.effectiveReservoirCapacity();
    preset.hardness = v.hardness / 100.0;
    preset.spacingRatio = v.spacing / 100.0;
    preset.aspect = v.aspectRatio / 100.0;
    preset.rotationRadians = v.tipAngle * (kPi / 180.0);
    preset.followDirection = v.followDirection;
    preset.initialLoad = v.paintLoad / 100.0;
    preset.depletionRate = v.depletion / 100.0;
    preset.pressureTransfer = v.pressureToAmount / 100.0;
    preset.contactPressureScale = v.pressureToContact / 100.0;
    preset.paintBody = v.paintBody / 100.0;
    preset.dryingRate = v.dryingSpeed / 100.0;
    preset.edgeAccumulation = v.edgeAccumulation / 100.0;
    preset.toothResponse = v.toothResponse / 100.0;
    preset.granulationResponse = v.granulation / 100.0;
    const double pickupFactor = v.pickup / 100.0;
    preset.pickupRate = pickupFactor;
    preset.paintLiftability = std::clamp(0.15 + pickupFactor * 0.85, 0.0, 1.0);
    preset.rewetting = pickupFactor * 0.9;
    preset.releaseRate *= v.flow / 100.0;
    preset.contactTransfer *= v.opacity / 100.0 * (erase ? v.flow / 100.0 : 1.0);
    preset.autoReloadOnStroke = !v.persistentReservoir;
    if (v.texturedTip && wasRound) {
        preset.tip = material::TipGeometry::Bristle;
        preset.bristleCount = 13;
        preset.bristleSpread = 0.55;
        preset.textureStrength = std::max(preset.textureStrength, 0.45);
    }
    return preset;
}

// The pigment/water/binder batch the reservoir is loaded with.
void assignPaint(material::BrushDefinition* definition,
                 const material::BrushDefinition& preset, double pigmentConcentration,
                 double waterAmount, double paintBody, std::uint32_t rgbaStraight,
                 double alpha) {
    if (preset.initialPaint.pigments.empty()) {
        definition->initialPaint = preset.initialPaint;
        const double waterScale = std::max(0.1, waterAmount / 100.0);
        definition->initialPaint.water = std::max(0.25, preset.initialPaint.water * waterScale);
        definition->initialPaint.wetness = 1.0;
        definition->initialPaint.consistency = 0.0;
        return;
    }
    const double a = std::max(0.05, alpha);
    const double concentration = pigmentConcentration / 100.0;
    const double waterRatio = waterAmount / 100.0;
    const double bodyRatio = paintBody / 100.0;
    if (definition->mediumId == "watercolour") {
        const double pigmentMass = a * (0.35 + 0.90 * concentration);
        const double binderMass = a * (0.04 + 0.14 * bodyRatio);
        const double waterMass = a * (0.40 + 2.20 * waterRatio);
        definition->initialPaint = material::makeResidualPaint(
            rgbaStraight, pigmentMass, waterMass, binderMass, definition->mediumId);
    } else {
        const double pigmentMass = a * (0.35 + 1.25 * concentration);
        const double binderMass = a * (0.12 + 0.45 * bodyRatio);
        const double waterMass = a * (0.08 + 1.60 * waterRatio);
        definition->initialPaint = material::makeResidualPaint(
            rgbaStraight, pigmentMass, waterMass, binderMass, definition->mediumId);
        if (bodyRatio > 0.0) definition->initialPaint.addFiller(a * 0.22 * bodyRatio);
    }
}

} // namespace

std::string digitalBrushIssue(const brush::BrushDocument& doc) {
    return inspectDigitalBrush(doc).summary();
}

BrushDefinition digitalDefinition(const brush::LegacyBrush& legacy,
                                  const StrokeRuntime& runtime) {
    const ResolvedValues v = resolve(legacy);
    return digitalFrom(v, double(tipShapeForStyle(legacy.tipStyle)), runtime,
                       runtime.erase || legacy.erase);
}

BrushDefinition digitalDefinition(const brush::BrushDocument& document,
                                  const StrokeRuntime& runtime) {
    const ResolvedValues v = resolve(document);
    return digitalFrom(v, tipShape(document), runtime, runtime.erase);
}

material::BrushDefinition materialDefinition(const material::BrushDefinition& preset,
                                             const brush::LegacyBrush& legacy,
                                             const StrokeRuntime& runtime) {
    const ResolvedValues v = resolve(legacy);
    return materialFrom(preset, v, runtime.erase || legacy.erase);
}

material::BrushDefinition materialDefinition(const material::BrushDefinition& preset,
                                             const brush::BrushDocument& document,
                                             const StrokeRuntime& runtime) {
    const ResolvedValues v = resolve(document);
    return materialFrom(preset, v, runtime.erase);
}

void assignInitialPaint(material::BrushDefinition* definition,
                        const brush::LegacyBrush& legacy,
                        std::uint32_t rgbaStraight, double alpha) {
    const material::BrushDefinition preset = *definition;
    assignPaint(definition, preset, legacy.pigmentConcentration, legacy.waterAmount,
                legacy.paintBody, rgbaStraight, alpha);
}

void assignInitialPaint(material::BrushDefinition* definition,
                        const brush::BrushDocument& document,
                        std::uint32_t rgbaStraight, double alpha) {
    const material::BrushDefinition preset = *definition;
    assignPaint(definition, preset, stored(document, "pigment-concentration"),
                stored(document, "water-amount"), stored(document, "paint-body"),
                rgbaStraight, alpha);
}

// ---- DocumentStroke ---------------------------------------------------------

DocumentStroke::DocumentStroke(SparseTileStore& store, brush::BrushDocument document,
                               BrushDefinition base, std::uint32_t strokeSeed)
    : DocumentStroke(store, std::make_shared<const brush::BrushDocument>(std::move(document)),
                     base, strokeSeed, false) {}

DocumentStroke::DocumentStroke(SparseTileStore& store, std::shared_ptr<const brush::BrushDocument> document,
                               BrushDefinition base, std::uint32_t strokeSeed, bool native)
    : m_document(std::move(document)), m_native(native), m_base(base), m_strokeSeed(strokeSeed) {
    if (!m_document) { m_failed = true; m_failureReason = SparseTileStore::WriteResult::InvalidPixel; return; }
    // Off-canvas input can still leave visible pixels after per-component
    // scale, rotation or scattering. Use a conservative footprint in the
    // shared scheduler; the rasterizer clips the actual dab to the tiles.
    auto maxParameter = [&](const char* name) {
        for (const auto& rule : m_document->randomization)
            if (rule.enabled && rule.parameter == name) return brush::findParameter(name)->maximum;
        return stored(*m_document, name);
    };
    const double scatter = std::hypot(maxParameter("scatter-x"), maxParameter("scatter-y"))
                           * (1.0 + maxParameter("cluster") / 100.0) / 100.0;
    const double scale = maxParameter("component-scale") / 100.0;
    for (const auto& component : m_document->components) {
        if (!component.visible) continue;
        const auto& t = component.transform;
        const double offset = std::hypot(t.offsetX / 100.0, t.offsetY / 100.0);
        const double extent = t.scale * scale * (component.resourceId.empty() ? 1.0 : std::sqrt(2.0));
        m_base.stampReach = std::max(m_base.stampReach, offset + scatter + extent);
    }
    if (!std::isfinite(m_base.stampReach)) { m_failed = true; m_failureReason = SparseTileStore::WriteResult::InvalidPixel; return; }
    m_base.randomSeed = m_strokeSeed;
    m_stroke = std::make_unique<BrushStroke>(store, m_base, &DocumentStroke::sink, this);
    if (m_stroke->failed()) {
        m_failed = true;
        m_failureReason = m_stroke->failureReason();
    }
}

bool DocumentStroke::begin(InputSample sample) {
    return m_stroke && m_stroke->begin(sample);
}

bool DocumentStroke::extend(InputSample sample) {
    return m_stroke && m_stroke->extend(sample);
}

bool DocumentStroke::end(InputSample sample) {
    return m_stroke && m_stroke->end(sample);
}

SparseTileStore::WriteResult DocumentStroke::sink(SparseTileStore& store,
                                                  const BrushDefinition& brush,
                                                  InputSample sample, double strokeAngle,
                                                  std::uint32_t carryPremul, bool hasCarry,
                                                  void* context) {
    auto* self = static_cast<DocumentStroke*>(context);
    if (!self) return SparseTileStore::WriteResult::InvalidPixel;
    try {
        return self->paintStamp(store, brush, sample, strokeAngle, carryPremul, hasCarry);
    } catch (const std::bad_alloc&) {
        return SparseTileStore::WriteResult::AllocationFailed;
    } catch (const std::length_error&) {
        return SparseTileStore::WriteResult::AllocationFailed;
    }
}

SparseTileStore::WriteResult DocumentStroke::paintStamp(SparseTileStore& store,
                                                        const BrushDefinition& brush,
                                                        const InputSample& sample,
                                                        double strokeAngle,
                                                        std::uint32_t carryPremul,
                                                        bool hasCarry) {
    brush::StampContext context;
    context.input.pressure = sample.pressure;
    context.input.tilt = sample.hasTilt
        ? std::clamp(std::hypot(sample.tiltX, sample.tiltY) / 90.0, 0.0, 1.0)
        : 0.0;
    context.input.tiltX = sample.tiltX;
    context.input.tiltY = sample.tiltY;
    // Direction, barrel rotation and tilt are owned by the stroke definition --
    // the dab already orients the tip from them -- so they are not fed to the
    // evaluator as well. One decision, one place.
    context.input.rotationRadians = 0.0;
    context.input.directionRadians = 0.0;
    context.input.distance = m_distance;
    context.sizePixels = brush.size;
    context.brushSeed = m_document->seed;
    context.strokeSeed = m_strokeSeed;
    context.stampIndex = m_stampCount;

    const std::vector<brush::DrawRequest> requests =
        brush::BrushEvaluator::evaluate(*m_document, context);

    if (m_stampCount > 0) {
        m_distance += std::hypot(sample.x - m_lastX, sample.y - m_lastY);
    }
    m_lastX = sample.x;
    m_lastY = sample.y;
    ++m_stampCount;
    if (requests.empty()) return SparseTileStore::WriteResult::Unchanged;

    const double radius = brush.size / 2.0;
    SparseTileStore::WriteResult combined = SparseTileStore::WriteResult::Unchanged;
    for (const brush::DrawRequest& request : requests) {
        const auto* component = m_document->component(request.componentId);
        if (!component) return SparseTileStore::WriteResult::InvalidPixel;
        BrushDefinition dabBrush = brush;
        dabBrush.nativeAspect = m_native;
        dabBrush.blend = request.blend;
        if (component->kind == brush::ComponentKind::ProceduralShape) {
            dabBrush.tip = digitalTipForShape(component->parameters.value("tip-shape", 0.0));
        } else if (component->kind == brush::ComponentKind::Tip || component->kind == brush::ComponentKind::Image ||
                   component->kind == brush::ComponentKind::ColourImage) {
            dabBrush.bitmapTip = m_document->resource(component->resourceId);
            dabBrush.bitmapColour = component->kind != brush::ComponentKind::Tip;
            if (!dabBrush.bitmapTip) return SparseTileStore::WriteResult::InvalidPixel;
        } else {
            return SparseTileStore::WriteResult::InvalidPixel;
        }

        if (!component->maskComponentId.empty()) {
            const auto* mask = m_document->component(component->maskComponentId);
            if (!mask || mask->resourceId.empty()) return SparseTileStore::WriteResult::InvalidPixel;
            dabBrush.maskTip = m_document->resource(mask->resourceId);
            if (!dabBrush.maskTip) return SparseTileStore::WriteResult::InvalidPixel;
            const double textureScale = m_document->parameters.contains("texture-scale")
                ? stored(*m_document, "texture-scale") / 100.0 : 1.0;
            const double textureRotation = m_document->parameters.contains("texture-rotation")
                ? stored(*m_document, "texture-rotation") : 0.0;
            dabBrush.maskScale = std::clamp(mask->transform.scale * textureScale, 0.05, 8.0);
            dabBrush.maskRotationRadians =
                (mask->transform.rotationDegrees + textureRotation) * (kPi / 180.0);
            dabBrush.maskOffsetX =
                mask->transform.offsetX / 100.0 +
                (m_document->parameters.contains("texture-offset-x")
                     ? stored(*m_document, "texture-offset-x") / 100.0 : 0.0);
            dabBrush.maskOffsetY =
                mask->transform.offsetY / 100.0 +
                (m_document->parameters.contains("texture-offset-y")
                     ? stored(*m_document, "texture-offset-y") / 100.0 : 0.0);
            dabBrush.maskContrast = m_document->parameters.contains("texture-contrast")
                ? std::clamp(stored(*m_document, "texture-contrast") / 100.0, 0.0, 2.0) : 1.0;
            dabBrush.maskDepth = m_document->parameters.contains("texture-depth")
                ? std::clamp(stored(*m_document, "texture-depth") / 100.0, 0.0, 1.0) : 1.0;
            // A mask is authoritative when the brush has no texture strength
            // yet; an explicit 0 is still a deliberate "mask off".
            dabBrush.maskStrength = m_document->parameters.contains("texture-strength")
                ? std::clamp(stored(*m_document, "texture-strength") / 100.0, 0.0, 1.0)
                : 1.0;
        }

        InputSample dabSample = sample;
        dabBrush.size *= request.scale;
        dabBrush.opacity *= request.opacity;
        dabBrush.angleRadians += request.rotationDegrees * (kPi / 180.0);
        dabBrush.aspectRatio *= request.aspect;
        dabBrush.flipX = request.flipX;
        dabBrush.flipY = request.flipY;
        if (request.erases) dabBrush.erase = true;
        dabSample.x += request.offsetX * radius;
        dabSample.y += request.offsetY * radius;

        const auto result = BrushRasterizer::dab(store, dabBrush, dabSample, strokeAngle,
                                                 carryPremul, hasCarry);
        if (result != SparseTileStore::WriteResult::Changed &&
            result != SparseTileStore::WriteResult::Unchanged) {
            m_failed = true;
            m_failureReason = result;
            return result;
        }
        if (result == SparseTileStore::WriteResult::Changed) {
            combined = SparseTileStore::WriteResult::Changed;
        }
        ++m_dabCount;
    }
    return combined;
}

} // namespace drawing
} // namespace komapaint
