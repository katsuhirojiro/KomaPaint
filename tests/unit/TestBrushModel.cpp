// Universal brush model: structure, invariants and legacy migration.
//
// Deliberately Qt-free (like TestTileGeometry) so the model can be exercised
// with nothing but a C++20 compiler, both here and in CI.
#include "brush/BrushModel.h"

#include <cstdio>
#include <limits>
#include <cstring>
#include <string>

using namespace komapaint::brush;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL line %d: %s\n", line, what);
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

bool hasIssue(const ValidationResult& result, const char* code) {
    for (const auto& issue : result.issues()) {
        if (issue.code == code) return true;
    }
    return false;
}

BrushResource makeResource(const std::string& id, int w, int h) {
    BrushResource r;
    r.id = id;
    r.name = "resource " + id;
    r.kind = ResourceKind::Grayscale8;
    r.width = w;
    r.height = h;
    r.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0x80);
    return r;
}

BrushComponent makeComponent(const std::string& id, const std::string& resourceId) {
    BrushComponent c;
    c.id = id;
    c.name = id;
    c.kind = ComponentKind::Tip;
    c.resourceId = resourceId;
    return c;
}

// A foliage-shaped brush: two groups, weighted leaves, scatter and dynamics.
BrushDocument makeFoliageDocument() {
    BrushDocument doc("Foliage");
    doc.id = "brush-foliage";
    doc.addResource(makeResource("p1", 8, 8));
    doc.addResource(makeResource("p2", 8, 8));

    ComponentGroup leaves;
    leaves.id = "leaves";
    leaves.name = "Leaves";
    leaves.mode = SelectionMode::Weighted;
    doc.addGroup(leaves);

    for (int i = 0; i < 5; ++i) {
        BrushComponent c = makeComponent("leaf" + std::to_string(i), i % 2 == 0 ? "p1" : "p2");
        c.kind = ComponentKind::Image;
        c.weight = 1.0 + static_cast<double>(i);
        c.transform.rotationDegrees = 12.0 * i;
        doc.addComponent(c);
        doc.addToGroup("leaves", c.id);
    }

    BrushGenerator stamp;
    stamp.id = "n1";
    stamp.kind = GeneratorKind::Stamp;
    stamp.groupId = "leaves";
    stamp.parameters.set("scatter-x", 35.0);
    stamp.parameters.set("scatter-y", 30.0);
    doc.addGenerator(stamp);

    DynamicsBinding pressureSize;
    pressureSize.parameter = "size";
    pressureSize.signal = Signal::Pressure;
    pressureSize.amount = 0.8;
    doc.bindDynamics(pressureSize);

    RandomizationRule rotation;
    rotation.parameter = "rotation-offset";
    rotation.enabled = true;
    rotation.minimum = -35.0;
    rotation.maximum = 35.0;
    rotation.perStamp = true;
    rotation.seed = 1234;
    doc.addRandomization(rotation);

    doc.setParameter("size", 120.0);
    doc.setParameter("opacity", 90.0);
    return doc;
}

void testIdsAreUniqueAndAllocated() {
    BrushDocument doc("Ids");
    BrushComponent a = makeComponent("", "");
    a.kind = ComponentKind::ProceduralShape;
    CHECK(doc.addComponent(a));
    BrushComponent b = makeComponent("", "");
    b.kind = ComponentKind::ProceduralShape;
    CHECK(doc.addComponent(b));
    CHECK(doc.components.size() == 2);
    CHECK(doc.components[0].id != doc.components[1].id);
    CHECK(doc.components[0].id == "c1");
    CHECK(doc.components[1].id == "c2");

    // A caller-chosen id is honoured, and a clash is refused rather than
    // silently renamed.
    BrushComponent c = makeComponent("custom", "");
    c.kind = ComponentKind::ProceduralShape;
    CHECK(doc.addComponent(c));
    BrushComponent duplicate = makeComponent("custom", "");
    duplicate.kind = ComponentKind::ProceduralShape;
    CHECK(!doc.addComponent(duplicate));
}

void testOrdering() {
    BrushDocument doc("Order");
    for (int i = 0; i < 3; ++i) {
        BrushComponent c = makeComponent("c" + std::to_string(i + 1), "");
        c.kind = ComponentKind::ProceduralShape;
        CHECK(doc.addComponent(c));
    }
    CHECK(doc.components[0].id == "c1");
    CHECK(doc.moveComponent("c3", 0));
    CHECK(doc.components[0].id == "c3");
    CHECK(doc.components[1].id == "c1");
    // Out-of-range moves clamp instead of corrupting the list.
    CHECK(doc.moveComponent("c1", 99));
    CHECK(doc.components.back().id == "c1");
    CHECK(!doc.moveComponent("missing", 0));
}

void testGroupsAndMembershipStayConsistent() {
    BrushDocument doc("Groups");
    doc.addResource(makeResource("p1", 4, 4));
    ComponentGroup g;
    g.id = "g-leaves";
    g.name = "Leaves";
    g.mode = SelectionMode::Random;
    CHECK(doc.addGroup(g));

    BrushComponent c = makeComponent("c1", "p1");
    CHECK(doc.addComponent(c));
    CHECK(doc.setComponentGroup("c1", "g-leaves"));

    const ComponentGroup* group = doc.group("g-leaves");
    CHECK(group != nullptr);
    CHECK(group->members.size() == 1);
    CHECK(doc.component("c1")->groupId == "g-leaves");
    CHECK(doc.validate().ok());

    // Removing from the group clears both sides.
    CHECK(doc.removeFromGroup("g-leaves", "c1"));
    CHECK(doc.group("g-leaves")->members.empty());
    CHECK(doc.component("c1")->groupId.empty());

    // A group that does not exist cannot be assigned.
    CHECK(!doc.setComponentGroup("c1", "nope"));
    // Nor can a component join a missing group.
    CHECK(!doc.addToGroup("nope", "c1"));
}

void testGroupMembershipMismatchIsAnError() {
    BrushDocument doc("Broken");
    doc.addResource(makeResource("p1", 4, 4));
    ComponentGroup g;
    g.id = "g";
    g.name = "G";
    doc.addGroup(g);
    BrushComponent c = makeComponent("c1", "p1");
    doc.addComponent(c);
    // Construct an inconsistent raw document, bypassing the mutation API.
    doc.component("c1")->groupId = "g";

    const ValidationResult result = doc.validate();
    CHECK(!result.ok());
    CHECK(hasIssue(result, "group-membership"));
}

void testMembershipInsertionReorderingAndRepair() {
    BrushDocument doc("Grouped");
    doc.addResource(makeResource("p1", 4, 4));
    ComponentGroup parent;
    parent.id = "parent";
    CHECK(doc.addGroup(parent));
    ComponentGroup child;
    child.id = "child";
    child.parentGroupId = parent.id;
    CHECK(doc.addGroup(child));

    auto c = makeComponent("c1", "p1");
    c.groupId = "missing";
    CHECK(!doc.addComponent(c));
    CHECK(doc.components.empty());
    c.groupId = child.id;
    CHECK(doc.addComponent(c));
    std::string copy;
    CHECK(doc.duplicateComponent(c.id, &copy));
    CHECK((doc.group(child.id)->members == std::vector<std::string>{c.id, copy}));
    CHECK(doc.addToGroup(child.id, copy, 0));
    CHECK((doc.group(child.id)->members == std::vector<std::string>{copy, c.id}));
    CHECK(doc.setComponentGroup(copy, child.id));
    CHECK((doc.group(child.id)->members == std::vector<std::string>{copy, c.id}));

    // The validator also protects loaded data. Regrouping repairs all copies
    // of this membership without changing another component's affiliation.
    doc.group(child.id)->members.push_back(copy);
    CHECK(hasIssue(doc.validate(), "duplicate-member"));
    doc.group(parent.id)->members.push_back(copy);
    CHECK(hasIssue(doc.validate(), "group-membership"));
    CHECK(doc.setComponentGroup(copy, parent.id));
    CHECK((doc.group(child.id)->members == std::vector<std::string>{c.id}));
    CHECK((doc.group(parent.id)->members == std::vector<std::string>{copy}));
    CHECK(doc.validate().ok());

    ComponentGroup unilateral;
    unilateral.id = "unilateral";
    unilateral.members.push_back(c.id);
    const auto before = doc;
    CHECK(!doc.addGroup(unilateral));
    CHECK(!doc.removeGroup(parent.id, false));
    CHECK(doc == before);
    CHECK(doc.removeGroup(parent.id));
    CHECK(doc.group(child.id)->parentGroupId.empty());
    CHECK(doc.component(copy)->groupId.empty());
    CHECK(doc.component(c.id)->groupId == child.id);
    CHECK(doc.validate().ok());
}

void testRemovingAComponentDetachesItFromGroups() {
    BrushDocument doc("Detach");
    doc.addResource(makeResource("p1", 4, 4));
    ComponentGroup g;
    g.id = "g";
    doc.addGroup(g);
    doc.addComponent(makeComponent("c1", "p1"));
    doc.setComponentGroup("c1", "g");
    CHECK(doc.removeComponent("c1"));
    CHECK(doc.group("g")->members.empty());
    CHECK(doc.component("c1") == nullptr);
    CHECK(doc.validate().ok());
}

void testMaskCycleIsDetected() {
    BrushDocument doc("Masks");
    doc.addResource(makeResource("p1", 4, 4));
    doc.addComponent(makeComponent("c1", "p1"));
    doc.addComponent(makeComponent("c2", "p1"));
    doc.component("c1")->maskComponentId = "c2";
    doc.component("c2")->maskComponentId = "c1";
    const ValidationResult result = doc.validate();
    CHECK(!result.ok());
    CHECK(hasIssue(result, "mask-cycle"));

    // A one-way mask is fine.
    doc.component("c2")->maskComponentId.clear();
    CHECK(doc.validate().ok());
}

void testRemovingAMaskedComponent() {
    BrushDocument doc("Cascade");
    doc.addResource(makeResource("p1", 4, 4));
    doc.addComponent(makeComponent("c1", "p1"));
    doc.addComponent(makeComponent("c2", "p1"));
    doc.component("c1")->maskComponentId = "c2";

    // Refused without cascade: a mask cannot be left dangling.
    CHECK(!doc.removeComponent("c2", false));
    CHECK(doc.component("c2") != nullptr);

    // With cascade the dependent goes too.
    CHECK(doc.removeComponent("c2", true));
    CHECK(doc.component("c2") == nullptr);
    CHECK(doc.component("c1") == nullptr);
}

void testUnknownAndOutOfRangeParametersAreRejected() {
    BrushDocument doc("Params");
    doc.addResource(makeResource("p1", 4, 4));
    doc.addComponent(makeComponent("c1", "p1"));

    CHECK(doc.setParameter("size", 64.0));
    CHECK(!doc.setParameter("not-a-parameter", 1.0));
    // The setter refuses a brush-scope violation outright.
    CHECK(!doc.setParameter("component-opacity", 50.0));

    // A bag written behind the setter's back is caught by validation.
    doc.components[0].parameters.set("nonsense", 1.0);
    ValidationResult result = doc.validate();
    CHECK(!result.ok());
    CHECK(hasIssue(result, "unknown-parameter"));

    doc.components[0].parameters.clear();
    doc.parameters.set("size", 99999.0);
    result = doc.validate();
    CHECK(!result.ok());
    CHECK(hasIssue(result, "parameter-out-of-range"));
}

void testValidationRejectsInvalidComponentSettings() {
    BrushDocument doc("Invalid component settings");
    doc.addResource(makeResource("p1", 4, 4));
    auto original = makeComponent("c1", "p1");
    CHECK(doc.addComponent(original));
    CHECK(doc.validate().ok());
    auto& c = doc.components.front();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    c.opacity = nan;
    CHECK(hasIssue(doc.validate(), "opacity-range"));
    c = original;
    c.transform.rotationDegrees = inf;
    CHECK(hasIssue(doc.validate(), "invalid-transform"));
    c = original;
    c.transform.scale = 0.0;
    CHECK(hasIssue(doc.validate(), "invalid-transform"));
    c = original;
    c.transform.offsetY = nan;
    CHECK(hasIssue(doc.validate(), "invalid-transform"));
    c = original;
    c.blend = static_cast<ComponentBlend>(99);
    CHECK(hasIssue(doc.validate(), "invalid-blend"));
    c = original;
    c.kind = ComponentKind::ProceduralShape;
    c.parameters.set("tip-shape", 2.5);
    CHECK(hasIssue(doc.validate(), "invalid-tip-shape"));
}

void testWeightedGroupNeedsWeight() {
    BrushDocument doc("Weighted");
    doc.addResource(makeResource("p1", 4, 4));
    ComponentGroup g;
    g.id = "g";
    g.mode = SelectionMode::Weighted;
    doc.addGroup(g);
    BrushComponent c = makeComponent("c1", "p1");
    c.weight = 0.0;
    doc.addComponent(c);
    doc.addToGroup("g", "c1");

    ValidationResult result = doc.validate();
    CHECK(!result.ok());
    CHECK(hasIssue(result, "weighted-empty"));

    doc.component("c1")->weight = 3.0;
    CHECK(doc.validate().ok());
}

void testResourceAccounting() {
    BrushDocument doc("Resources");
    CHECK(doc.addResource(makeResource("p1", 4, 4)));
    // Wrong byte count is refused at insertion rather than corrupting a file.
    BrushResource bad = makeResource("p2", 4, 4);
    bad.pixels.resize(3);
    CHECK(!doc.addResource(bad));

    doc.addComponent(makeComponent("c1", "p1"));
    doc.addComponent(makeComponent("c2", "p1"));
    CHECK(doc.resourceUsage("p1") == 2);
    CHECK(doc.resourceBytes() == 16);

    // Two components, one copy of the pixels.
    CHECK(doc.resources.size() == 1);
    CHECK(doc.validate().ok());

    // An unused resource is a warning, not a failure: deletion may be staged.
    doc.addResource(makeResource("p3", 2, 2));
    const ValidationResult result = doc.validate();
    CHECK(result.ok());
    CHECK(result.hasWarnings());
    CHECK(hasIssue(result, "unused-resource"));
}

void testDynamicsAndRandomizationReplaceRatherThanAccumulate() {
    BrushDocument doc("Replace");
    doc.addResource(makeResource("p1", 4, 4));
    doc.addComponent(makeComponent("c1", "p1"));

    DynamicsBinding binding;
    binding.parameter = "size";
    binding.signal = Signal::Pressure;
    binding.amount = 0.5;
    CHECK(doc.bindDynamics(binding));
    binding.amount = 0.9;
    CHECK(doc.bindDynamics(binding));
    CHECK(doc.dynamics.size() == 1);
    CHECK(doc.dynamics[0].amount > 0.89);

    RandomizationRule rule;
    rule.parameter = "rotation-offset";
    rule.minimum = -10.0;
    rule.maximum = 10.0;
    CHECK(doc.addRandomization(rule));
    CHECK(doc.addRandomization(rule));
    // addRandomization replaces a rule for the same parameter instead of
    // storing two that would fight over it.
    CHECK(doc.randomization.size() == 1);

    rule.minimum = 20.0;
    rule.maximum = 10.0;
    CHECK(!doc.addRandomization(rule));   // inverted range refused
}

void testExposedPropertiesOrderingAndVisibility() {
    BrushDocument doc("Exposed");
    doc.addResource(makeResource("p1", 4, 4));
    doc.addComponent(makeComponent("c1", "p1"));

    ExposedProperty size;
    size.parameter = "size";
    size.section = "Shape";
    size.order = 2;
    ExposedProperty scatter;
    scatter.parameter = "scatter-x";
    scatter.section = "Shape";
    scatter.order = 1;
    ExposedProperty hidden;
    hidden.parameter = "opacity";
    hidden.section = "Shape";
    hidden.order = 0;
    hidden.visible = false;
    ExposedProperty other;
    other.parameter = "hardness";
    other.section = "Edge";
    other.order = 5;

    CHECK(doc.expose(size));
    CHECK(doc.expose(scatter));
    CHECK(doc.expose(hidden));
    CHECK(doc.expose(other));

    const auto visible = doc.visibleExposed();
    CHECK(visible.size() == 3);
    // Section first, then order within the section.
    CHECK(visible[0]->section == "Edge");
    CHECK(visible[1]->parameter == "scatter-x");
    CHECK(visible[2]->parameter == "size");

    CHECK(doc.unexpose("scatter-x"));
    CHECK(doc.visibleExposed().size() == 2);
    // Unknown names never enter the list.
    ExposedProperty bogus;
    bogus.parameter = "no-such-setting";
    CHECK(!doc.expose(bogus));
}

void testExposedPropertiesFollowTheGroupOrder() {
    // A property with no section of its own is shown under the group the
    // registry puts its setting in, and the groups appear in the panel's own
    // order -- not alphabetically, which would bury the tip under "Texture".
    BrushDocument doc("Grouped");
    doc.addResource(makeResource("p1", 4, 4));
    doc.addComponent(makeComponent("c1", "p1"));

    const char* keys[] = {"size", "hardness", "texture-strength", "scatter-x", "opacity"};
    int order = 0;
    for (const char* key : keys) {
        ExposedProperty property;
        property.parameter = key;
        property.order = order++;
        CHECK(doc.expose(property));
    }

    const auto visible = doc.visibleExposed();
    CHECK(visible.size() == 5);
    // size -> Size; hardness -> Brush Tip; texture-strength -> Texture;
    // scatter-x -> Scatter; opacity -> Opacity & Flow.
    CHECK(effectiveSection(*visible[0]) == "Brush Tip");
    CHECK(visible[0]->parameter == "hardness");
    CHECK(effectiveSection(*visible[1]) == "Size");
    CHECK(visible[1]->parameter == "size");
    CHECK(effectiveSection(*visible[2]) == "Opacity & Flow");
    CHECK(visible[2]->parameter == "opacity");
    CHECK(effectiveSection(*visible[3]) == "Scatter");
    CHECK(visible[3]->parameter == "scatter-x");
    CHECK(effectiveSection(*visible[4]) == "Texture");
    CHECK(visible[4]->parameter == "texture-strength");

    // A group the artist invented sorts after every built-in one.
    ExposedProperty custom;
    custom.parameter = "flow";
    custom.section = "My own group";
    custom.order = 0;
    CHECK(doc.expose(custom));
    const auto withCustom = doc.visibleExposed();
    CHECK(withCustom.back()->parameter == "flow");
    CHECK(effectiveSection(*withCustom.back()) == "My own group");
}

void testLegacyMigrationIsLosslessForTheOldFields() {
    LegacyBrush legacy;
    legacy.size = 42;
    legacy.opacity = 77;
    legacy.flow = 66;
    legacy.spacing = 12;
    legacy.hardness = 55;
    legacy.stabilization = 33;
    legacy.pressureSize = 90;
    legacy.pressureOpacity = 40;
    legacy.pressureFlow = 25;
    legacy.pressureGamma = 120;
    legacy.minSizeRatio = 22;
    legacy.tiltSize = 15;
    legacy.aspectRatio = 180;
    legacy.tipAngle = -30;
    legacy.followDirection = true;
    legacy.erase = false;
    legacy.realisticEngine = true;
    legacy.tipStyle = "bristle";
    legacy.paintLoad = 70;
    legacy.waterAmount = 44;

    const BrushDocument doc = BrushDocument::fromLegacy(legacy, "brush-legacy", "Legacy");
    CHECK(!doc.id.empty());
    CHECK(doc.name == "Legacy");

    // One procedural tip, one continuous generator: the smallest faithful
    // reading of a flat record.
    CHECK(doc.components.size() == 1);
    CHECK(doc.components[0].kind == ComponentKind::ProceduralShape);
    CHECK(doc.components[0].parameters.value("tip-shape") == 2.0);   // bristle
    CHECK(doc.generators.size() == 1);
    CHECK(doc.generators[0].kind == GeneratorKind::Continuous);
    CHECK(doc.engine == Engine::Realistic);
    CHECK(doc.dynamics.size() == 4);   // size/opacity/flow on pressure, size on tilt
    CHECK(doc.validate().ok());

    const LegacyBrush back = doc.toLegacy();
    CHECK(back.size == legacy.size);
    CHECK(back.opacity == legacy.opacity);
    CHECK(back.flow == legacy.flow);
    CHECK(back.spacing == legacy.spacing);
    CHECK(back.hardness == legacy.hardness);
    CHECK(back.stabilization == legacy.stabilization);
    CHECK(back.pressureSize == legacy.pressureSize);
    CHECK(back.pressureOpacity == legacy.pressureOpacity);
    CHECK(back.pressureFlow == legacy.pressureFlow);
    CHECK(back.pressureGamma == legacy.pressureGamma);
    CHECK(back.minSizeRatio == legacy.minSizeRatio);
    CHECK(back.tiltSize == legacy.tiltSize);
    CHECK(back.aspectRatio == legacy.aspectRatio);
    CHECK(back.tipAngle == legacy.tipAngle);
    CHECK(back.followDirection == legacy.followDirection);
    CHECK(back.tiltAngle == legacy.tiltAngle);
    CHECK(back.realisticEngine == legacy.realisticEngine);
    CHECK(back.tipStyle == legacy.tipStyle);
    CHECK(back.paintLoad == legacy.paintLoad);
    CHECK(back.waterAmount == legacy.waterAmount);
}

void testFoliageDocumentIsValid() {
    const BrushDocument doc = makeFoliageDocument();
    const ValidationResult result = doc.validate();
    if (!result.ok()) std::printf("foliage: %s\n", result.summary().c_str());
    CHECK(result.ok());
    CHECK(doc.components.size() == 5);
    CHECK(doc.groupMembers("leaves").size() == 5);
    CHECK(doc.resources.size() == 2);
    // Five leaves, two stored images: sharing works.
    CHECK(doc.resourceBytes() == 2 * 64);
}

void testResponseCurve() {
    ResponseCurve curve;
    CHECK(curve.evaluate(0.0) == 0.0);
    CHECK(curve.evaluate(1.0) == 1.0);
    CHECK(curve.evaluate(0.5) > 0.49 && curve.evaluate(0.5) < 0.51);

    // Points arrive unsorted and out of range; the curve normalises them.
    CHECK(curve.setPoints({{0.8, 1.0}, {0.2, 0.0}, {1.4, 1.0}}));
    CHECK(curve.points().size() == 3);
    CHECK(curve.points()[0].x == 0.2);
    CHECK(curve.evaluate(0.5) > 0.49 && curve.evaluate(0.5) < 0.51);
    CHECK(curve.evaluate(-3.0) == 0.0);   // clamped from below
    CHECK(curve.evaluate(7.0) == 1.0);    // clamped from above

    // A curve needs at least two points.
    CHECK(!curve.setPoints({{0.5, 0.5}}));
}

} // namespace

int main() {
    testIdsAreUniqueAndAllocated();
    testOrdering();
    testGroupsAndMembershipStayConsistent();
    testGroupMembershipMismatchIsAnError();
    testMembershipInsertionReorderingAndRepair();
    testRemovingAComponentDetachesItFromGroups();
    testMaskCycleIsDetected();
    testRemovingAMaskedComponent();
    testUnknownAndOutOfRangeParametersAreRejected();
    testValidationRejectsInvalidComponentSettings();
    testWeightedGroupNeedsWeight();
    testResourceAccounting();
    testDynamicsAndRandomizationReplaceRatherThanAccumulate();
    testExposedPropertiesOrderingAndVisibility();
    testExposedPropertiesFollowTheGroupOrder();
    testLegacyMigrationIsLosslessForTheOldFields();
    testFoliageDocumentIsValid();
    testResponseCurve();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
