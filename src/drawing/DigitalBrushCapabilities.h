#pragma once
// One description of what the production Digital renderer can paint.
//
// The Studio's editors, the preview pad, save/install and the Sub-Tool library
// all ask this module. They must not keep separate allow-lists: a setting that
// is editable but rejected at install is exactly the failure this exists to
// prevent.
#include "brush/BrushModel.h"

#include <string>
#include <string_view>
#include <vector>

namespace komapaint::drawing {

struct DigitalCapabilityIssue {
    // Stable machine key: "parameter:<name>", "component:<id>", "blend",
    // "generator", "binding:<name>", "variation:<name>", "engine", ...
    std::string feature;
    // Artist-facing explanation. No validator internals.
    std::string message;
};

struct DigitalCapabilityReport {
    bool supported = false;
    std::vector<DigitalCapabilityIssue> issues;
    std::string summary() const; // first issue, or empty when supported
    const DigitalCapabilityIssue* find(std::string_view feature) const;
};

// Full document check, including combinations such as a bitmap tip with a
// stored hardness other than 100.
DigitalCapabilityReport inspectDigitalBrush(const brush::BrushDocument& document);

// Static vocabulary support. A false result means no value of this setting is
// consumed by the Digital renderer; a true result can still be rejected in a
// particular document (for example Hardness on a bitmap tip).
bool digitalSupportsParameter(std::string_view name);
bool digitalSupportsComponentKind(brush::ComponentKind kind);
bool digitalSupportsBlend(brush::ComponentBlend blend);

// The compact set offered for a new brush and as the fallback for a native
// brush that has no exposure of its own. These are common marker controls, not
// a second support list: callers still ask the predicates above.
std::vector<std::string_view> recommendedDigitalControls();

// Explanations for one control/component. Empty when supported.
std::string digitalParameterReason(const brush::BrushDocument& document, std::string_view name);
std::string digitalComponentReason(const brush::BrushDocument& document, std::string_view componentId);
std::string digitalBlendReason(brush::ComponentBlend blend);

} // namespace komapaint::drawing
