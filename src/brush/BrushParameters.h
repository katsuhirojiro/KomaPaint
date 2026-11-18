#pragma once
// The parameter registry: the single vocabulary of brush settings.
//
// Every parameter here either maps onto a field the production engines already
// read, or onto a structural property the model actually uses. Nothing is
// declared "for completeness": a setting with no consumer is a lie to the
// artist, and Part 5.13 forbids exactly that.
//
// The registry is what lets new generators and new dynamics be added without
// changing the document schema: a parameter is a name, not an enum member.
#include "brush/BrushTypes.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace komapaint::brush {

struct ParameterInfo {
    const char* name;      // stable storage key, e.g. "pressure-size"
    const char* label;     // UI label, e.g. "Pressure to size"
    ParameterScope scope;
    ParameterUnit unit;
    double minimum;
    double maximum;
    double defaultValue;
    const char* description; // one sentence, shown as a tooltip
};

std::span<const ParameterInfo> parameterRegistry();
const ParameterInfo* findParameter(std::string_view name);
bool parameterInRange(const ParameterInfo& info, double value);

} // namespace komapaint::brush
