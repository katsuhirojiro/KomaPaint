#pragma once

#include <QtGlobal>

// Centralized list of KomaPaint module identifiers. Used by the plugin / panel
// system to register UI panels and workspace sections without hard-coded strings.

namespace komapaint::modules {

inline constexpr const char* kCore      = "core";
inline constexpr const char* kApp       = "app";
inline constexpr const char* kCanvas    = "canvas";
inline constexpr const char* kDrawing   = "drawing";
inline constexpr const char* kLayers    = "layers";
inline constexpr const char* kAnimation = "animation";
inline constexpr const char* kColor     = "color";
inline constexpr const char* kAssets    = "assets";
inline constexpr const char* kFiles     = "files";
inline constexpr const char* kHistory   = "history";

}  // namespace komapaint::modules
