#pragma once
// Brush evaluation: what a brush actually stamps.
//
// This is the decision layer between the document and a renderer. Given a
// brush, an input sample and a stamp index, it answers: which components are
// used, at what transform, opacity and blend, in what order.
//
// It draws nothing, allocates no tiles and knows no pixels, which is what
// makes it testable and what makes it shareable by both engines (Stage 9)
// instead of each engine inventing its own selection rules.
//
// Determinism is a contract: the same document, sample and seeds produce the
// same request list, on every machine, forever. Artists need reproducible
// random brushes, and tests need to be able to assert on the result.
#include "brush/BrushModel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace komapaint::brush {

// The input state at the moment a stamp is placed (Part 5.11).
struct InputSignalSample {
    double pressure = 1.0;
    double tilt = 0.0;            // 0..1 magnitude
    double tiltX = 0.0;           // -1..1, unit vector
    double tiltY = 0.0;
    double rotationRadians = 0.0; // barrel rotation
    double velocity = 0.0;        // 0..1, normalised by the caller
    double directionRadians = 0.0;// stroke tangent
    double distance = 0.0;        // document pixels travelled so far
    double progress = 0.0;        // 0..1 through the stroke
    double time = 0.0;            // seconds since the stroke began
    double speed = 0.0;           // 0..1 normalized pointer speed
};

// Everything the caller knows about the stamp being placed.
struct StampContext {
    InputSignalSample input;
    double sizePixels = 16.0;
    std::uint32_t brushSeed = 0;  // document seed
    std::uint32_t strokeSeed = 0; // caller's seed for this stroke
    std::uint64_t stampIndex = 0; // 0-based, monotonic along the stroke
};

// One component to paint, in document-space-normalised terms.
struct DrawRequest {
    std::string componentId;
    std::string generatorId;
    double offsetX = 0.0;         // fraction of the tip radius, -1..1 typical
    double offsetY = 0.0;
    double rotationDegrees = 0.0;
    double scale = 1.0;
    double aspect = 1.0;
    double opacity = 1.0;         // 0..1
    bool flipX = false;
    bool flipY = false;
    bool erases = false;
    ComponentBlend blend = ComponentBlend::Normal;
    int frameIndex = 0;           // sequence/pattern position
    std::string sequenceKey;      // distinguishes independent sequences
};

// What one evaluation decided, for tests, the preview and the diagnostics the
// specification asks for (Part 14: clear reporting when something is wrong).
struct EvaluationReport {
    int generatorsEvaluated = 0;
    int componentsConsidered = 0;
    int requestsProduced = 0;
    std::vector<std::string> notes;  // plain-language, never silently swallowed
};

class BrushEvaluator {
public:
    // Value of one input signal in 0..1.
    static double signalValue(Signal signal, const InputSignalSample& input) noexcept;

    // Base value of a parameter: the brush's value, or the registry default.
    static double baseParameter(const BrushDocument& document, std::string_view name);

    // Base value with dynamics applied (multiplicative, or additive for
    // angles), before randomization.
    static double dynamicParameter(const BrushDocument& document, std::string_view name,
                                   const InputSignalSample& input);

    // The full resolved value: dynamics, then randomization, then clamped to
    // the registry range. This is the number a renderer should use.
    static double resolvedParameter(const BrushDocument& document, std::string_view name,
                                    const StampContext& context);

    // The stamps for one input sample, in paint order.
    static std::vector<DrawRequest> evaluate(const BrushDocument& document,
                                             const StampContext& context,
                                             EvaluationReport* report = nullptr);

    // Deterministic uniform value in [0,1) for a key. Exposed because the
    // node graph (Stage 13) will need the same source of randomness.
    static double uniform01(std::uint64_t key) noexcept;
};

} // namespace komapaint::brush
