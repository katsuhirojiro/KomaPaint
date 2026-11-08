#pragma once

#include <QtGlobal>

namespace komapaint::tools {

// Identifiers for tools selectable in the left tool palette.
// In Phase 1 these are interface placeholders — no actual painting occurs.
enum class Id {
    Selection,
    Move,
    Brush,
    Pencil,
    Eraser,
    Fill,
    Eyedropper,
    Text,
};

}  // namespace komapaint::tools
