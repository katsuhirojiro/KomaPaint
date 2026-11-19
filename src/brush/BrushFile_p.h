#pragma once
// Private test seam for the on-disk save transaction. Faults are per call,
// never global and never enabled by the application.
#include "brush/BrushFormat.h"

namespace komapaint::brush::detail {

enum class SaveFault { None, ShortWrite, BadReadback, Replace };

bool saveBrushFile(const std::filesystem::path& path, const BrushDocument& document,
                   std::string* error, SaveFault fault);

} // namespace komapaint::brush::detail
