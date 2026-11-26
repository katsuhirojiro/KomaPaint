// Writes the seven .kb brushes used by the human Brush Studio artist check.
//
// This is test material, not an installed default: the files live beside the
// application in Human-Test/Brushes and are opened explicitly in Brush Studio.
// Keeping the generator here makes the exact settings reviewable, and lets the
// Windows build regenerate byte-identical fixtures if they are ever lost.
#include "brush/BrushFormat.h"
#include "brush/BrushModel.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

using komapaint::brush::BrushDocument;
using komapaint::brush::LegacyBrush;

struct Recipe {
    const char* file;
    const char* name;
    const char* description;
    const char* tip;
    double size;
    double opacity;
    double flow;
    double spacing;
    double hardness;
    double pressureSize;
    double pressureOpacity;
    double minSizeRatio;
};

// Artist-facing intent, expressed through the same flat record the Studio's
// migration reads. No hidden renderer flags: each value below is a Sub-Tool
// setting the human can inspect and change.
const Recipe kRecipes[] = {
    {"01-round-ink.kb", "Round ink",
     "Crisp, fully opaque round ink. Pressure does not change the nib.",
     "round", 24, 100, 100, 8, 100, 0, 100, 15},
    {"02-pressure-pencil.kb", "Pressure pencil",
     "Graphite grain: light pressure is thin and pale, firm pressure is darker and wider.",
     "graphite", 12, 90, 65, 10, 100, 55, 90, 15},
    {"03-soft-airbrush.kb", "Soft airbrush",
     "Large, low-flow, zero-hardness round tip. Repeated dabs build a soft cloud.",
     "round", 90, 100, 8, 8, 0, 15, 95, 15},
    {"04-small-low-pressure.kb", "Small low-pressure nib",
     "A 6 px nib squeezed by pressure-to-size at a light touch.",
     "round", 6, 100, 100, 10, 100, 70, 100, 10},
    {"05-large-varying-pressure.kb", "Large pressure wash",
     "A 160 px round tip whose diameter follows a long pressure ramp.",
     "round", 160, 100, 100, 10, 100, 65, 100, 10},
    {"06-high-spacing.kb", "High-spacing round",
     "The artist's own 50% spacing: dabs meet at their rims rather than hiding the step.",
     "round", 40, 100, 100, 50, 100, 0, 100, 15},
    {"07-changing-pressure.kb", "Changing-pressure round",
     "A 48 px round tip with a strong pressure-to-size response for one long gesture.",
     "round", 48, 100, 100, 12, 100, 80, 100, 10},
};

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path out = argc > 1 ? argv[1] : "docs/testing/brushes";
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    if (ec) {
        std::cerr << "cannot create " << out << ": " << ec.message() << '\n';
        return 1;
    }
    for (const auto& recipe : kRecipes) {
        LegacyBrush legacy;
        legacy.size = recipe.size;
        legacy.opacity = recipe.opacity;
        legacy.flow = recipe.flow;
        legacy.spacing = recipe.spacing;
        legacy.hardness = recipe.hardness;
        legacy.pressureSize = recipe.pressureSize;
        legacy.pressureOpacity = recipe.pressureOpacity;
        legacy.minSizeRatio = recipe.minSizeRatio;
        legacy.tipStyle = recipe.tip;
        legacy.realisticEngine = false;

        const std::string id = std::filesystem::path(recipe.file).stem().string();
        BrushDocument document = BrushDocument::fromLegacy(legacy, id, recipe.name);
        document.description = recipe.description;
        document.category = "Artist verification";
        document.seed = 0x4b4f4d41U; // deterministic, shared by the test and the human build
        const auto validation = document.validate();
        if (!validation.ok()) {
            std::cerr << recipe.file << ": " << validation.summary() << '\n';
            return 1;
        }
        std::string error;
        if (!komapaint::brush::saveBrushFile(out / recipe.file, document, &error)) {
            std::cerr << recipe.file << ": " << error << '\n';
            return 1;
        }
        std::cout << (out / recipe.file).string() << '\n';
    }
    return 0;
}
