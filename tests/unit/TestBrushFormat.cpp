// The native .kb container: round-trip, determinism and every way a file can
// be wrong. Qt-free, so it runs with nothing but a C++20 compiler.
#include "brush/BrushFormat.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

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

constexpr std::size_t kHeader = 16;

void setU16(std::vector<std::uint8_t>* bytes, std::size_t offset, std::uint16_t value) {
    (*bytes)[offset] = std::uint8_t(value & 0xFF);
    (*bytes)[offset + 1] = std::uint8_t((value >> 8) & 0xFF);
}

void setU32(std::vector<std::uint8_t>* bytes, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        (*bytes)[offset + std::size_t(i)] = std::uint8_t((value >> (8 * i)) & 0xFF);
    }
}

// Recomputes the header checksum after a deliberate edit, so a test can reach
// the parser's structural checks instead of stopping at the CRC.
void reseal(std::vector<std::uint8_t>* bytes) {
    const std::uint32_t crc =
        crc32(bytes->data() + kHeader, bytes->size() - kHeader);
    setU32(bytes, 12, crc);
}

void appendChunk(std::vector<std::uint8_t>* bytes, const char tag[5],
                 const std::vector<std::uint8_t>& payload) {
    bytes->insert(bytes->end(), tag, tag + 4);
    const std::size_t lengthOffset = bytes->size();
    for (int i = 0; i < 4; ++i) bytes->push_back(0);
    setU32(bytes, lengthOffset, static_cast<std::uint32_t>(payload.size()));
    bytes->insert(bytes->end(), payload.begin(), payload.end());
}

BrushDocument makeDocument() {
    BrushDocument doc("Wet Foliage");
    doc.id = "brush-wet-foliage";
    doc.description = "Leaves, branches and a grain layer, weighted.";
    doc.engine = Engine::Realistic;
    doc.category = "Foliage";
    doc.tags = {"organic", "leaves"};
    doc.seed = 0xDEADBEEF;

    BrushResource leaf;
    leaf.id = "p1";
    leaf.name = "leaf";
    leaf.kind = ResourceKind::Grayscale8;
    leaf.width = 4;
    leaf.height = 4;
    leaf.pixels.assign(16, 0x90);
    doc.addResource(leaf);

    BrushResource grain;
    grain.id = "p2";
    grain.name = "grain";
    grain.kind = ResourceKind::Rgba8;
    grain.width = 2;
    grain.height = 2;
    grain.pixels.assign(16, 0x40);
    doc.addResource(grain);

    ComponentGroup leaves;
    leaves.id = "g1";
    leaves.name = "Leaves";
    leaves.mode = SelectionMode::Weighted;
    leaves.seed = 77;
    doc.addGroup(leaves);

    for (int i = 0; i < 3; ++i) {
        BrushComponent c;
        c.id = "c" + std::to_string(i + 1);
        c.name = "leaf " + std::to_string(i + 1);
        c.kind = ComponentKind::Image;
        c.resourceId = "p1";
        c.weight = 2.0 + static_cast<double>(i);
        c.transform.rotationDegrees = 15.0 * i;
        c.transform.scale = 1.0 + 0.1 * i;
        c.blend = ComponentBlend::Multiply;
        c.parameters.set("component-opacity", 80.0);
        doc.addComponent(c);
        doc.addToGroup("g1", c.id);
    }

    BrushComponent texture;
    texture.id = "c4";
    texture.name = "grain";
    texture.kind = ComponentKind::Texture;
    texture.resourceId = "p2";
    texture.blend = ComponentBlend::Overlay;
    texture.visible = false;
    doc.addComponent(texture);

    BrushGenerator stamp;
    stamp.id = "n1";
    stamp.kind = GeneratorKind::Stamp;
    stamp.groupId = "g1";
    stamp.parameters.set("scatter-x", 40.0);
    doc.addGenerator(stamp);

    BrushGenerator particle;
    particle.id = "n2";
    particle.kind = GeneratorKind::Particle;
    particle.enabled = false;
    doc.addGenerator(particle);

    DynamicsBinding binding;
    binding.parameter = "size";
    binding.signal = Signal::Pressure;
    binding.amount = 0.75;
    binding.curve = ResponseCurve({{0.0, 0.1}, {0.5, 0.6}, {1.0, 1.0}});
    doc.bindDynamics(binding);

    RandomizationRule rule;
    rule.parameter = "rotation-offset";
    rule.enabled = true;
    rule.minimum = -35.0;
    rule.maximum = 35.0;
    rule.distribution = Distribution::Triangular;
    rule.seed = 4242;
    doc.addRandomization(rule);

    ExposedProperty exposed;
    exposed.parameter = "size";
    exposed.label = "Leaf size";
    exposed.section = "Foliage";
    exposed.control = ControlType::Slider;
    exposed.hasRangeOverride = true;
    exposed.minimumOverride = 8.0;
    exposed.maximumOverride = 400.0;
    doc.expose(exposed);

    doc.setParameter("size", 180.0);
    doc.setParameter("opacity", 85.0);
    doc.setParameter("water-amount", 30.0);
    return doc;
}

void testRoundTrip() {
    const BrushDocument original = makeDocument();
    CHECK(original.validate().ok());

    const std::vector<std::uint8_t> bytes = encodeBrush(original);
    CHECK(bytes.size() > kHeader);

    BrushDocument restored;
    DecodeResult result;
    CHECK(decodeBrush(bytes.data(), bytes.size(), &restored, &result));
    CHECK(result.ok);
    CHECK(result.error.empty());
    CHECK(result.ignoredChunks.empty());
    CHECK(result.version == kBrushFormatVersion);

    // The whole construction survives, value for value.
    CHECK(restored == original);
    CHECK(restored.components.size() == 4);
    CHECK(restored.groups.size() == 1);
    CHECK(restored.generators.size() == 2);
    CHECK(restored.dynamics.size() == 1);
    CHECK(restored.randomization.size() == 1);
    CHECK(restored.exposed.size() == 1);
    CHECK(restored.resources.size() == 2);
    CHECK(restored.resources[1].kind == ResourceKind::Rgba8);
    CHECK(restored.resources[1].pixels.size() == 16);
    CHECK(restored.engine == Engine::Realistic);
    CHECK(restored.seed == 0xDEADBEEF);
    CHECK(restored.dynamics[0].curve.points().size() == 3);
    CHECK(restored.component("c4")->visible == false);
    CHECK(restored.component("c3")->transform.rotationDegrees == 30.0);
}

void testDeterministicBytes() {
    const BrushDocument doc = makeDocument();
    const auto first = encodeBrush(doc);
    const auto second = encodeBrush(doc);
    CHECK(first == second);

    // A decoded document re-encodes to exactly the same bytes.
    BrushDocument restored;
    CHECK(decodeBrush(first.data(), first.size(), &restored, nullptr));
    const auto third = encodeBrush(restored);
    CHECK(third == first);
}

void testRejectsForeignData() {
    const char junk[32] = "definitely not a brush file";
    BrushDocument doc;
    DecodeResult result;
    CHECK(!decodeBrush(reinterpret_cast<const std::uint8_t*>(junk), sizeof(junk), &doc,
                       &result));
    CHECK(result.error.rfind("not-a-brush", 0) == 0);
    CHECK(!result.ok);
}

void testRejectsTruncatedFile() {
    auto bytes = encodeBrush(makeDocument());
    BrushDocument doc;
    DecodeResult result;

    // Anything shorter than the header cannot even be identified.
    CHECK(!decodeBrush(bytes.data(), 8, &doc, &result));
    CHECK(result.error.rfind("truncated", 0) == 0);

    // A payload chopped in half fails the checksum rather than parsing halfway.
    bytes.resize(bytes.size() / 2);
    CHECK(!decodeBrush(bytes.data(), bytes.size(), &doc, &result));
    CHECK(result.error.rfind("checksum", 0) == 0);
}

void testRejectsCorruptByte() {
    auto bytes = encodeBrush(makeDocument());
    bytes[bytes.size() - 1] ^= 0xFF;   // flip a byte inside the payload
    BrushDocument doc;
    DecodeResult result;
    CHECK(!decodeBrush(bytes.data(), bytes.size(), &doc, &result));
    CHECK(result.error.rfind("checksum", 0) == 0);
}

void testRejectsFutureVersion() {
    auto bytes = encodeBrush(makeDocument());
    setU16(&bytes, 8, static_cast<std::uint16_t>(kBrushFormatVersion + 7));
    BrushDocument doc;
    DecodeResult result;
    CHECK(!decodeBrush(bytes.data(), bytes.size(), &doc, &result));
    CHECK(result.error.rfind("unsupported-version", 0) == 0);
    CHECK(result.version == kBrushFormatVersion + 7);
    CHECK(doc.components.empty());   // nothing was half-loaded
}

void testRejectsImpossibleChunkLength() {
    auto bytes = encodeBrush(makeDocument());
    // The first chunk starts right after the header: tag[4] then u32 length.
    setU32(&bytes, kHeader + 4, 0xFFFFFFF0u);
    reseal(&bytes);
    BrushDocument doc;
    DecodeResult result;
    CHECK(!decodeBrush(bytes.data(), bytes.size(), &doc, &result));
    CHECK(result.error.rfind("truncated-chunk", 0) == 0);
}

void testSkipsUnknownChunk() {
    auto bytes = encodeBrush(makeDocument());
    appendChunk(&bytes, "XTRA", {1, 2, 3, 4, 5});
    reseal(&bytes);

    BrushDocument doc;
    DecodeResult result;
    CHECK(decodeBrush(bytes.data(), bytes.size(), &doc, &result));
    CHECK(result.ok);
    CHECK(result.ignoredChunks.size() == 1);
    CHECK(result.ignoredChunks[0] == "XTRA");
    // Skipping an unknown chunk does not damage what we do understand.
    CHECK(doc.components.size() == 4);
}

void testRejectsDuplicateChunk() {
    auto bytes = encodeBrush(makeDocument());
    appendChunk(&bytes, "EXPO", {0, 0, 0, 0});
    reseal(&bytes);
    BrushDocument doc;
    DecodeResult result;
    CHECK(!decodeBrush(bytes.data(), bytes.size(), &doc, &result));
    CHECK(result.error.rfind("duplicate-chunk", 0) == 0);
}

void testRejectsIncoherentBrush() {
    // A document that encodes fine but describes an impossible brush: the
    // component points at a resource that is not in the file.
    BrushDocument broken("Broken");
    broken.id = "brush-broken";
    BrushComponent c;
    c.id = "c1";
    c.kind = ComponentKind::Tip;
    c.resourceId = "nowhere";
    broken.addComponent(c);

    const std::vector<std::uint8_t> bytes = encodeBrush(broken);
    BrushDocument doc;
    DecodeResult result;
    CHECK(!decodeBrush(bytes.data(), bytes.size(), &doc, &result));
    CHECK(result.error.rfind("invalid-brush", 0) == 0);
}

void testFailedDecodeLeavesTargetUntouched() {
    BrushDocument target = makeDocument();
    const std::string originalName = target.name;
    auto bytes = encodeBrush(makeDocument());
    bytes[bytes.size() - 1] ^= 0xFF;

    DecodeResult result;
    CHECK(!decodeBrush(bytes.data(), bytes.size(), &target, &result));
    CHECK(target.name == originalName);
    CHECK(target.components.size() == 4);
}

void testFileRoundTrip() {
    // Written next to the running test rather than in a hard-coded directory:
    // there is no /tmp on Windows, and CI proved it.
    namespace fs = std::filesystem;
    const fs::path path = fs::current_path() / "komapaint-brush-roundtrip.kb";
    const fs::path absent = fs::current_path() / "komapaint-brush-does-not-exist.kb";

    const BrushDocument original = makeDocument();
    std::string error;
    CHECK(saveBrushFile(path.string(), original, &error));

    BrushDocument loaded;
    DecodeResult result;
    std::string loadError;
    CHECK(loadBrushFile(path.string(), &loaded, &result, &loadError));
    CHECK(loaded == original);

    // Loading a file that is not there reports a reason instead of crashing.
    BrushDocument missing;
    CHECK(!loadBrushFile(absent.string(), &missing, &result, &loadError));
    CHECK(!loadError.empty());

    std::error_code ignored;
    fs::remove(path, ignored);
}

void testNoArbitraryComponentLimit() {
    // Part "unlimited creative freedom": nothing in the model caps counts, so
    // prove a large brush survives a round-trip rather than assuming it.
    BrushDocument doc("Five hundred leaves");
    doc.id = "brush-many";
    BrushResource leaf;
    leaf.id = "p1";
    leaf.width = 2;
    leaf.height = 2;
    leaf.pixels.assign(4, 0x70);
    doc.addResource(leaf);

    ComponentGroup group;
    group.id = "g1";
    group.mode = SelectionMode::Sequence;
    doc.addGroup(group);

    constexpr int kCount = 500;
    for (int i = 0; i < kCount; ++i) {
        BrushComponent c;
        c.id = "c" + std::to_string(i + 1);
        c.kind = ComponentKind::Image;
        c.resourceId = "p1";
        c.weight = 1.0 + double(i % 7);
        doc.addComponent(c);
        doc.addToGroup("g1", c.id);
    }

    CHECK(doc.components.size() == kCount);
    CHECK(doc.group("g1")->members.size() == kCount);
    // One stored image for five hundred references.
    CHECK(doc.resources.size() == 1);
    CHECK(doc.validate().ok());

    const auto bytes = encodeBrush(doc);
    BrushDocument restored;
    CHECK(decodeBrush(bytes.data(), bytes.size(), &restored, nullptr));
    CHECK(restored == doc);
}

void testRandomCorruptionNeverCrashes() {
    // A file arrives from the network or a mail attachment. Half of these will
    // be rejected at the checksum, and the rest must be rejected by the
    // structural checks -- what matters is that none of them crash or hang.
    const auto clean = encodeBrush(makeDocument());
    std::srand(20261117);
    int accepted = 0;
    for (int iteration = 0; iteration < 400; ++iteration) {
        std::vector<std::uint8_t> bytes = clean;
        const int flips = 1 + std::rand() % 4;
        for (int f = 0; f < flips; ++f) {
            const std::size_t at = static_cast<std::size_t>(std::rand()) % bytes.size();
            bytes[at] ^= static_cast<std::uint8_t>(1 + std::rand() % 255);
        }
        // Occasionally let the corruption through the checksum, so the chunk
        // parser itself is the thing under test.
        if (iteration % 3 == 0) reseal(&bytes);

        BrushDocument doc;
        DecodeResult result;
        const bool ok = decodeBrush(bytes.data(), bytes.size(), &doc, &result);
        if (ok) {
            ++accepted;
            // If it was accepted, it must at least be a coherent brush.
            CHECK(doc.validate().ok());
        } else {
            CHECK(!result.error.empty());
        }
    }
    // Corrupted files were overwhelmingly rejected; a couple may repair to a
    // valid brush by luck, but not many.
    CHECK(accepted < 40);
}

void testEmptyDocumentIsStorable() {
    // A brush being authored may legitimately be empty; that is a warning, not
    // a corruption, and it must not block saving a draft.
    BrushDocument empty("Draft");
    empty.id = "brush-draft";
    CHECK(empty.validate().ok());       // only warnings
    const auto bytes = encodeBrush(empty);
    BrushDocument restored;
    DecodeResult result;
    CHECK(decodeBrush(bytes.data(), bytes.size(), &restored, &result));
    CHECK(restored.name == "Draft");
}

} // namespace

int main() {
    testRoundTrip();
    testDeterministicBytes();
    testRejectsForeignData();
    testRejectsTruncatedFile();
    testRejectsCorruptByte();
    testRejectsFutureVersion();
    testRejectsImpossibleChunkLength();
    testSkipsUnknownChunk();
    testRejectsDuplicateChunk();
    testRejectsIncoherentBrush();
    testFailedDecodeLeavesTargetUntouched();
    testFileRoundTrip();
    testNoArbitraryComponentLimit();
    testRandomCorruptionNeverCrashes();
    testEmptyDocumentIsStorable();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
