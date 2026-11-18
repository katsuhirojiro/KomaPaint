#pragma once
// The universal brush document: one brush, whatever it is made of.
//
// A brush is not one image with settings attached (Part 2.1). It is a
// document holding components, the groups those components are chosen from,
// the generators that turn them into marks, the dynamics that map input to
// properties, the randomization that varies them, the settings an artist
// chose to expose, and the pixel resources they all share.
//
// The model owns no rendering. It is inspected by the editors (Stage 3+),
// projected into the production engines (Stage 9), and serialised by
// BrushFormat (.kb). Keeping it Qt-free is what makes that testable outside
// a GUI build.
#include "brush/BrushParameters.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace komapaint::brush {

// Placement of one component inside the tip footprint.
struct ComponentTransform {
    double offsetX = 0.0;         // percent of the tip, -200..200
    double offsetY = 0.0;
    double rotationDegrees = 0.0;
    double scale = 1.0;           // 1 = natural size
    double aspect = 1.0;
    bool flipX = false;
    bool flipY = false;
    bool operator==(const ComponentTransform&) const = default;
};

// One thing a brush is made of. Its pixels, if it has any, live once in the
// document's resource table and are referenced by id, so a fifty-leaf brush
// does not store fifty copies of a leaf.
struct BrushComponent {
    std::string id;
    std::string name;
    ComponentKind kind = ComponentKind::Tip;
    std::string resourceId;       // empty for procedural components
    std::string groupId;          // empty = the brush's root group
    ComponentTransform transform;
    double opacity = 1.0;         // 0..1
    bool visible = true;
    ComponentBlend blend = ComponentBlend::Normal;
    std::string maskComponentId;  // another component's coverage gates this one
    double weight = 1.0;          // relative probability inside a weighted group
    bool erases = false;          // subtracts coverage instead of adding it
    ParamBag parameters;          // component-scope overrides
    bool operator==(const BrushComponent&) const = default;
};

// How a set of components is chosen from, and how it composites.
struct ComponentGroup {
    std::string id;
    std::string name;
    std::string parentGroupId;    // empty = top level; groups may nest
    SelectionMode mode = SelectionMode::All;
    std::vector<std::string> members; // ordered: sequence modes depend on it
    int membersPerStamp = 1;      // how many members a stamp uses
    std::uint32_t seed = 0;       // deterministic selection
    bool operator==(const ComponentGroup&) const = default;
};

// How components become marks. Generators combine; they are not categories.
struct BrushGenerator {
    std::string id;
    GeneratorKind kind = GeneratorKind::Continuous;
    std::string groupId;          // the group this generator draws from
    bool enabled = true;
    ParamBag parameters;
    bool operator==(const BrushGenerator&) const = default;
};

// Maps an input signal onto a parameter through a response curve.
struct DynamicsBinding {
    std::string parameter;        // registry name
    Signal signal = Signal::Pressure;
    ResponseCurve curve;          // signal value -> parameter multiplier
    double amount = 1.0;          // 0 = binding off, 1 = full effect
    bool perStamp = true;         // per stamp or once per stroke
    bool operator==(const DynamicsBinding&) const = default;
};

// Independent randomization for one parameter (Part 6: never one generic
// random checkbox). Each rule owns its own range, distribution and seed, and
// can be per-stamp or per-stroke.
struct RandomizationRule {
    std::string parameter;
    bool enabled = false;
    double minimum = 0.0;
    double maximum = 0.0;
    Distribution distribution = Distribution::Uniform;
    bool perStamp = true;
    std::uint32_t seed = 0;
    bool sharedSeed = false;      // reuse the brush seed for reproducible sheets
    bool operator==(const RandomizationRule&) const = default;
};

// Which settings the normal Sub-Tool Properties panel shows, under what
// label, in what section and order. This is brush data, not window state, so
// it travels with the .kb (Part 10).
struct ExposedProperty {
    std::string parameter;
    std::string label;            // empty = use the registry label
    std::string section;          // empty = the default section
    int order = 0;
    bool visible = true;
    ControlType control = ControlType::Slider;
    double minimumOverride = 0.0; // NaN-equivalent: 0 means "use registry"
    double maximumOverride = 0.0;
    bool hasRangeOverride = false;
    bool operator==(const ExposedProperty&) const = default;
};

// Shared pixel data. One entry can back many components.
struct BrushResource {
    std::string id;
    std::string name;
    ResourceKind kind = ResourceKind::Grayscale8;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    bool operator==(const BrushResource&) const = default;
    std::size_t expectedBytes() const;
};

struct ValidationIssue {
    enum class Severity { Warning, Error };
    Severity severity = Severity::Error;
    std::string code;    // stable machine-readable reason
    std::string detail;  // human sentence
};

class ValidationResult {
public:
    void addError(std::string code, std::string detail);
    void addWarning(std::string code, std::string detail);
    bool ok() const;                       // no errors
    bool hasWarnings() const;
    const std::vector<ValidationIssue>& issues() const { return m_issues; }
    std::string summary() const;           // first error, or "ok"
private:
    std::vector<ValidationIssue> m_issues;
};

// A flat brush record as the application has stored it until now. Used to
// migrate existing presets into the model without touching their numbers.
struct LegacyBrush {
    double size = 16;
    double opacity = 100;
    double flow = 100;
    double spacing = 15;
    double hardness = 100;
    double stabilization = 0;
    double pressureSize = 0;
    double pressureOpacity = 100;
    double pressureFlow = 0;
    double pressureGamma = 100;
    double minSizeRatio = 15;
    double tiltSize = 0;
    bool tiltAngle = false;
    bool stylusRotation = false;
    double blendStrength = 0;
    double colorMix = 100;
    double aspectRatio = 100;
    double tipAngle = 0;
    bool followDirection = false;
    bool erase = false;
    bool realisticEngine = false;
    std::string tipStyle = "round";
    // Material fields, only meaningful for the Realistic engine.
    double paintLoad = 88;
    double pigmentConcentration = 82;
    double waterAmount = 22;
    double paintBody = 78;
    double pickup = 16;
    double depletion = 100;
    double edgeAccumulation = 8;
    double toothResponse = 18;
    double granulation = 5;
    double dryingSpeed = 115;
    double pressureToAmount = 62;
    double pressureToContact = 35;
};

class BrushDocument {
public:
    // ---- identity ----------------------------------------------------------
    std::string id;
    std::string name = "Untitled brush";
    std::string description;          // empty = undecided, never invented
    Engine engine = Engine::Digital;
    std::string thumbnail;            // optional PNG bytes, base64-free (raw)
    std::vector<std::string> tags;
    std::string category;
    std::uint32_t seed = 0;           // brush-wide seed for reproducible tests

    // ---- contents ----------------------------------------------------------
    std::vector<BrushComponent> components;
    std::vector<ComponentGroup> groups;
    std::vector<BrushGenerator> generators;
    std::vector<DynamicsBinding> dynamics;
    std::vector<RandomizationRule> randomization;
    std::vector<ExposedProperty> exposed;
    std::vector<BrushResource> resources;
    ParamBag parameters;              // brush-scope values

    BrushDocument();
    explicit BrushDocument(std::string name);

    // ---- identity helpers --------------------------------------------------
    std::string allocateId(std::string_view prefix);   // "c1", "g2", "r3"…
    bool idInUse(std::string_view candidate) const;

    // ---- components --------------------------------------------------------
    BrushComponent* component(std::string_view componentId);
    const BrushComponent* component(std::string_view componentId) const;
    int componentIndex(std::string_view componentId) const;
    bool addComponent(BrushComponent component, int index = -1);
    bool removeComponent(std::string_view componentId, bool cascade = true);
    bool moveComponent(std::string_view componentId, int newIndex);
    bool setComponentGroup(std::string_view componentId, std::string_view groupId);
    bool duplicateComponent(std::string_view componentId, std::string* newId = nullptr);

    // ---- groups ------------------------------------------------------------
    ComponentGroup* group(std::string_view groupId);
    const ComponentGroup* group(std::string_view groupId) const;
    bool addGroup(ComponentGroup group);
    bool removeGroup(std::string_view groupId, bool cascade = true);
    bool addToGroup(std::string_view groupId, std::string_view componentId, int index = -1);
    bool removeFromGroup(std::string_view groupId, std::string_view componentId);
    std::vector<const BrushComponent*> groupMembers(std::string_view groupId) const;

    // ---- generators, dynamics, randomization, exposure ---------------------
    bool addGenerator(BrushGenerator generator);
    bool removeGenerator(std::string_view generatorId);
    BrushGenerator* generator(std::string_view generatorId);
    bool setParameter(std::string_view parameterName, double value);   // brush scope
    double parameter(std::string_view parameterName, double fallback = 0.0) const;
    bool bindDynamics(DynamicsBinding binding);               // replaces same pair
    bool removeDynamics(std::string_view parameter, Signal signal);
    bool addRandomization(RandomizationRule rule);            // replaces same param
    bool removeRandomization(std::string_view parameter);
    bool expose(ExposedProperty property);                    // replaces same param
    bool unexpose(std::string_view parameter);
    std::vector<const ExposedProperty*> visibleExposed() const; // ordered by section/order

    // ---- resources ---------------------------------------------------------
    BrushResource* resource(std::string_view resourceId);
    const BrushResource* resource(std::string_view resourceId) const;
    bool addResource(BrushResource resource);                 // replaces by id
    int resourceUsage(std::string_view resourceId) const;             // components pointing at it
    std::size_t resourceBytes() const;

    // ---- correctness -------------------------------------------------------
    ValidationResult validate() const;

    // ---- migration ---------------------------------------------------------
    static BrushDocument fromLegacy(const LegacyBrush& legacy, std::string id,
                                    std::string name);
    // Reads back the flat projection. Only meaningful for a single-tip
    // document, which is exactly what fromLegacy produces; used to prove that
    // migration is lossless for the values the old format could express.
    LegacyBrush toLegacy() const;

    bool operator==(const BrushDocument&) const = default;
};

} // namespace komapaint::brush
