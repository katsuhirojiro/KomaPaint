// The document path against the flat path, on real pixels.
//
// The claim this file has to earn is narrow and absolute: a brush that was
// migrated into the .kb document model paints the *same pixels* as the brush
// record it came from, in both engines, for every value the old record could
// express. Not "similar", not "visually equivalent" -- identical, compared
// byte for byte on the tile store.
//
// The second half covers what the flat path could not do at all: several
// components per stamp, selection modes, per-component erasing, transform
// offsets, and reproducibility from a seed.
//
// Qt-free, like the engine it tests.
#include "brush/BrushFormat.h"
#include "brush/BrushModel.h"
#include "drawing/BrushDocumentAdapter.h"
#include "drawing/BrushEngine.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace komapaint;
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

constexpr std::int64_t kWidth = 96;
constexpr std::int64_t kHeight = 96;

// The dabs of one stroke, sampled along a diagonal so spacing, angle smoothing
// and smudge carry all take part.
std::vector<InputSample> strokePath() {
    std::vector<InputSample> path;
    for (int i = 0; i < 24; ++i) {
        InputSample s;
        s.x = 16.0 + double(i) * 2.4;
        s.y = 18.0 + double(i) * 1.7;
        s.pressure = 0.35 + 0.65 * (double(i) / 23.0);
        s.tiltX = 20.0;
        s.tiltY = -12.0;
        s.hasTilt = true;
        s.rotationRadians = 0.4;
        s.hasRotation = true;
        path.push_back(s);
    }
    return path;
}

std::vector<std::uint32_t> pixelsOf(const SparseTileStore& store) {
    std::vector<std::uint32_t> pixels;
    const auto& g = store.geometry();
    pixels.reserve(static_cast<std::size_t>(g.width() * g.height()));
    for (std::int64_t y = 0; y < g.height(); ++y) {
        for (std::int64_t x = 0; x < g.width(); ++x) {
            pixels.push_back(store.pixel({x, y}).value_or(0));
        }
    }
    return pixels;
}

std::uint64_t hashOf(const std::vector<std::uint32_t>& pixels) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::uint32_t p : pixels) {
        h ^= p;
        h *= 1099511628211ull;
    }
    return h;
}

// A plain, opaque coat so eraser strokes have something to remove.
bool primeWithPaint(SparseTileStore& store, std::uint32_t color) {
    BrushDefinition brush;
    brush.size = 30;
    brush.spacing = 0.10;   // fraction of the diameter, like every stroke
    brush.hardness = 1;
    brush.color = color;
    BrushStroke stroke(store, brush);
    if (stroke.failed()) return false;
    InputSample s;
    s.x = 30.0;
    s.y = 40.0;
    if (!stroke.begin(s)) return false;
    for (int i = 0; i < 20; ++i) {
        s.x += 3.0;
        s.y += 1.0;
        if (!stroke.extend(s)) return false;
    }
    return true;
}

int changedPixels(const std::vector<std::uint32_t>& pixels) {
    int changed = 0;
    for (std::uint32_t p : pixels) {
        if (p != 0) ++changed;
    }
    return changed;
}

// One legacy brush per behaviour the flat record can express.
std::vector<std::pair<std::string, LegacyBrush>> legacyBrushes() {
    std::vector<std::pair<std::string, LegacyBrush>> brushes;

    LegacyBrush pen;
    pen.size = 7;
    pen.opacity = 85;
    pen.flow = 70;
    pen.spacing = 8;
    pen.hardness = 90;
    pen.pressureSize = 60;
    pen.pressureOpacity = 90;
    brushes.emplace_back("pen", pen);

    LegacyBrush pencil;
    pencil.size = 4;
    pencil.opacity = 55;
    pencil.flow = 40;
    pencil.spacing = 10;
    pencil.hardness = 35;
    pencil.tipStyle = "graphite";
    pencil.pressureSize = 80;
    pencil.pressureGamma = 130;
    brushes.emplace_back("pencil", pencil);

    LegacyBrush chisel;
    chisel.size = 22;
    chisel.opacity = 100;
    chisel.flow = 100;
    chisel.spacing = 12;
    chisel.hardness = 100;
    chisel.aspectRatio = 260;
    chisel.tipAngle = 37;
    chisel.followDirection = true;
    brushes.emplace_back("chisel", chisel);

    LegacyBrush tilted;
    tilted.size = 18;
    tilted.spacing = 20;
    tilted.hardness = 60;
    tilted.tiltSize = 70;
    tilted.tiltAngle = true;
    tilted.stylusRotation = true;
    brushes.emplace_back("tilted", tilted);

    LegacyBrush smudge;
    smudge.size = 14;
    smudge.opacity = 60;
    smudge.flow = 45;
    smudge.spacing = 6;
    smudge.blendStrength = 70;
    smudge.colorMix = 40;
    brushes.emplace_back("smudge", smudge);

    LegacyBrush eraser;
    eraser.size = 12;
    eraser.spacing = 9;
    eraser.erase = true;
    brushes.emplace_back("eraser", eraser);

    LegacyBrush dry;
    dry.size = 26;
    dry.spacing = 14;
    dry.hardness = 20;
    dry.tipStyle = "textured";
    dry.pressureOpacity = 70;
    dry.pressureFlow = 80;
    brushes.emplace_back("dry", dry);

    LegacyBrush wide;
    wide.size = 40;
    wide.opacity = 30;
    wide.flow = 25;
    wide.spacing = 25;
    wide.hardness = 5;
    wide.minSizeRatio = 40;
    wide.pressureSize = 100;
    brushes.emplace_back("wash", wide);

    return brushes;
}

// ---- flat definition fidelity ----------------------------------------------

void testDefinitionsMatchForEveryLegacyField() {
    for (const auto& [name, legacy] : legacyBrushes()) {
        StrokeRuntime runtime;
        runtime.color = 0xff3366cc;

        const BrushDefinition flat = drawing::digitalDefinition(legacy, runtime);
        const BrushDocument document = BrushDocument::fromLegacy(legacy, "brush-" + name, name);
        const BrushDefinition fromDocument =
            drawing::digitalDefinition(document, runtime);

        CHECK(flat.size == fromDocument.size);
        CHECK(flat.opacity == fromDocument.opacity);
        CHECK(flat.flow == fromDocument.flow);
        CHECK(flat.spacing == fromDocument.spacing);
        CHECK(flat.tip == fromDocument.tip);
        CHECK(flat.hardness == fromDocument.hardness);
        CHECK(flat.aspectRatio == fromDocument.aspectRatio);
        CHECK(flat.angleRadians == fromDocument.angleRadians);
        CHECK(flat.followDirection == fromDocument.followDirection);
        CHECK(flat.pressureSize == fromDocument.pressureSize);
        CHECK(flat.pressureOpacity == fromDocument.pressureOpacity);
        CHECK(flat.pressureFlow == fromDocument.pressureFlow);
        CHECK(flat.pressureGamma == fromDocument.pressureGamma);
        CHECK(flat.minSizeRatio == fromDocument.minSizeRatio);
        CHECK(flat.tiltSize == fromDocument.tiltSize);
        CHECK(flat.tiltAngle == fromDocument.tiltAngle);
        CHECK(flat.stylusRotation == fromDocument.stylusRotation);
        CHECK(flat.blendStrength == fromDocument.blendStrength);
        CHECK(flat.colorMix == fromDocument.colorMix);
        // The document keeps "this brush erases" on its component, so the flat
        // request is the definition's eraser *or* the component's.
        const bool documentErases = fromDocument.erase ||
            (!document.components.empty() && document.components.front().erases);
        CHECK(flat.erase == documentErases);
        CHECK(flat.color == fromDocument.color);
    }
}

void testEngineSwitchIsHonoured() {
    LegacyBrush realistic;
    realistic.realisticEngine = true;
    realistic.size = 30;
    StrokeRuntime runtime;
    const BrushDocument document = BrushDocument::fromLegacy(realistic, "brush-r", "R");
    CHECK(document.engine == Engine::Realistic);
    const BrushDefinition flat = drawing::digitalDefinition(realistic, runtime);
    const BrushDefinition projected = drawing::digitalDefinition(document, runtime);
    CHECK(flat.size == projected.size);
}

// ---- identical pixels -------------------------------------------------------

void testMigratedBrushPaintsIdenticalPixels() {
    const auto path = strokePath();
    for (const auto& [name, legacy] : legacyBrushes()) {
        SparseTileStore flatStore(kWidth, kHeight);
        SparseTileStore documentStore(kWidth, kHeight);

        StrokeRuntime runtime;
        runtime.color = 0xff2f9b6d;
        runtime.erase = false;

        if (legacy.erase) {
            // An eraser on blank canvas proves nothing, so both stores start
            // with the same coat of paint.
            CHECK(primeWithPaint(flatStore, runtime.color));
            CHECK(primeWithPaint(documentStore, runtime.color));
        }
        const auto before = pixelsOf(flatStore);
        BrushStroke flatStroke(flatStore, drawing::digitalDefinition(legacy, runtime));
        BrushDocument document = BrushDocument::fromLegacy(legacy, "brush-" + name, name);
        document.seed = 4242;
        drawing::DocumentStroke documentStroke(
            documentStore, document, drawing::digitalDefinition(document, runtime), 99u);

        CHECK(flatStroke.begin(path.front()));
        CHECK(documentStroke.begin(path.front()));
        for (std::size_t i = 1; i < path.size(); ++i) {
            CHECK(flatStroke.extend(path[i]));
            CHECK(documentStroke.extend(path[i]));
        }
        CHECK(!flatStroke.failed());
        CHECK(!documentStroke.failed());
        CHECK(!documentStroke.failed());

        const auto flatPixels = pixelsOf(flatStore);
        const auto documentPixels = pixelsOf(documentStore);
        // A brush must do something: deposit on blank canvas, or remove paint
        // where an eraser was pointed at a coat that already existed.
        if (legacy.erase) {
            CHECK(flatPixels != before);
        } else {
            if (changedPixels(flatPixels) == 0) {
                std::printf("brush '%s' painted nothing\n", name.c_str());
            }
            CHECK(changedPixels(flatPixels) > 0);
        }
        if (hashOf(flatPixels) != hashOf(documentPixels)) {
            std::printf("pixel mismatch for legacy brush '%s'\n", name.c_str());
        }
        CHECK(hashOf(flatPixels) == hashOf(documentPixels));
        // One component, one dab per stamp: no extra work, no missing work.
        CHECK(documentStroke.dabCount() == documentStroke.stampCount());
        CHECK(documentStroke.stampCount() > 5);
    }
}

void testBothEnginesMigrateALegacyRecordTheSameWay() {
    // Every material number the flat record carries must survive the migration,
    // including the ones the digital engine ignores. The definitions are
    // compared field by field because the initial paint needs a loaded
    // reservoir and is covered separately.
    const auto presets = material::defaultBrushDefinitions();
    CHECK(!presets.empty());
    for (const auto& [name, legacy] : legacyBrushes()) {
        const BrushDocument document = BrushDocument::fromLegacy(legacy, "brush-" + name, name);
        for (const auto& preset : presets) {
            if (preset.initialPaint.pigments.empty()) continue;
            StrokeRuntime runtime;
            const auto flat = drawing::materialDefinition(preset, legacy, runtime);
            const auto projected = drawing::materialDefinition(preset, document, runtime);
            CHECK(flat.size == projected.size);
            CHECK(flat.hardness == projected.hardness);
            CHECK(flat.spacingRatio == projected.spacingRatio);
            CHECK(flat.aspect == projected.aspect);
            CHECK(flat.rotationRadians == projected.rotationRadians);
            CHECK(flat.followDirection == projected.followDirection);
            CHECK(flat.initialLoad == projected.initialLoad);
            CHECK(flat.depletionRate == projected.depletionRate);
            CHECK(flat.pressureTransfer == projected.pressureTransfer);
            CHECK(flat.contactPressureScale == projected.contactPressureScale);
            CHECK(flat.paintBody == projected.paintBody);
            CHECK(flat.dryingRate == projected.dryingRate);
            CHECK(flat.edgeAccumulation == projected.edgeAccumulation);
            CHECK(flat.toothResponse == projected.toothResponse);
            CHECK(flat.granulationResponse == projected.granulationResponse);
            CHECK(flat.pickupRate == projected.pickupRate);
            CHECK(flat.paintLiftability == projected.paintLiftability);
            CHECK(flat.rewetting == projected.rewetting);
            CHECK(flat.releaseRate == projected.releaseRate);
            CHECK(flat.contactTransfer == projected.contactTransfer);
            CHECK(flat.autoReloadOnStroke == projected.autoReloadOnStroke);
            CHECK(flat.tip == projected.tip);
            CHECK(flat.bristleCount == projected.bristleCount);
            CHECK(flat.bristleSpread == projected.bristleSpread);
            CHECK(flat.textureStrength == projected.textureStrength);
            break;
        }
    }
}

void testInitialPaintMatches() {
    const auto presets = material::defaultBrushDefinitions();
    for (const auto& [name, legacy] : legacyBrushes()) {
        const BrushDocument document = BrushDocument::fromLegacy(legacy, "brush-" + name, name);
        for (const auto& preset : presets) {
            if (preset.initialPaint.pigments.empty()) continue;
            StrokeRuntime runtime;
            auto flat = drawing::materialDefinition(preset, legacy, runtime);
            auto projected = drawing::materialDefinition(preset, document, runtime);
            drawing::assignInitialPaint(&flat, legacy, 0xffa3421d, 0.8);
            drawing::assignInitialPaint(&projected, document, 0xffa3421d, 0.8);
            CHECK(flat.initialPaint.mediumId == projected.initialPaint.mediumId);
            CHECK(std::fabs(flat.initialPaint.pigmentMass() - projected.initialPaint.pigmentMass()) < 1e-12);
            CHECK(std::fabs(flat.initialPaint.water - projected.initialPaint.water) < 1e-12);
            CHECK(std::fabs(flat.initialPaint.binder - projected.initialPaint.binder) < 1e-12);
            CHECK(std::fabs(flat.initialPaint.filler - projected.initialPaint.filler) < 1e-12);
            break;
        }
    }
}

// ---- what the flat record could not do --------------------------------------

// Several components in one group, selected by a rule, all in one stroke.
BrushDocument multiComponentDocument(SelectionMode mode, int count, int perStamp,
                                     ComponentBlend blend = ComponentBlend::Normal) {
    BrushDocument doc("Multi");
    doc.id = "brush-multi";
    doc.seed = 777;
    doc.setParameter("size", 20.0);
    doc.setParameter("opacity", 90.0);
    doc.setParameter("flow", 80.0);
    doc.setParameter("spacing", 25.0);
    doc.setParameter("hardness", 80.0);

    ComponentGroup group;
    group.id = "g1";
    group.name = "Marks";
    group.mode = mode;
    group.membersPerStamp = perStamp;
    group.seed = 5;
    doc.addGroup(group);

    for (int i = 0; i < count; ++i) {
        BrushComponent component;
        component.id = "c" + std::to_string(i + 1);
        component.name = "mark " + std::to_string(i + 1);
        component.kind = ComponentKind::ProceduralShape;
        component.blend = blend;
        component.weight = 1.0 + double(i);
        component.transform.offsetX = -30.0 + 20.0 * double(i);
        component.transform.rotationDegrees = 15.0 * double(i);
        component.parameters.set("tip-shape", double(i % 5));
        doc.addComponent(component);
        doc.addToGroup("g1", component.id);
    }

    BrushGenerator generator;
    generator.id = "n1";
    generator.kind = GeneratorKind::Continuous;
    generator.groupId = "g1";
    doc.addGenerator(generator);
    return doc;
}

void testEveryComponentPaintsInAllMode() {
    BrushDocument document = multiComponentDocument(SelectionMode::All, 3, 1);
    SparseTileStore store(kWidth, kHeight);
    StrokeRuntime runtime;
    runtime.color = 0xffcc8844;
    drawing::DocumentStroke stroke(store, document,
                                   drawing::digitalDefinition(document, runtime), 3u);
    const auto path = strokePath();
    CHECK(stroke.begin(path.front()));
    for (std::size_t i = 1; i < path.size(); ++i) CHECK(stroke.extend(path[i]));
    CHECK(!stroke.failed());
    CHECK(stroke.dabCount() == stroke.stampCount() * 3);
    CHECK(changedPixels(pixelsOf(store)) > 0);
}

void testRandomSelectionIsReproducibleAndSeeded() {
    const BrushDocument document = multiComponentDocument(SelectionMode::Random, 5, 2);
    StrokeRuntime runtime;
    runtime.color = 0xff2255aa;
    const auto path = strokePath();

    auto paint = [&](std::uint32_t strokeSeed) {
        SparseTileStore store(kWidth, kHeight);
        drawing::DocumentStroke stroke(store, document,
                                       drawing::digitalDefinition(document, runtime), strokeSeed);
        stroke.begin(path.front());
        for (std::size_t i = 1; i < path.size(); ++i) stroke.extend(path[i]);
        return hashOf(pixelsOf(store));
    };

    const auto first = paint(11u);
    const auto second = paint(11u);
    const auto other = paint(12u);
    CHECK(first == second);   // same seeds, same picture
    CHECK(first != other);    // a new stroke is a new draw

    // With a brush whose random choice is bound to the stroke seed, the seed
    // must matter. Selection uses the brush seed, so instead check that the
    // grouping changed the picture at all against a plain brush.
    SparseTileStore plainStore(kWidth, kHeight);
    BrushDefinition flat = drawing::digitalDefinition(document, runtime);
    BrushStroke plain(plainStore, flat);
    plain.begin(path.front());
    for (std::size_t i = 1; i < path.size(); ++i) plain.extend(path[i]);
    CHECK(hashOf(pixelsOf(plainStore)) != first);
}

void testPerComponentErase() {
    BrushDocument document = multiComponentDocument(SelectionMode::All, 2, 1);
    StrokeRuntime runtime;
    runtime.color = 0xff000000;
    const auto path = strokePath();

    SparseTileStore store(kWidth, kHeight);
    drawing::DocumentStroke stroke(store, document,
                                   drawing::digitalDefinition(document, runtime), 5u);
    stroke.begin(path.front());
    for (std::size_t i = 1; i < path.size(); ++i) stroke.extend(path[i]);
    const auto before = pixelsOf(store);
    CHECK(changedPixels(before) > 0);

    // The same brush, with one component turned into an eraser, removes paint
    // instead of adding it -- from inside one stroke, which a flat brush has no
    // way to express.
    BrushDocument erasing = document;
    erasing.component("c1")->erases = true;
    SparseTileStore erasedStore(kWidth, kHeight);
    drawing::DocumentStroke erasingStroke(
        erasedStore, erasing, drawing::digitalDefinition(erasing, runtime), 5u);
    erasingStroke.begin(path.front());
    for (std::size_t i = 1; i < path.size(); ++i) erasingStroke.extend(path[i]);
    CHECK(changedPixels(pixelsOf(erasedStore)) != changedPixels(before));
}

void testHiddenComponentsDoNotPaint() {
    BrushDocument document = multiComponentDocument(SelectionMode::All, 3, 1);
    SparseTileStore store(kWidth, kHeight);
    StrokeRuntime runtime;
    drawing::DocumentStroke stroke(store, document,
                                   drawing::digitalDefinition(document, runtime), 1u);
    const auto path = strokePath();
    stroke.begin(path.front());
    for (std::size_t i = 1; i < path.size(); ++i) stroke.extend(path[i]);
    CHECK(stroke.dabCount() == stroke.stampCount() * 3);

    document.component("c2")->visible = false;
    SparseTileStore hiddenStore(kWidth, kHeight);
    drawing::DocumentStroke hidden(hiddenStore, document,
                                   drawing::digitalDefinition(document, runtime), 1u);
    hidden.begin(path.front());
    for (std::size_t i = 1; i < path.size(); ++i) hidden.extend(path[i]);
    CHECK(hidden.dabCount() == hidden.stampCount() * 2);
}

void testRandomizationReachesThePixels() {
    BrushDocument document = multiComponentDocument(SelectionMode::All, 1, 1);
    RandomizationRule rule;
    rule.parameter = "component-scale";
    rule.enabled = true;
    rule.minimum = -60.0;
    rule.maximum = 60.0;
    rule.perStamp = true;
    document.addRandomization(rule);

    StrokeRuntime runtime;
    runtime.color = 0xff118833;
    const auto path = strokePath();

    auto paint = [&](std::uint32_t strokeSeed) {
        SparseTileStore store(kWidth, kHeight);
        drawing::DocumentStroke stroke(store, document,
                                       drawing::digitalDefinition(document, runtime), strokeSeed);
        stroke.begin(path.front());
        for (std::size_t i = 1; i < path.size(); ++i) stroke.extend(path[i]);
        return hashOf(pixelsOf(store));
    };
    CHECK(paint(21u) == paint(21u));
    CHECK(paint(21u) != paint(22u));

    BrushDocument steady = document;
    steady.removeRandomization("component-scale");
    SparseTileStore steadyStore(kWidth, kHeight);
    drawing::DocumentStroke steadyStroke(
        steadyStore, steady, drawing::digitalDefinition(steady, runtime), 21u);
    steadyStroke.begin(path.front());
    for (std::size_t i = 1; i < path.size(); ++i) steadyStroke.extend(path[i]);
    CHECK(hashOf(pixelsOf(steadyStore)) != paint(21u));
}

BrushDocument bitmapDocument(ResourceKind kind, std::vector<std::uint8_t> pixels) {
    BrushDocument doc = multiComponentDocument(SelectionMode::All, 1, 1);
    doc.setParameter("size", 16.0);
    doc.setParameter("opacity", 100.0);
    doc.setParameter("flow", 100.0);
    doc.setParameter("hardness", 100.0);
    auto* tip = doc.component("c1");
    tip->transform = {};
    tip->parameters = {};
    tip->kind = kind == ResourceKind::Rgba8 ? ComponentKind::ColourImage : ComponentKind::Tip;
    tip->resourceId = "pixels";
    BrushResource resource;
    resource.id = tip->resourceId;
    resource.width = 2;
    resource.height = 1;
    resource.kind = kind;
    resource.setPixels(std::move(pixels));
    CHECK(doc.addResource(std::move(resource)));
    CHECK(drawing::digitalBrushIssue(doc).empty());
    return doc;
}

std::vector<std::uint32_t> paintNative(const BrushDocument& doc, bool artworkMask = false,
                                       bool tap = true) {
    const auto native = std::make_shared<const BrushDocument>(doc);
    SparseTileStore store(kWidth, kHeight);
    StrokeRuntime runtime{0xff2468ac, false};
    auto brush = drawing::digitalDefinition(*native, runtime);
    if (artworkMask) brush.pixelMask = [](void*, std::int64_t x, std::int64_t) noexcept {
        return x < 48 ? 0.5 : 0.0;
    };
    drawing::DocumentStroke stroke(store, native, brush, 541u);
    const InputSample p{48, 48, 1};
    CHECK(stroke.begin(p));
    if (!tap) {
        for (const auto& sample : strokePath()) CHECK(stroke.extend(sample));
    } else {
        CHECK(stroke.end(p));
    }
    CHECK(!stroke.failed());
    return pixelsOf(store);
}

std::uint32_t at(const std::vector<std::uint32_t>& pixels, int x, int y) {
    return pixels[std::size_t(y * kWidth + x)];
}

void testBitmapTipPaintsActualPixels() {
    auto doc = bitmapDocument(ResourceKind::Grayscale8, {255, 0});
    const auto pixels = paintNative(doc);
    CHECK(at(pixels, 42, 48) == 0xff2468acU);
    CHECK(at(pixels, 53, 48) == 0);
    CHECK(at(pixels, 42, 42) == 0); // 2:1 source rectangle, not an ellipse
    CHECK(at(pixels, 48, 48) != 0 && (at(pixels, 48, 48) >> 24) < 255);
    const auto masked = paintNative(doc, true);
    CHECK((at(masked, 42, 48) >> 24) == 128);
    CHECK(at(masked, 48, 48) == 0); // artwork selection still gates bitmap coverage
    doc.components[0].transform.flipX = true;
    const auto flipped = paintNative(doc);
    CHECK(at(flipped, 42, 48) == 0);
    CHECK(at(flipped, 53, 48) == 0xff2468acU);

    doc.components[0].transform.rotationDegrees = 90;
    const auto rotated = paintNative(doc);
    CHECK(at(rotated, 48, 53) == 0xff2468acU);
    CHECK(at(rotated, 48, 42) == 0);
    CHECK(at(rotated, 53, 48) == 0);
}

void testColourPixelsAndNativeRoundTrip() {
    auto doc = bitmapDocument(ResourceKind::Rgba8, {255, 24, 0, 128, 0, 0, 255, 0});
    const auto tap = paintNative(doc);
    CHECK(at(tap, 42, 48) == 0x80800c00U);
    CHECK(at(tap, 53, 48) == 0);
    CHECK((at(tap, 48, 48) & 255u) == 0); // transparent blue does not bleed into red
    const auto bytes = encodeBrush(doc);
    BrushDocument reloaded;
    DecodeResult decoded;
    CHECK(decodeBrush(bytes.data(), bytes.size(), &reloaded, &decoded));
    CHECK(drawing::digitalBrushIssue(reloaded).empty());
    CHECK(paintNative(doc, false, false) == paintNative(reloaded, false, false));
    reloaded.resources[0].setPixels({0, 255, 0, 128, 0, 0, 255, 0});
    CHECK(paintNative(reloaded) != tap);
    reloaded.components[0].kind = ComponentKind::Tip;
    CHECK(at(paintNative(reloaded), 42, 48) == 0x80123456U); // RGBA tip uses alpha, not imported hue
}

void testUnsupportedAndBadAssetsAreDiagnosed() {
    const auto bitmap = bitmapDocument(ResourceKind::Alpha8, {0, 255});
    auto broken = bitmap;
    broken.resources.clear();
    CHECK(drawing::digitalBrushIssue(broken).find("missing resource") != std::string::npos);
    broken = bitmap;
    broken.resources[0].setPixels({255});
    CHECK(!drawing::digitalBrushIssue(broken).empty());
    auto native = std::make_shared<const BrushDocument>(broken);
    SparseTileStore store(kWidth, kHeight);
    drawing::DocumentStroke rejected(store, native, drawing::digitalDefinition(*native, {}), 1u);
    CHECK(!rejected.begin({48, 48, 1}));
    CHECK(rejected.failed());
    CHECK(changedPixels(pixelsOf(store)) == 0);

    auto unsupported = bitmap;
    unsupported.components[0].blend = ComponentBlend::Multiply;
    CHECK(drawing::digitalBrushIssue(unsupported).find("blend") != std::string::npos);
    unsupported = bitmap;
    unsupported.setParameter("taper-out", 20);
    CHECK(drawing::digitalBrushIssue(unsupported).find("Taper out") != std::string::npos);
    unsupported = bitmap;
    unsupported.opaqueChunks.push_back({"FUTR", "retained"});
    CHECK(drawing::digitalBrushIssue(unsupported).find("extensions") != std::string::npos);
    unsupported = bitmap;
    unsupported.engine = Engine::Realistic;
    CHECK(drawing::digitalBrushIssue(unsupported).find("Realistic") != std::string::npos);
}

void testInvalidStrokeIsReportedNotSilent() {
    // A size below the engine's minimum must fail loudly, exactly as the flat
    // path does, rather than painting nothing.
    LegacyBrush broken;
    broken.size = 0.2;
    broken.spacing = 15;
    BrushDocument document = BrushDocument::fromLegacy(broken, "brush-broken", "Broken");
    StrokeRuntime runtime;
    SparseTileStore store(kWidth, kHeight);
    drawing::DocumentStroke stroke(store, document,
                                   drawing::digitalDefinition(document, runtime), 1u);
    CHECK(stroke.failed());
    CHECK(!stroke.begin(strokePath().front()));
}

} // namespace

int main() {
    testDefinitionsMatchForEveryLegacyField();
    testEngineSwitchIsHonoured();
    testMigratedBrushPaintsIdenticalPixels();
    testBothEnginesMigrateALegacyRecordTheSameWay();
    testInitialPaintMatches();
    testEveryComponentPaintsInAllMode();
    testRandomSelectionIsReproducibleAndSeeded();
    testPerComponentErase();
    testHiddenComponentsDoNotPaint();
    testRandomizationReachesThePixels();
    testInvalidStrokeIsReportedNotSilent();
    testBitmapTipPaintsActualPixels();
    testColourPixelsAndNativeRoundTrip();
    testUnsupportedAndBadAssetsAreDiagnosed();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
