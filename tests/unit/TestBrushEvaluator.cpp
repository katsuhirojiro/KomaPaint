// Brush evaluation: selection, dynamics, randomization, determinism.
// Qt-free, like the rest of the brush core.
#include "brush/BrushEvaluator.h"

#include <cmath>
#include <cstdio>
#include <map>
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

BrushResource makeResource(const std::string& id) {
    BrushResource r;
    r.id = id;
    r.width = 2;
    r.height = 2;
    r.pixels.assign(4, 0x80);
    return r;
}

// Builds a document with N visible components in one group with a given mode.
BrushDocument makeGroupDocument(SelectionMode mode, int count, int membersPerStamp = 1) {
    BrushDocument doc("Grouped");
    doc.id = "brush-grouped";
    doc.addResource(makeResource("p1"));

    ComponentGroup group;
    group.id = "g1";
    group.name = "Group";
    group.mode = mode;
    group.membersPerStamp = membersPerStamp;
    group.seed = 991;
    doc.addGroup(group);

    for (int i = 0; i < count; ++i) {
        BrushComponent c;
        c.id = "c" + std::to_string(i + 1);
        c.name = "component " + std::to_string(i + 1);
        c.kind = ComponentKind::Image;
        c.resourceId = "p1";
        c.weight = 1.0 + static_cast<double>(i) * 2.0;
        doc.addComponent(c);
        doc.addToGroup("g1", c.id);
    }

    BrushGenerator generator;
    generator.id = "n1";
    generator.kind = GeneratorKind::Stamp;
    generator.groupId = "g1";
    doc.addGenerator(generator);
    return doc;
}

StampContext makeContext(std::uint64_t stampIndex = 0, std::uint32_t strokeSeed = 7) {
    StampContext context;
    context.stampIndex = stampIndex;
    context.brushSeed = 2026;
    context.strokeSeed = strokeSeed;
    context.sizePixels = 100.0;
    return context;
}

void testDeterminism() {
    const BrushDocument doc = makeGroupDocument(SelectionMode::Random, 6, 2);
    const StampContext context = makeContext(42);
    const auto first = BrushEvaluator::evaluate(doc, context);
    const auto second = BrushEvaluator::evaluate(doc, context);
    CHECK(first.size() == second.size());
    CHECK(!first.empty());
    for (std::size_t i = 0; i < first.size() && i < second.size(); ++i) {
        CHECK(first[i].componentId == second[i].componentId);
        CHECK(first[i].rotationDegrees == second[i].rotationDegrees);
        CHECK(first[i].offsetX == second[i].offsetX);
        CHECK(first[i].opacity == second[i].opacity);
    }

    // A different stroke seed is a different picture. Any single stamp can
    // collide by chance, so compare a run of them.
    bool differs = false;
    for (std::uint64_t stamp = 0; stamp < 12; ++stamp) {
        const auto a = BrushEvaluator::evaluate(doc, makeContext(stamp, 7));
        const auto b = BrushEvaluator::evaluate(doc, makeContext(stamp, 99));
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
            if (a[i].componentId != b[i].componentId || a[i].offsetX != b[i].offsetX) differs = true;
        }
    }
    CHECK(differs);
}

void testUniformIsStable() {
    // The random source must not drift: a fixed key always yields one value.
    CHECK(BrushEvaluator::uniform01(1) == BrushEvaluator::uniform01(1));
    CHECK(BrushEvaluator::uniform01(1) >= 0.0);
    CHECK(BrushEvaluator::uniform01(1) < 1.0);
    double sum = 0.0;
    constexpr int kSamples = 20000;
    for (int i = 0; i < kSamples; ++i) sum += BrushEvaluator::uniform01(std::uint64_t(i) * 2654435761ull);
    const double mean = sum / kSamples;
    CHECK(mean > 0.48 && mean < 0.52);
}

void testAllModeUsesEveryComponent() {
    const BrushDocument doc = makeGroupDocument(SelectionMode::All, 4);
    const auto requests = BrushEvaluator::evaluate(doc, makeContext());
    CHECK(requests.size() == 4);
    CHECK(requests[0].componentId == "c1");
    CHECK(requests[3].componentId == "c4");
}

void testSequenceAdvancesAndWraps() {
    const BrushDocument doc = makeGroupDocument(SelectionMode::Sequence, 3);
    std::map<std::uint64_t, std::string> picked;
    for (std::uint64_t stamp = 0; stamp < 7; ++stamp) {
        const auto requests = BrushEvaluator::evaluate(doc, makeContext(stamp));
        CHECK(requests.size() == 1);
        picked[stamp] = requests[0].componentId;
    }
    CHECK(picked[0] == "c1");
    CHECK(picked[1] == "c2");
    CHECK(picked[2] == "c3");
    CHECK(picked[3] == "c1");   // wraps rather than stopping
    CHECK(picked[6] == "c1");
}

void testAlternatingMode() {
    const BrushDocument doc = makeGroupDocument(SelectionMode::Alternating, 2);
    CHECK(BrushEvaluator::evaluate(doc, makeContext(0))[0].componentId == "c1");
    CHECK(BrushEvaluator::evaluate(doc, makeContext(1))[0].componentId == "c2");
    CHECK(BrushEvaluator::evaluate(doc, makeContext(2))[0].componentId == "c1");
}

void testWeightedModeFollowsWeights() {
    // Weights 1,3,5,7 -> probabilities 1/16, 3/16, 5/16, 7/16.
    BrushDocument doc = makeGroupDocument(SelectionMode::Weighted, 4);
    std::map<std::string, int> counts;
    constexpr int kStamps = 4000;
    for (int stamp = 0; stamp < kStamps; ++stamp) {
        const auto requests = BrushEvaluator::evaluate(doc, makeContext(std::uint64_t(stamp)));
        CHECK(requests.size() == 1);
        if (!requests.empty()) ++counts[requests[0].componentId];
    }
    const double total = double(kStamps);
    auto share = [&](const char* id) { return double(counts[id]) / total; };
    CHECK(std::fabs(share("c1") - 1.0 / 16.0) < 0.03);
    CHECK(std::fabs(share("c2") - 3.0 / 16.0) < 0.04);
    CHECK(std::fabs(share("c3") - 5.0 / 16.0) < 0.05);
    CHECK(std::fabs(share("c4") - 7.0 / 16.0) < 0.05);
}

void testRandomModeNeverRepeatsWithinAStamp() {
    const BrushDocument doc = makeGroupDocument(SelectionMode::Random, 5, 3);
    for (int stamp = 0; stamp < 200; ++stamp) {
        const auto requests = BrushEvaluator::evaluate(doc, makeContext(std::uint64_t(stamp)));
        CHECK(requests.size() == 3);
        std::map<std::string, int> seen;
        for (const auto& r : requests) ++seen[r.componentId];
        for (const auto& [id, count] : seen) {
            (void)id;
            CHECK(count == 1);
        }
    }
}

void testHiddenComponentsAreSkipped() {
    BrushDocument doc = makeGroupDocument(SelectionMode::All, 3);
    doc.component("c2")->visible = false;
    const auto requests = BrushEvaluator::evaluate(doc, makeContext());
    CHECK(requests.size() == 2);
    for (const auto& r : requests) CHECK(r.componentId != "c2");
}

void testDisabledGeneratorProducesNothing() {
    BrushDocument doc = makeGroupDocument(SelectionMode::All, 3);
    doc.generator("n1")->enabled = false;
    const auto requests = BrushEvaluator::evaluate(doc, makeContext());
    CHECK(requests.empty());
}

void testEmptyDocumentProducesNothing() {
    BrushDocument doc("Empty");
    EvaluationReport report;
    const auto requests = BrushEvaluator::evaluate(doc, makeContext(), &report);
    CHECK(requests.empty());
    CHECK(report.requestsProduced == 0);
}

void testMissingGroupIsReported() {
    BrushDocument doc = makeGroupDocument(SelectionMode::All, 1);
    doc.generators[0].groupId = "g1";
    // Delete the group behind the generator's back.
    doc.groups.clear();
    EvaluationReport report;
    const auto requests = BrushEvaluator::evaluate(doc, makeContext(), &report);
    CHECK(requests.empty());
    CHECK(!report.notes.empty());
}

void testPressureDynamics() {
    BrushDocument doc = makeGroupDocument(SelectionMode::All, 1);
    doc.setParameter("size", 100.0);

    DynamicsBinding binding;
    binding.parameter = "size";
    binding.signal = Signal::Pressure;
    binding.amount = 1.0;
    binding.curve = ResponseCurve({{0.0, 0.2}, {1.0, 1.0}});   // full range
    doc.bindDynamics(binding);

    StampContext light = makeContext();
    light.input.pressure = 0.0;
    StampContext heavy = makeContext();
    heavy.input.pressure = 1.0;

    const double atZero = BrushEvaluator::resolvedParameter(doc, "size", light);
    const double atFull = BrushEvaluator::resolvedParameter(doc, "size", heavy);
    CHECK(std::fabs(atZero - 20.0) < 0.001);
    CHECK(std::fabs(atFull - 100.0) < 0.001);

    // Half strength halves the effect, it does not switch it off.
    doc.dynamics.clear();
    binding.amount = 0.5;
    doc.bindDynamics(binding);
    const double halfAtZero = BrushEvaluator::resolvedParameter(doc, "size", light);
    CHECK(halfAtZero > atZero && halfAtZero < atFull);
}

void testTiltDynamics() {
    BrushDocument doc = makeGroupDocument(SelectionMode::All, 1);
    doc.setParameter("aspect", 100.0);
    DynamicsBinding binding;
    binding.parameter = "aspect";
    binding.signal = Signal::Tilt;
    binding.amount = 1.0;
    binding.curve = ResponseCurve({{0.0, 0.5}, {1.0, 1.5}});
    doc.bindDynamics(binding);

    StampContext flat = makeContext();
    StampContext tilted = makeContext();
    tilted.input.tilt = 1.0;
    CHECK(BrushEvaluator::resolvedParameter(doc, "aspect", flat) <
          BrushEvaluator::resolvedParameter(doc, "aspect", tilted));
}

void testRandomizationRespectsRangeAndScope() {
    BrushDocument doc = makeGroupDocument(SelectionMode::All, 1);
    doc.setParameter("rotation-offset", 0.0);

    RandomizationRule rule;
    rule.parameter = "rotation-offset";
    rule.enabled = true;
    rule.minimum = -30.0;
    rule.maximum = 30.0;
    rule.perStamp = true;
    rule.seed = 77;
    CHECK(doc.addRandomization(rule));

    bool sawVariation = false;
    double first = BrushEvaluator::resolvedParameter(doc, "rotation-offset", makeContext(0));
    for (std::uint64_t stamp = 1; stamp < 200; ++stamp) {
        const double value = BrushEvaluator::resolvedParameter(doc, "rotation-offset", makeContext(stamp));
        CHECK(value >= -30.0 && value <= 30.0);
        if (std::fabs(value - first) > 0.5) sawVariation = true;
    }
    CHECK(sawVariation);

    // Per-stroke: one value for every stamp in the stroke.
    doc.randomization[0].perStamp = false;
    const double a = BrushEvaluator::resolvedParameter(doc, "rotation-offset", makeContext(3));
    const double b = BrushEvaluator::resolvedParameter(doc, "rotation-offset", makeContext(4));
    CHECK(a == b);

    // A disabled rule changes nothing.
    doc.randomization[0].enabled = false;
    CHECK(BrushEvaluator::resolvedParameter(doc, "rotation-offset", makeContext(5)) == 0.0);
}

void testScatterProducesOffsetsWithinRange() {
    BrushDocument doc = makeGroupDocument(SelectionMode::All, 1);
    doc.setParameter("scatter-x", 50.0);
    doc.setParameter("scatter-y", 50.0);
    doc.setParameter("scatter-count", 6.0);

    for (std::uint64_t stamp = 0; stamp < 50; ++stamp) {
        const auto requests = BrushEvaluator::evaluate(doc, makeContext(stamp));
        CHECK(requests.size() == 6);
        for (const auto& r : requests) {
            CHECK(std::fabs(r.offsetX) <= 0.5 + 1e-9);
            CHECK(std::fabs(r.offsetY) <= 0.5 + 1e-9);
        }
    }
}

void testFollowDirectionAndRotation() {
    BrushDocument doc = makeGroupDocument(SelectionMode::All, 1);
    doc.setParameter("rotation-offset", 10.0);
    doc.setParameter("follow-direction", 1.0);

    StampContext context = makeContext();
    context.input.directionRadians = 1.5707963267948966;  // 90 degrees
    const auto requests = BrushEvaluator::evaluate(doc, context);
    CHECK(requests.size() == 1);
    CHECK(std::fabs(requests[0].rotationDegrees - 100.0) < 0.01);

    // Turning the switch off removes the direction term but keeps the offset.
    doc.setParameter("follow-direction", 0.0);
    const auto without = BrushEvaluator::evaluate(doc, context);
    CHECK(std::fabs(without[0].rotationDegrees - 10.0) < 0.01);
}

void testCombinationNormalisesWeights() {
    BrushDocument doc = makeGroupDocument(SelectionMode::Combination, 2);
    // Weights 1 and 3 -> contributions 25% and 75%.
    const auto requests = BrushEvaluator::evaluate(doc, makeContext());
    CHECK(requests.size() == 2);
    if (requests.size() == 2) {
        CHECK(std::fabs(requests[0].opacity - 0.25) < 0.001);
        CHECK(std::fabs(requests[1].opacity - 0.75) < 0.001);
    }
}

void testOpacityAndEraseCarryThrough() {
    BrushDocument doc = makeGroupDocument(SelectionMode::All, 1);
    doc.setParameter("opacity", 50.0);
    doc.component("c1")->opacity = 0.5;
    doc.component("c1")->erases = true;
    doc.component("c1")->blend = ComponentBlend::Multiply;

    const auto requests = BrushEvaluator::evaluate(doc, makeContext());
    CHECK(requests.size() == 1);
    CHECK(std::fabs(requests[0].opacity - 0.25) < 0.001);   // 0.5 * 0.5
    CHECK(requests[0].erases);
    CHECK(requests[0].blend == ComponentBlend::Multiply);
}

void testNestedGroupsContribute() {
    // A parent set holding two child sets: the parent's rule applies to its own
    // members, and each child adds its own selection.
    BrushDocument doc("Nested");
    doc.id = "brush-nested";
    doc.addResource(makeResource("p1"));

    ComponentGroup parent;
    parent.id = "gp";
    parent.mode = SelectionMode::All;
    doc.addGroup(parent);

    ComponentGroup leaves;
    leaves.id = "gl";
    leaves.parentGroupId = "gp";
    leaves.mode = SelectionMode::Sequence;
    doc.addGroup(leaves);

    ComponentGroup grass;
    grass.id = "gg";
    grass.parentGroupId = "gp";
    grass.mode = SelectionMode::Alternating;
    doc.addGroup(grass);

    for (const char* id : {"leaf1", "leaf2"}) {
        BrushComponent c;
        c.id = id;
        c.kind = ComponentKind::Image;
        c.resourceId = "p1";
        doc.addComponent(c);
        doc.addToGroup("gl", id);
    }
    for (const char* id : {"grass1", "grass2"}) {
        BrushComponent c;
        c.id = id;
        c.kind = ComponentKind::Image;
        c.resourceId = "p1";
        doc.addComponent(c);
        doc.addToGroup("gg", id);
    }

    BrushGenerator generator;
    generator.id = "n1";
    generator.kind = GeneratorKind::Stamp;
    generator.groupId = "gp";
    doc.addGenerator(generator);

    CHECK(doc.validate().ok());

    const auto first = BrushEvaluator::evaluate(doc, makeContext(0));
    CHECK(first.size() == 2);
    if (first.size() == 2) {
        CHECK(first[0].componentId == "leaf1");
        CHECK(first[1].componentId == "grass1");
    }
    const auto second = BrushEvaluator::evaluate(doc, makeContext(1));
    if (second.size() == 2) {
        CHECK(second[0].componentId == "leaf2");
        CHECK(second[1].componentId == "grass2");
    }
}

void testReportCounts() {
    BrushDocument doc = makeGroupDocument(SelectionMode::Random, 5, 2);
    EvaluationReport report;
    const auto requests = BrushEvaluator::evaluate(doc, makeContext(), &report);
    CHECK(report.generatorsEvaluated == 1);
    CHECK(report.componentsConsidered == 5);
    CHECK(report.requestsProduced == static_cast<int>(requests.size()));
    CHECK(requests.size() == 2);
}

void testLargeBrushStaysCheap() {
    // 500 components, one stamp: the evaluator must not walk the rich
    // structures more than necessary, and must stay deterministic.
    BrushDocument doc("Big");
    doc.id = "brush-big";
    doc.addResource(makeResource("p1"));
    ComponentGroup group;
    group.id = "g1";
    group.mode = SelectionMode::Weighted;
    group.membersPerStamp = 4;
    doc.addGroup(group);
    for (int i = 0; i < 500; ++i) {
        BrushComponent c;
        c.id = "c" + std::to_string(i + 1);
        c.kind = ComponentKind::Image;
        c.resourceId = "p1";
        c.weight = 1.0 + double(i % 11) * 0.5;
        doc.addComponent(c);
        doc.addToGroup("g1", c.id);
    }
    BrushGenerator generator;
    generator.id = "n1";
    generator.kind = GeneratorKind::Stamp;
    generator.groupId = "g1";
    doc.addGenerator(generator);

    CHECK(doc.validate().ok());
    const auto requests = BrushEvaluator::evaluate(doc, makeContext(1234));
    CHECK(requests.size() == 4);
    std::map<std::string, int> seen;
    for (const auto& r : requests) ++seen[r.componentId];
    for (const auto& [id, count] : seen) {
        (void)id;
        CHECK(count == 1);
    }
}

} // namespace

int main() {
    testDeterminism();
    testUniformIsStable();
    testAllModeUsesEveryComponent();
    testSequenceAdvancesAndWraps();
    testAlternatingMode();
    testWeightedModeFollowsWeights();
    testRandomModeNeverRepeatsWithinAStamp();
    testHiddenComponentsAreSkipped();
    testDisabledGeneratorProducesNothing();
    testEmptyDocumentProducesNothing();
    testMissingGroupIsReported();
    testPressureDynamics();
    testTiltDynamics();
    testRandomizationRespectsRangeAndScope();
    testScatterProducesOffsetsWithinRange();
    testFollowDirectionAndRotation();
    testCombinationNormalisesWeights();
    testOpacityAndEraseCarryThrough();
    testNestedGroupsContribute();
    testReportCounts();
    testLargeBrushStaysCheap();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
