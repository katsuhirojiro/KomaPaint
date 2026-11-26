#include "drawing/DigitalBrushCapabilities.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>

namespace komapaint::drawing {
namespace {

using brush::ComponentKind;
using brush::Signal;

constexpr std::string_view kBrushValues[] = {
    "size", "opacity", "flow", "spacing", "hardness", "aspect", "tip-angle",
    "pressure-size", "pressure-opacity", "pressure-flow", "pressure-gamma", "min-size-ratio",
    "tilt-size", "tilt-angle", "follow-rotation", "follow-direction", "blend-strength", "color-mix",
    "stabilization", "rotation-offset", "scatter-x", "scatter-y", "scatter-count", "cluster", "sequence-step",
    "taper-in", "taper-out", "spacing-jitter", "stroke-delay",
    "texture-strength", "texture-scale", "texture-rotation", "texture-offset-x", "texture-offset-y",
    "texture-contrast", "texture-depth"
};

constexpr std::string_view kRandomValues[] = {
    "rotation-offset", "scatter-x", "scatter-y", "scatter-count", "cluster", "component-scale", "component-opacity"
};

bool contains(std::span<const std::string_view> values, std::string_view key) {
    return std::find(values.begin(), values.end(), key) != values.end();
}

std::string parameterLabel(std::string_view name) {
    const auto* info = brush::findParameter(name);
    return info ? std::string(info->label) : std::string(name);
}

std::string componentLabel(const brush::BrushComponent& component) {
    return component.name.empty() ? component.id : component.name;
}

bool maskSourceKind(ComponentKind kind) noexcept {
    return kind == ComponentKind::Texture || kind == ComponentKind::AlphaMask ||
           kind == ComponentKind::GrayscaleMask;
}

DigitalCapabilityIssue issue(std::string feature, std::string message) {
    return {std::move(feature), std::move(message)};
}

} // namespace

std::string DigitalCapabilityReport::summary() const {
    return issues.empty() ? std::string{} : issues.front().message;
}

const DigitalCapabilityIssue* DigitalCapabilityReport::find(std::string_view feature) const {
    const auto it = std::find_if(issues.begin(), issues.end(),
                                 [feature](const auto& item) { return item.feature == feature; });
    return it == issues.end() ? nullptr : &*it;
}

bool digitalSupportsParameter(std::string_view name) {
    if (name == "tip-shape" || name == "component-opacity" || name == "component-weight")
        return true;
    if (name == "component-scale" || name == "component-rotation" || name == "component-aspect" ||
        name == "component-offset-x" || name == "component-offset-y")
        return true;
    return contains(kBrushValues, name);
}

bool digitalSupportsComponentKind(brush::ComponentKind kind) {
    switch (kind) {
    case ComponentKind::ProceduralShape:
    case ComponentKind::Tip:
    case ComponentKind::Image:
    case ComponentKind::ColourImage:
    case ComponentKind::Texture:
    case ComponentKind::AlphaMask:
    case ComponentKind::GrayscaleMask:
        return true;
    default:
        return false;
    }
}

bool digitalSupportsBlend(brush::ComponentBlend blend) {
    switch (blend) {
    case brush::ComponentBlend::Normal:
    case brush::ComponentBlend::Multiply:
    case brush::ComponentBlend::Screen:
    case brush::ComponentBlend::Overlay:
        return true;
    default:
        return false;
    }
}

std::vector<std::string_view> recommendedDigitalControls() {
    return {"size", "opacity", "flow", "spacing", "hardness", "aspect", "tip-angle"};
}

std::string digitalBlendReason(brush::ComponentBlend blend) {
    if (digitalSupportsBlend(blend)) return {};
    return "Digital painting supports Normal, Multiply, Screen and Overlay component blending. This blend is kept in the brush file.";
}

bool realisticSupportsParameter(std::string_view name) {
    constexpr std::string_view values[] = {
        "size", "opacity", "flow", "spacing", "hardness", "aspect", "tip-angle",
        "follow-direction", "paint-load", "pigment-concentration", "water-amount",
        "paint-body", "pickup", "depletion", "wetness", "drying-speed", "tooth-response",
        "granulation", "edge-accumulation", "pressure-amount", "pressure-contact",
        "persistent-reservoir"
    };
    return contains(values, name);
}

DigitalCapabilityReport inspectRealisticBrush(const brush::BrushDocument& doc) {
    DigitalCapabilityReport report;
    auto add = [&](std::string feature, std::string message) {
        report.issues.push_back(issue(std::move(feature), std::move(message)));
    };
    if (doc.engine != brush::Engine::Realistic) {
        add("engine", "This is not a Realistic brush.");
        return report;
    }
    const auto validation = doc.validate();
    if (!validation.ok()) {
        add("document", validation.summary());
        return report;
    }
    if (doc.formatFlags || !doc.opaqueChunks.empty()) {
        add("extensions", "This brush contains native extensions the Realistic renderer cannot interpret. They are preserved on save.");
    }
    for (const auto& [name, value] : doc.parameters.entries()) {
        const auto* info = brush::findParameter(name);
        if (!realisticSupportsParameter(name) && (!info || value != info->defaultValue)) {
            add("parameter:" + name, parameterLabel(name) +
                " is not used by Realistic paint. The stored value is kept in the brush file.");
        }
    }
    for (const auto& component : doc.components) {
        if (!component.visible) continue;
        if (component.kind != ComponentKind::ProceduralShape) {
            add("component:" + component.id,
                "Realistic paint uses the physical tip, bristles and paint parameters. "
                "This mark component is kept in the brush file but is not painted by the Realistic renderer.");
        }
    }
    for (const auto& binding : doc.dynamics) {
        if (binding.amount == 0.0) continue;
        add("binding:" + binding.parameter,
            "Realistic paint reads pressure through its paint-flow and contact controls, not this response curve. "
            "The binding is kept in the brush file.");
        break;
    }
    for (const auto& rule : doc.randomization) {
        if (!rule.enabled) continue;
        add("variation:" + rule.parameter,
            "Realistic paint does not apply Digital parameter variation. The rule is kept in the brush file.");
        break;
    }
    report.supported = report.issues.empty();
    return report;
}

DigitalCapabilityReport inspectNativeBrush(const brush::BrushDocument& document) {
    return document.engine == brush::Engine::Realistic ? inspectRealisticBrush(document)
                                                      : inspectDigitalBrush(document);
}

std::string nativeBrushIssue(const brush::BrushDocument& document) {
    return inspectNativeBrush(document).summary();
}

std::string digitalParameterReason(const brush::BrushDocument& document, std::string_view name) {
    if (!digitalSupportsParameter(name)) {
        return parameterLabel(name) +
               " is kept in the brush file, but the Digital renderer does not use it.";
    }
    const auto report = inspectDigitalBrush(document);
    if (const auto* found = report.find("parameter:" + std::string(name))) return found->message;
    return {};
}

std::string digitalComponentReason(const brush::BrushDocument& document, std::string_view componentId) {
    const auto* component = document.component(std::string(componentId));
    if (!component) return {};
    if (!digitalSupportsComponentKind(component->kind)) {
        return std::string("Digital painting does not use ") + brush::toString(component->kind) +
               " components. This component is kept in the brush file.";
    }
    const auto report = inspectDigitalBrush(document);
    if (const auto* found = report.find("component:" + std::string(componentId))) return found->message;
    return {};
}

DigitalCapabilityReport inspectDigitalBrush(const brush::BrushDocument& doc) {
    DigitalCapabilityReport report;
    auto add = [&](std::string feature, std::string message) {
        report.issues.push_back(issue(std::move(feature), std::move(message)));
    };

    if (doc.engine != brush::Engine::Digital) {
        add("engine", "Native Realistic brushes can be saved, but this renderer only paints native Digital brushes.");
        return report;
    }
    const auto validation = doc.validate();
    if (!validation.ok()) {
        add("document", validation.summary());
        return report;
    }
    if (doc.formatFlags || !doc.opaqueChunks.empty()) {
        add("extensions", "This brush contains native extensions this renderer cannot interpret. The extensions are preserved on save.");
        return report;
    }

    for (const auto& [name, value] : doc.parameters.entries()) {
        const auto* info = brush::findParameter(name);
        if (!digitalSupportsParameter(name) && (!info || value != info->defaultValue)) {
            add("parameter:" + name, parameterLabel(name) +
                " is not supported by Digital painting. The stored value is kept in the brush file.");
        }
    }

    for (const auto& binding : doc.dynamics) {
        if (binding.amount == 0.0) continue;
        const char* explicitKey = nullptr;
        if (binding.signal == Signal::Pressure) {
            if (binding.parameter == "size") explicitKey = "pressure-size";
            if (binding.parameter == "opacity") explicitKey = "pressure-opacity";
            if (binding.parameter == "flow") explicitKey = "pressure-flow";
        } else if (binding.signal == Signal::Tilt && binding.parameter == "size") {
            explicitKey = "tilt-size";
        }
        double amount = binding.amount * 100;
        if (binding.parameter == "opacity") amount = 100 - amount;
        const bool supported = explicitKey && binding.perStamp && binding.amount <= 1.0 &&
            binding.curve == brush::ResponseCurve::linear() &&
            (!doc.parameters.contains(explicitKey) ||
             std::abs(doc.parameters.value(explicitKey, 0.0) - amount) <= 1.0e-9);
        if (!supported) {
            add("binding:" + binding.parameter,
                "This Digital renderer cannot apply the stored response for '" + binding.parameter + "'. It is kept in the brush file.");
        }
    }

    for (const auto& rule : doc.randomization) {
        if (rule.enabled && (!contains(kRandomValues, rule.parameter) || !std::isfinite(rule.minimum) ||
                             !std::isfinite(rule.maximum) || !std::isfinite(rule.maximum - rule.minimum))) {
            add("variation:" + rule.parameter,
                "This Digital renderer cannot apply the stored variation for '" + rule.parameter + "'. It is kept in the brush file.");
        }
    }

    for (const auto& generator : doc.generators) {
        if (!generator.enabled) continue;
        if (generator.kind != brush::GeneratorKind::Continuous || !generator.parameters.empty()) {
            add("generator", std::string("Digital painting does not yet support the stored '") +
                brush::toString(generator.kind) + "' generator or its overrides.");
        }
    }

    auto hasGenerator = [&](std::string_view groupId) {
        for (const auto& generator : doc.generators) {
            if (generator.enabled && generator.groupId == groupId) return true;
        }
        return false;
    };
    const double hardness = doc.parameters.value("hardness", 100.0);
    const double blendStrength = doc.parameters.value("blend-strength", 0.0);
    bool hasTip = false;
    for (const auto& component : doc.components) {
        if (!component.visible) continue;
        if (maskSourceKind(component.kind)) {
            if (component.resourceId.empty() || !doc.resource(component.resourceId)) {
                add("component:" + component.id, "Component '" + componentLabel(component) +
                    "' is a texture or mask source and needs image pixels.");
            }
            continue;
        }
        bool connected = hasGenerator(component.groupId);
        for (const auto* group = doc.group(component.groupId);
             group && !connected && !group->parentGroupId.empty();
             group = doc.group(group->parentGroupId)) {
            connected = hasGenerator(group->parentGroupId);
        }
        if (!connected) continue;
        hasTip = true;
        const std::string label = componentLabel(component);
        const std::string componentFeature = "component:" + component.id;
        if (!digitalSupportsComponentKind(component.kind)) {
            add(componentFeature, "Digital painting does not use " + std::string(brush::toString(component.kind)) +
                " components. '" + label + "' is kept in the brush file.");
            continue;
        }
        if (!digitalSupportsBlend(component.blend)) {
            add(componentFeature, "Component '" + label + "' uses a blend mode Digital painting cannot render.");
            add("blend", "Digital painting supports Normal, Multiply, Screen and Overlay component blending. "
                "The stored blend on '" + label + "' is kept in the brush file.");
            continue;
        }
        if (!component.maskComponentId.empty()) {
            const auto* mask = doc.component(component.maskComponentId);
            if (!mask || !maskSourceKind(mask->kind) || mask->resourceId.empty() ||
                !doc.resource(mask->resourceId)) {
                add(componentFeature, "Component '" + label + "' is gated by a texture or mask that is missing or has no pixels.");
                continue;
            }
        }
        if (component.kind == ComponentKind::ProceduralShape && component.resourceId.empty()) {
            for (const auto& [key, value] : component.parameters.entries()) {
                (void)value;
                if (key != "tip-shape") {
                    add(componentFeature, "Component '" + label + "' has unsupported tip settings.");
                    break;
                }
            }
            continue;
        }
        const bool colour = component.kind == ComponentKind::Image || component.kind == ComponentKind::ColourImage;
        if (!(colour || component.kind == ComponentKind::Tip) || !component.parameters.empty()) {
            add(componentFeature, "Digital painting does not support component '" + label + "' (" +
                brush::toString(component.kind) + ").");
            continue;
        }
        const auto* bitmap = doc.resource(component.resourceId);
        if (!bitmap || (colour && bitmap->kind != brush::ResourceKind::Rgba8)) {
            add(componentFeature, "Component '" + label + "' needs " +
                (colour ? "an RGBA image." : "valid tip pixels."));
            continue;
        }
        if (hardness != 100.0) {
            add(componentFeature, "Bitmap tips use their own edges. Digital painting cannot apply the stored Hardness to '" + label + "'.");
            add("parameter:hardness", "Hardness cannot change a bitmap tip. Set Hardness to 100 or use a procedural tip.");
        }
        if (colour && blendStrength != 0.0) {
            add(componentFeature, "Colour image stamps do not support Digital smudging yet ('" + label + "').");
            add("parameter:blend-strength", "Smudging is not supported on Colour Image components. Set Blend / Smudge to 0.");
        }
    }
    if (!hasTip) add("components", "No visible tip is connected to an enabled generator.");
    report.supported = report.issues.empty();
    return report;
}

} // namespace komapaint::drawing
