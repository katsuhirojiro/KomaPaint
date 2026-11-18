#pragma once
// The bridge between the universal brush document and the production engines.
//
// Two jobs, deliberately in one place:
//
//  1. Translation. A `brush::BrushDocument` becomes exactly the
//     `drawing::BrushDefinition` or `material::BrushDefinition` the existing
//     engine consumes. The same formulas are used for both the flat legacy
//     record and the document, so a migrated brush is not a second,
//     approximately-equal brush: it is the same numbers.
//
//  2. Rendering. `DocumentStroke` drives the existing `BrushStroke` loop with a
//     stamp sink that paints every component the evaluator asks for. The
//     spacing, angle smoothing, clipping and smudge-carry logic is therefore
//     shared with every brush that came before; nothing here re-implements the
//     stroke engine.
//
// The legacy path is untouched: a `BrushStroke` built without a sink behaves
// exactly as it did, and `TestBrushDocumentEngine` proves the two produce
// identical pixels for the same brush.
#include "brush/BrushEvaluator.h"
#include "brush/BrushModel.h"
#include "drawing/BrushEngine.h"
#include "material/MaterialEngine.h"

#include <cstdint>
#include <memory>
#include <string>

namespace komapaint {

// Values the caller knows at stroke time and the brush record does not.
struct StrokeRuntime {
    std::uint32_t color = 0xff000000;  // straight ARGB
    bool erase = false;                // eraser tool, or a boundary eraser
};

namespace drawing {

// ---- translation -----------------------------------------------------------

BrushDefinition digitalDefinition(const brush::LegacyBrush& legacy,
                                  const StrokeRuntime& runtime);
BrushDefinition digitalDefinition(const brush::BrushDocument& document,
                                  const StrokeRuntime& runtime);

// `preset` supplies the engine defaults the flat record never carried
// (texture strength, initial paint). The record then overrides what it owns,
// which is exactly what the editor did inline before this existed.
material::BrushDefinition materialDefinition(const material::BrushDefinition& preset,
                                             const brush::LegacyBrush& legacy,
                                             const StrokeRuntime& runtime);
material::BrushDefinition materialDefinition(const material::BrushDefinition& preset,
                                             const brush::BrushDocument& document,
                                             const StrokeRuntime& runtime);

// Builds the pigment/water/binder batch the reservoir is loaded with. Kept
// here so the document path and the legacy path cannot drift apart.
void assignInitialPaint(material::BrushDefinition* definition,
                        const brush::LegacyBrush& legacy,
                        std::uint32_t rgbaStraight, double alpha);
void assignInitialPaint(material::BrushDefinition* definition,
                        const brush::BrushDocument& document,
                        std::uint32_t rgbaStraight, double alpha);

// ---- rendering -------------------------------------------------------------

// A stroke whose stamps are decided by the brush document: several components,
// their own transforms, opacity and blend, random selection and randomization,
// all through the production dab and the production stroke loop.
class DocumentStroke {
public:
    DocumentStroke(SparseTileStore& store, brush::BrushDocument document,
                   BrushDefinition base, std::uint32_t strokeSeed);

    bool begin(InputSample sample);
    bool extend(InputSample sample);
    bool end(InputSample sample);
    bool failed() const { return m_failed; }
    SparseTileStore::WriteResult failureReason() const { return m_failureReason; }

    // Stamps placed so far and components painted, for tests and diagnostics.
    std::uint64_t stampCount() const { return m_stampCount; }
    std::uint64_t dabCount() const { return m_dabCount; }

private:
    static SparseTileStore::WriteResult sink(SparseTileStore& store,
                                             const BrushDefinition& brush,
                                             InputSample sample, double strokeAngle,
                                             std::uint32_t carryPremul, bool hasCarry,
                                             void* context);
    SparseTileStore::WriteResult paintStamp(SparseTileStore& store,
                                            const BrushDefinition& brush,
                                            const InputSample& sample, double strokeAngle,
                                            std::uint32_t carryPremul, bool hasCarry);

    brush::BrushDocument m_document;
    BrushDefinition m_base;
    std::uint32_t m_strokeSeed = 0;
    std::unique_ptr<BrushStroke> m_stroke;
    std::uint64_t m_stampCount = 0;
    std::uint64_t m_dabCount = 0;
    // Distance travelled, fed to the evaluator's distance dynamics.
    double m_distance = 0.0;
    double m_lastX = 0.0;
    double m_lastY = 0.0;
    bool m_failed = false;
    SparseTileStore::WriteResult m_failureReason = SparseTileStore::WriteResult::Unchanged;
};

} // namespace drawing
} // namespace komapaint
