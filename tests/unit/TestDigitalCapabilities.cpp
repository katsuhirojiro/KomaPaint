// The Digital renderer's capability description is the single source for
// Studio controls, preview, install and the Sub-Tool library. This test locks
// down its published vocabulary and its combination failures.
#include "drawing/DigitalBrushCapabilities.h"
#include "brush/BrushFormat.h"
#include "brush/BrushModel.h"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace komapaint;

static int g_checks = 0;

#define CHECK(test) do { \
    ++g_checks; \
    if (!(test)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); \
        return 1; \
    } \
} while (false)

namespace {

brush::BrushDocument baseDocument()
{
    auto doc = brush::BrushDocument::fromLegacy({}, "capability-test", "Capability test");
    return doc;
}

} // namespace

int main()
{
    // Static vocabulary: every registry setting has an explicit answer.
    for (const auto& info : brush::parameterRegistry()) {
        const bool supported = drawing::digitalSupportsParameter(info.name);
        if (info.name == std::string("taper-in") || info.name == std::string("taper-out"))
            CHECK(!supported);
        if (info.name == std::string("texture-strength") || info.name == std::string("bristle-count") ||
            info.name == std::string("stroke-delay") || info.name == std::string("accumulate") ||
            info.name == std::string("particle-count") || info.name == std::string("antialias"))
            CHECK(!supported);
        if (info.name == std::string("size") || info.name == std::string("aspect") ||
            info.name == std::string("tip-angle") || info.name == std::string("follow-direction") ||
            info.name == std::string("hardness") || info.name == std::string("stabilization") ||
            info.name == std::string("scatter-x") || info.name == std::string("rotation-offset") ||
            info.name == std::string("tip-shape"))
            CHECK(supported);
    }

    for (const auto kind : {brush::ComponentKind::ProceduralShape, brush::ComponentKind::Tip,
                            brush::ComponentKind::Image, brush::ComponentKind::ColourImage})
        CHECK(drawing::digitalSupportsComponentKind(kind));
    for (const auto kind : {brush::ComponentKind::Texture, brush::ComponentKind::AlphaMask,
                            brush::ComponentKind::GrayscaleMask, brush::ComponentKind::Particle,
                            brush::ComponentKind::BristleGroup, brush::ComponentKind::Pattern,
                            brush::ComponentKind::Sequence})
        CHECK(!drawing::digitalSupportsComponentKind(kind));
    CHECK(drawing::digitalSupportsBlend(brush::ComponentBlend::Normal));
    for (const auto blend : {brush::ComponentBlend::Multiply, brush::ComponentBlend::Screen,
                             brush::ComponentBlend::Overlay, brush::ComponentBlend::Height,
                             brush::ComponentBlend::Mask})
        CHECK(!drawing::digitalSupportsBlend(blend));

    // Every brush-scope setting is either consumed or refused. This is the
    // exhaustive companion to the hand-picked vocabulary checks above.
    for (const auto& info : brush::parameterRegistry()) {
        if (info.scope != brush::ParameterScope::Brush) continue;
        auto probe = baseDocument();
        double value = info.defaultValue == info.maximum ? info.minimum
                                                         : std::min(info.maximum, info.defaultValue + 1.0);
        if (std::string(info.name) == "taper-in" || std::string(info.name) == "taper-out") value = 1.0;
        if (std::string(info.name) == "follow-direction" || std::string(info.name) == "follow-rotation" ||
            std::string(info.name) == "tilt-angle" || std::string(info.name) == "persistent-reservoir")
            value = 1.0;
        if (!brush::parameterInRange(info, value)) continue;
        CHECK(probe.setParameter(info.name, value));
        const auto report = drawing::inspectDigitalBrush(probe);
        const std::string feature = std::string("parameter:") + info.name;
        if (drawing::digitalSupportsParameter(info.name)) {
            CHECK(!report.find(feature));
        } else {
            CHECK(report.find(feature));
        }
    }

    auto doc = baseDocument();
    CHECK(drawing::inspectDigitalBrush(doc).supported);
    CHECK(drawing::inspectDigitalBrush(doc).summary().empty());

    // Unsupported stored data is reported, never silently dropped.
    CHECK(doc.setParameter("texture-strength", 42));
    const auto texture = drawing::inspectDigitalBrush(doc);
    CHECK(!texture.supported);
    CHECK(texture.find("parameter:texture-strength"));
    CHECK(!texture.summary().empty());

    // The same document after removing the unsupported value is renderable.
    doc.parameters.erase("texture-strength");
    CHECK(drawing::inspectDigitalBrush(doc).supported);

    // Taper is special: the registry default (20) is not a Digital feature.
    CHECK(doc.setParameter("taper-in", 20));
    CHECK(!drawing::inspectDigitalBrush(doc).supported);
    doc.parameters.set("taper-in", 0);
    CHECK(drawing::inspectDigitalBrush(doc).supported);

    // Component kind and blend are combination checks, not UI labels.
    doc = baseDocument();
    brush::BrushComponent textureComponent;
    textureComponent.id = "texture";
    textureComponent.kind = brush::ComponentKind::Texture;
    textureComponent.groupId = doc.components.front().groupId;
    brush::BrushResource texturePixels;
    texturePixels.id = "texture-pixels";
    texturePixels.kind = brush::ResourceKind::Grayscale8;
    texturePixels.width = texturePixels.height = 2;
    texturePixels.setPixels({255, 128, 64, 0});
    textureComponent.resourceId = texturePixels.id;
    CHECK(doc.addResource(texturePixels));
    CHECK(doc.addComponent(textureComponent));
    CHECK(!drawing::inspectDigitalBrush(doc).supported);
    CHECK(drawing::inspectDigitalBrush(doc).find("component:texture"));

    doc = baseDocument();
    doc.components.front().blend = brush::ComponentBlend::Multiply;
    const auto blend = drawing::inspectDigitalBrush(doc);
    CHECK(!blend.supported && blend.find("component:" + doc.components.front().id));
    CHECK(drawing::digitalBlendReason(brush::ComponentBlend::Multiply).find("Normal") != std::string::npos);

    // Bitmap tips own their edge; stored Hardness is not silently applied.
    auto bitmap = baseDocument();
    bitmap.components.front().kind = brush::ComponentKind::Tip;
    bitmap.components.front().parameters.clear();
    brush::BrushResource resource;
    resource.id = "tip";
    resource.kind = brush::ResourceKind::Grayscale8;
    resource.width = resource.height = 2;
    resource.setPixels({255, 255, 255, 255});
    bitmap.components.front().resourceId = resource.id;
    CHECK(bitmap.addResource(resource));
    CHECK(bitmap.setParameter("hardness", 55));
    const auto bitmapReport = drawing::inspectDigitalBrush(bitmap);
    CHECK(!bitmapReport.supported && bitmapReport.find("parameter:hardness"));

    std::printf("%d checks, 0 failures\n", g_checks);
    std::puts("Digital capability vocabulary, combinations and explanations passed.");
    return 0;
}
