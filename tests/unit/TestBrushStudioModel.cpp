// Brush Studio's model, as the window will use it.
//
// These checks go through the Qt-facing class on purpose: the QVariantLists it
// hands to QML are where a component row can lose a field, where a slider can
// show a value the engine does not read, or where an undo step can silently
// drop an edit. Every check therefore compares against the document itself, not
// against a second copy of the same expectations.
#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <limits>

#include "app/BrushStudioModel.h"
#include "brush/BrushEvaluator.h"
#include "brush/BrushFormat.h"
#include "brush/BrushModel.h"

using namespace komapaint;

namespace {

QString stringField(const QVariantMap& map, const char* key) {
    return map.value(QString::fromUtf8(key)).toString();
}

double numberField(const QVariantMap& map, const char* key) {
    return map.value(QString::fromUtf8(key)).toDouble();
}

} // namespace

class TestBrushStudioModel : public QObject {
    Q_OBJECT
private slots:
    void newBrushIsPaintable();
    void localFileCommandsPreserveHistoryAndIdentity();
    void componentEditingChangesTheDocument();
    void componentOrderAndSelectionFollowTheDocument();
    void componentModesDescribeRealResources();
    void importsTipPixelsAsOneUndoableEdit();
    void paintsPixelsAndLinksMasks();
    void groupsPickModeAndMembers();
    void regroupedComponentsStayConsistentThroughSave();
    void settingsComeFromTheRegistry();
    void settingSectionsAndReadbackKeepScopesSeparate();
    void settingsRejectNonsense();
    void componentRegistrySettingsReachTheSelectedComponent();
    void invalidComponentEditsAreTransactional_data();
    void invalidComponentEditsAreTransactional();
    void exposureCanBeRenamedReorderedAndReset();
    void undoAndRedoRestoreExactly();
    void exposedEditsAreValidatedAndUndoable();
    void largeHistorySharesImmutablePixelsAndReplays();
    void pressureRejectsNewEditsWithoutDroppingRedo();
    void batchesAndSavePointFollowReplay();
    void saveAndLoadRoundTrip();
    void anUnsaveableBrushIsRefused();
    void failedOverwriteLeavesUnicodeBrushAndDraftIntact();
    void aCorruptFileChangesNothing();
    void unsupportedDataSurvivesOpenAndResave();
};

void TestBrushStudioModel::newBrushIsPaintable() {
    BrushStudioModel model;
    const brush::BrushDocument& document = model.document();
    QCOMPARE(int(document.components.size()), 1);
    QCOMPARE(int(document.generators.size()), 1);
    QVERIFY(document.generators.front().enabled);
    QCOMPARE(document.generators.front().kind, brush::GeneratorKind::Continuous);
    QCOMPARE(int(model.components().size()), 1);
    QCOMPARE(model.selectedIndex(), 0);
    QCOMPARE(model.selectedId(), QStringLiteral("c1"));
    // The authoring menus offer only combinations the Digital renderer paints.
    QCOMPARE(int(model.componentKinds().size()), 7);
    for (const auto& kind : model.componentKinds()) {
        const auto name = kind.toMap().value(QStringLiteral("name")).toString();
        QVERIFY(name != QStringLiteral("particle") && name != QStringLiteral("bristle-group") &&
                name != QStringLiteral("pattern") && name != QStringLiteral("sequence"));
        QVERIFY(!kind.toMap().value(QStringLiteral("description")).toString().isEmpty());
    }
    QCOMPARE(int(model.componentBlends().size()), 4);
    QCOMPARE(model.componentBlends().front().toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("Normal"));
    QVERIFY(!model.canUndo());
    QVERIFY(!model.isModified());
    // A brush with one tip and one generator is valid and immediately usable.
    QVERIFY(document.validate().ok());
    QCOMPARE(model.validationSummary(), QStringLiteral("ok"));
}

void TestBrushStudioModel::localFileCommandsPreserveHistoryAndIdentity() {
    QTemporaryDir files;
    QVERIFY(files.isValid());
    const auto saved = QUrl::fromLocalFile(files.filePath(QStringLiteral("試し筆 #1.kb")));
    const auto copy = QUrl::fromLocalFile(files.filePath(QStringLiteral("別の筆.kb")));
    const auto missing = QUrl::fromLocalFile(files.filePath(QStringLiteral("missing/file.kb")));
    const QUrl remote(QStringLiteral("https://example.invalid/brush.kb"));
    BrushStudioModel draft;
    draft.setName(QStringLiteral("試し筆"));
    QVERIFY(draft.saveDraft(saved));
    QVERIFY(!draft.isModified());
    const auto original = draft.document();
    QVERIFY(draft.duplicateDraft());
    QVERIFY(draft.document().id != original.id);
    QCOMPARE(draft.document().components, original.components);
    QCOMPARE(draft.document().exposed, original.exposed);
    QVERIFY(draft.isModified());

    const auto duplicate = draft.document();
    QVERIFY(draft.exportDraft(copy));
    QVERIFY(draft.isModified());
    BrushStudioModel onDisk;
    QVERIFY(onDisk.openDraft(copy));
    QVERIFY(onDisk.document() == duplicate);
    QVERIFY(!onDisk.isModified());
    QVERIFY(!draft.openDraft(remote));
    QVERIFY(!draft.saveDraft(QUrl{}));
    QVERIFY(!draft.exportDraft(missing));
    QVERIFY(!draft.error().isEmpty());
    QVERIFY(draft.document() == duplicate && draft.isModified());

    draft.undo();
    QVERIFY(draft.document() == original && !draft.isModified());
    QVERIFY(draft.canRedo());
    draft.setName(QStringLiteral("   "));
    QVERIFY(!draft.error().isEmpty());
    QVERIFY(draft.document() == original && draft.canRedo());
    draft.redo();
    QVERIFY(draft.document() == duplicate && draft.isModified());
    QVERIFY(draft.newDraft());
    QVERIFY(draft.document().id != duplicate.id && draft.document().id != original.id);
    QVERIFY(draft.document().validate().ok() && draft.isModified());
    draft.undo();
    QVERIFY(draft.document() == duplicate);
    QVERIFY(draft.openDraft(saved));
    QVERIFY(draft.document() == original && !draft.isModified());
    draft.undo();
    QVERIFY(draft.document() == duplicate && draft.isModified());
    draft.redo();
    QVERIFY(draft.document() == original && !draft.isModified());
}

void TestBrushStudioModel::componentEditingChangesTheDocument() {
    BrushStudioModel model;
    QSignalSpy componentsChanged(&model, &BrushStudioModel::componentsChanged);

    const QString id = model.addComponent(int(brush::ComponentKind::Image));
    QVERIFY(!id.isEmpty());
    // A component that needs pixels gets a usable mask, so the brush can be
    // saved the moment the artist adds it.
    QVERIFY(model.document().validate().ok());
    QVERIFY(!model.document().component(id.toStdString())->resourceId.empty());
    QCOMPARE(int(model.document().components.size()), 2);
    QCOMPARE(model.selectedId(), id);
    QVERIFY(componentsChanged.count() >= 1);

    QVERIFY(model.setComponentName(id, QStringLiteral("Leaf")));
    QVERIFY(model.setComponentVisible(id, false));
    QVERIFY(model.setComponentOpacity(id, 0.4));
    QVERIFY(model.setComponentBlend(id, int(brush::ComponentBlend::Multiply)));
    QVERIFY(model.setComponentErases(id, true));
    QVERIFY(model.setComponentTransform(id, QStringLiteral("offsetX"), 35.0));
    QVERIFY(model.setComponentTransform(id, QStringLiteral("rotation"), -22.0));
    QVERIFY(model.setComponentWeight(id, 7.5));

    const brush::BrushComponent* component = model.document().component(id.toStdString());
    QVERIFY(component);
    QCOMPARE(QString::fromStdString(component->name), QStringLiteral("Leaf"));
    QCOMPARE(component->visible, false);
    QCOMPARE(component->opacity, 0.4);
    QCOMPARE(component->blend, brush::ComponentBlend::Multiply);
    QVERIFY(component->erases);
    QCOMPARE(component->transform.offsetX, 35.0);
    QCOMPARE(component->transform.rotationDegrees, -22.0);
    QCOMPARE(component->weight, 7.5);
    QVERIFY(model.isModified());

    // The row QML reads carries the same numbers.
    const QVariantList rows = model.components();
    QCOMPARE(int(rows.size()), 2);
    const QVariantMap row = rows.at(1).toMap();
    QCOMPARE(stringField(row, "name"), QStringLiteral("Leaf"));
    QCOMPARE(numberField(row, "offsetX"), 35.0);
    QCOMPARE(row.value(QStringLiteral("visible")).toBool(), false);

    const QString copy = model.duplicateComponent(id);
    QVERIFY(!copy.isEmpty());
    QCOMPARE(int(model.document().components.size()), 3);
    QCOMPARE(model.document().component(copy.toStdString())->transform.offsetX, 35.0);

    QVERIFY(model.removeComponent(copy));
    QCOMPARE(int(model.document().components.size()), 2);
    QVERIFY(!model.removeComponent(copy));   // already gone: reported, not ignored
    QVERIFY(!model.error().isEmpty());

    // Unknown ids are errors with a sentence, never a silent no-op.
    QVERIFY(!model.setComponentName(QStringLiteral("nope"), QStringLiteral("x")));
    QVERIFY(!model.setComponentVisible(QStringLiteral("nope"), true));
}

void TestBrushStudioModel::componentOrderAndSelectionFollowTheDocument() {
    BrushStudioModel model;
    const QString a = model.addComponent(int(brush::ComponentKind::Texture));
    const QString b = model.addComponent(int(brush::ComponentKind::Pattern));
    QCOMPARE(int(model.document().components.size()), 3);

    QVERIFY(model.moveComponent(b, 0));
    QCOMPARE(QString::fromStdString(model.document().components.front().id), b);
    QCOMPARE(model.selectedId(), b);
    QVERIFY(!model.moveComponent(a, 99));    // out of range: refused

    model.setSelectedIndex(0);
    QCOMPARE(model.selectedId(), b);
    model.setSelectedIndex(99);
    QCOMPARE(model.selectedIndex(), -1);
    QCOMPARE(model.selectedId(), QString());

    // Deleting the selected component moves the selection to a real row.
    model.setSelectedIndex(0);
    QVERIFY(model.removeComponent(b));
    QVERIFY(model.selectedIndex() >= 0);
    QVERIFY(model.selectedIndex() < int(model.document().components.size()));
}

void TestBrushStudioModel::componentModesDescribeRealResources() {
    BrushStudioModel model;
    QVERIFY(!model.selectedResource());
    QVERIFY(!model.selectComponentInMode(QStringLiteral("texture")));
    QVERIFY(!model.isModified() && !model.canUndo());
    const QString texture = model.addComponent(int(brush::ComponentKind::Texture));
    QVERIFY(!texture.isEmpty());
    QCOMPARE(model.componentCount(), 2);
    QVERIFY(model.selectedResource());
    const auto row = model.selectedComponent();
    QCOMPARE(row.value(QStringLiteral("texture")).toBool(), true);
    QCOMPARE(numberField(row, "resourceWidth"), double(model.selectedResource()->width));
    QCOMPARE(numberField(row, "resourceHeight"), double(model.selectedResource()->height));
    QCOMPARE(stringField(row, "resourceFormat"), QStringLiteral("grayscale8"));
    QVERIFY(model.setComponentVisible(texture, false));
    QCOMPARE(model.hiddenComponentCount(), 1);
    QCOMPARE(model.retainedStorage().historyStates, std::size_t(2));
    QVERIFY(model.selectComponentInMode(QStringLiteral("tip")));
    QCOMPARE(model.selectedId(), QStringLiteral("c1"));
    QVERIFY(model.selectComponentInMode(QStringLiteral("texture")));
    QCOMPARE(model.selectedId(), texture);
    QCOMPARE(model.retainedStorage().historyStates, std::size_t(2));
}

void TestBrushStudioModel::importsTipPixelsAsOneUndoableEdit() {
    BrushStudioModel model;
    QTemporaryDir files;
    QVERIFY(files.isValid());
    const QString filename = files.filePath(QStringLiteral("stamp.png"));
    QImage image(2, 1, QImage::Format_RGBA8888);
    image.setPixelColor(0, 0, QColor(100, 50, 12, 80));
    image.setPixelColor(1, 0, QColor(8, 220, 130, 255));
    QVERIFY(image.save(filename));
    const auto url = QUrl::fromLocalFile(filename);
    QVERIFY(!model.importTipImage(url)); // select a bitmap component, not a procedural one
    QVERIFY(!model.addComponent(int(brush::ComponentKind::Image)).isEmpty());
    QVERIFY(model.canImportTipImage());
    const auto before = model.document();
    QVERIFY2(model.importTipImage(url), qPrintable(model.error()));
    QVERIFY(model.selectedResource());
    QCOMPARE(model.selectedResource()->kind, brush::ResourceKind::Rgba8);
    const std::vector<std::uint8_t> imported{100, 50, 12, 80, 8, 220, 130, 255};
    QCOMPARE(model.selectedResource()->pixels(), imported);
    QCOMPARE(model.document().resources.size(), std::size_t(1));
    const auto after = model.document();
    model.undo();
    QVERIFY(model.document() == before && model.canRedo());
    QVERIFY(QFile::remove(filename));
    QVERIFY(!model.importTipImage(url));
    QVERIFY(model.document() == before && model.canRedo());
    model.redo();
    QVERIFY(model.document() == after);
    const auto saved = QUrl::fromLocalFile(files.filePath(QStringLiteral("stamp.kb")));
    QVERIFY(model.saveDraft(saved));
    BrushStudioModel reloaded;
    QVERIFY(reloaded.openDraft(saved));
    QVERIFY(reloaded.document() == after);
}

void TestBrushStudioModel::paintsPixelsAndLinksMasks() {
    BrushStudioModel model;
    const auto before = model.document();
    // Luminance on an opaque canvas is coverage for a grayscale tip.
    const QVariantList painted{0xff000000u, 0xffffffffu, 0xffffffffu, 0xff000000u};
    QVERIFY2(model.setTipPixels(painted, 2, 2), qPrintable(model.error()));
    QVERIFY(model.selectedResource());
    QCOMPARE(model.selectedResource()->kind, brush::ResourceKind::Grayscale8);
    QCOMPARE(model.selectedResource()->pixels(), (std::vector<std::uint8_t>{0, 255, 255, 0}));
    QVERIFY(model.canUndo());
    model.undo();
    QVERIFY(model.document() == before);
    model.redo();
    QVERIFY(model.selectedResource()->pixels() == (std::vector<std::uint8_t>{0, 255, 255, 0}));
    QVERIFY(!model.setTipPixels({0xffffffffu}, 2, 2)); // wrong count is refused
    QVERIFY(!model.error().isEmpty());

    const auto texture = model.addComponent(int(brush::ComponentKind::GrayscaleMask));
    QVERIFY(!texture.isEmpty());
    const QString tip = QStringLiteral("c1");
    QVERIFY(model.setComponentMask(tip, texture));
    QCOMPARE(QString::fromStdString(model.document().component(tip.toStdString())->maskComponentId),
             texture);
    QVERIFY(!model.setComponentMask(tip, tip)); // a mark cannot mask itself
    QVERIFY(model.maskCandidates().size() >= 1);
    model.setSelectedIndex(0);
    QVERIFY(!model.loadSelectedPixels().value(QStringLiteral("rgba")).toList().isEmpty());

    const auto outer = model.createGroupInParent(QStringLiteral("Leaves"), QString());
    const auto inner = model.createGroupInParent(QStringLiteral("Leaf detail"), outer);
    QVERIFY(!outer.isEmpty() && !inner.isEmpty());
    QVERIFY(model.setGroupParent(inner, outer));
    QVERIFY(!model.setGroupParent(outer, inner)); // no group cycles
    QCOMPARE(int(model.groups().size()), 2);
}

void TestBrushStudioModel::groupsPickModeAndMembers() {
    BrushStudioModel model;
    const QString leaf = model.addComponent(int(brush::ComponentKind::Image));
    const QString group = model.createGroup(QStringLiteral("Leaves"));
    QVERIFY(!group.isEmpty());
    QCOMPARE(int(model.groups().size()), 1);
    QCOMPARE(stringField(model.groups().at(0).toMap(), "name"), QStringLiteral("Leaves"));

    QVERIFY(model.assignToGroup(leaf, group));
    QCOMPARE(QString::fromStdString(
                 model.document().component(leaf.toStdString())->groupId), group.toStdString());
    QCOMPARE(int(model.document().group(group.toStdString())->members.size()), 1);

    QVERIFY(model.setGroupMode(group, int(brush::SelectionMode::Weighted)));
    QCOMPARE(model.document().group(group.toStdString())->mode, brush::SelectionMode::Weighted);
    QVERIFY(model.setGroupMembersPerStamp(group, 2));
    QCOMPARE(model.document().group(group.toStdString())->membersPerStamp, 2);
    QVERIFY(!model.setGroupMode(group, 99));
    QVERIFY(!model.assignToGroup(leaf, QStringLiteral("missing")));
    QVERIFY(!model.error().isEmpty());

    // The group is what the generator draws from once it is assigned one.
    QVERIFY(model.removeGroup(group));
    QCOMPARE(int(model.groups().size()), 0);
}

void TestBrushStudioModel::regroupedComponentsStayConsistentThroughSave() {
    BrushStudioModel model;
    const QString a = model.selectedId();
    const QString b = model.addComponent(int(brush::ComponentKind::Image));
    const QString c = model.addComponent(int(brush::ComponentKind::Image));
    QVERIFY(!b.isEmpty() && !c.isEmpty());
    auto seed = model.document();
    brush::ComponentGroup parent;
    parent.id = "parent";
    QVERIFY(seed.addGroup(parent));
    brush::ComponentGroup child;
    child.id = "child";
    child.parentGroupId = parent.id;
    child.mode = brush::SelectionMode::Sequence;
    QVERIFY(seed.addGroup(child));
    seed.generators.front().groupId = parent.id;
    QVERIFY(model.setDocument(std::move(seed)));

    const QString root = QStringLiteral("parent");
    const QString nested = QStringLiteral("child");
    QVERIFY(model.assignToGroup(a, root));
    QVERIFY(model.assignToGroup(b, nested));
    QVERIFY(model.assignToGroup(c, nested));
    const auto original = model.document();
    QVERIFY(!model.assignToGroup(b, nested)); // same assignment keeps its sequence position
    QVERIFY(model.document() == original);
    QVERIFY(!model.assignToGroup(a, QStringLiteral("missing")));
    QVERIFY(model.document() == original);

    QVERIFY(model.assignToGroup(a, nested));
    const auto& doc = model.document();
    QVERIFY(doc.group("parent")->members.empty());
    const std::vector<std::string> nestedOrder = {b.toStdString(), c.toStdString(), a.toStdString()};
    QCOMPARE(doc.group("child")->members, nestedOrder);
    QCOMPARE(doc.group("child")->parentGroupId, std::string("parent"));
    QVERIFY(doc.validate().ok());
    const auto afterMove = doc;
    model.undo();
    QVERIFY(doc == original);
    model.redo();
    QVERIFY(doc == afterMove);

    // The global row order and the group's own stamp sequence are independent.
    QVERIFY(model.moveComponent(b, 0));
    QCOMPARE(doc.components.front().id, b.toStdString());
    QCOMPARE(doc.group("child")->members, nestedOrder);
    brush::StampContext context;
    context.stampIndex = 2;
    const auto chosen = brush::BrushEvaluator::evaluate(doc, context);
    QCOMPARE(chosen.size(), std::size_t(1));
    QCOMPARE(chosen.front().componentId, a.toStdString());

    QVERIFY(model.assignToGroup(a, QString{}));
    QVERIFY(doc.component(a.toStdString())->groupId.empty());
    QVERIFY(model.removeComponent(c));
    QVERIFY(!doc.component(c.toStdString()));
    QCOMPARE(doc.group("child")->members, std::vector<std::string>{b.toStdString()});
    const auto beforeRefusedRemoval = doc;
    QVERIFY(!model.removeGroup(root)); // active generator still uses this group
    QVERIFY(!model.error().isEmpty());
    QVERIFY(doc == beforeRefusedRemoval);
    QVERIFY(doc.validate().ok());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("nested-groups.kb"));
    QVERIFY2(model.save(path), qPrintable(model.error()));
    BrushStudioModel loaded;
    QVERIFY2(loaded.load(path), qPrintable(loaded.error()));
    QVERIFY(loaded.document() == doc);
}

void TestBrushStudioModel::settingsComeFromTheRegistry() {
    BrushStudioModel model;
    const QVariantList parameters = model.parameters();
    QCOMPARE(parameters.size(), int(brush::parameterRegistry().size()));
    for (const QVariant& entry : parameters) {
        const QVariantMap map = entry.toMap();
        QVERIFY(!stringField(map, "key").isEmpty());
        QVERIFY(!stringField(map, "label").isEmpty());
        QVERIFY(!stringField(map, "section").isEmpty());
        QVERIFY(!stringField(map, "description").isEmpty());
        const double low = numberField(map, "min");
        const double high = numberField(map, "max");
        const double value = numberField(map, "value");
        QVERIFY(low < high);
        QVERIFY(value >= low && value <= high);
    }

    QVERIFY(model.setParameterValue(QStringLiteral("size"), 42.0));
    QCOMPARE(model.parameterValue(QStringLiteral("size")), 42.0);
    QCOMPARE(model.document().parameters.value("size"), 42.0);
    QCOMPARE(numberField(model.parameterInfo(QStringLiteral("size")), "value"), 42.0);
    QCOMPARE(stringField(model.parameterInfo(QStringLiteral("size")), "section"),
             QStringLiteral("Size"));
    QVERIFY(model.parameterInfo(QStringLiteral("no-such-setting")).isEmpty());
    QVERIFY(model.setParameterValue(QStringLiteral("size"), brush::MaxBrushDiameter));
    QCOMPARE(model.parameterValue(QStringLiteral("size")), double(brush::MaxBrushDiameter));
}

void TestBrushStudioModel::settingSectionsAndReadbackKeepScopesSeparate() {
    BrushStudioModel model;
    const auto sections = model.settingSections();
    const auto order = brush::parameterSections();
    QCOMPARE(sections.size(), qsizetype(order.size()));
    QSet<QString> keys;
    for (qsizetype index = 0; index < sections.size(); ++index) {
        const auto group = sections.at(index).toMap();
        QCOMPARE(stringField(group, "title"), QString::fromUtf8(order[static_cast<std::size_t>(index)]));
        const auto rows = group.value(QStringLiteral("rows")).toList();
        QVERIFY(!rows.isEmpty());
        for (const auto& entry : rows) {
            const auto meta = entry.toMap();
            const auto key = stringField(meta, "key");
            QVERIFY(!keys.contains(key));
            keys.insert(key);
            const auto* info = brush::findParameter(key.toStdString());
            QVERIFY(info);
            QCOMPARE(numberField(meta, "min"), info->minimum);
            QCOMPARE(numberField(meta, "max"), info->maximum);
            QVERIFY(!meta.contains(QStringLiteral("value")) && !meta.contains(QStringLiteral("exposed")));
        }
    }
    QCOMPARE(keys.size(), qsizetype(brush::parameterRegistry().size()));
    const auto state = [&](const QString& key) {
        return model.settingState().value(QStringLiteral("values")).toMap().value(key).toMap();
    };
    const auto size = QStringLiteral("size");
    const auto shape = QStringLiteral("tip-shape");
    const auto spacing = QStringLiteral("spacing");
    QVERIFY(state(shape).value(QStringLiteral("available")).toBool());
    QVERIFY(model.setParameterText(spacing, QLocale().toString(12.625)));
    QCOMPARE(numberField(state(spacing), "value"), 12.625);
    QCOMPARE(numberField(model.settingState(), "changedCount"), 1.0);
    QVERIFY(model.setParameterText(size, QString::number(brush::MaxBrushDiameter)));
    QCOMPARE(numberField(state(size), "value"), double(brush::MaxBrushDiameter));
    const auto before = model.document();
    QVERIFY(!model.setParameterText(spacing, QStringLiteral("not a number")));
    QVERIFY(!model.setParameterText(spacing, QLocale().toString(51.25)));
    QVERIFY(model.document() == before);
    const auto texture = model.addComponent(int(brush::ComponentKind::Texture));
    QVERIFY(!texture.isEmpty());
    QVERIFY(!state(shape).value(QStringLiteral("available")).toBool());
    QVERIFY(state(QStringLiteral("component-opacity")).value(QStringLiteral("available")).toBool());
    model.setSelectedIndex(-1);
    QVERIFY(!state(QStringLiteral("component-opacity")).value(QStringLiteral("available")).toBool());
    QVERIFY(state(size).value(QStringLiteral("available")).toBool());
    const auto metadata = model.exposureState();
    const auto keysRole = QStringLiteral("keys");
    const auto entriesRole = QStringLiteral("entries");
    QVERIFY(model.setExposure(size, {{QStringLiteral("label"), QStringLiteral("Width")}}));
    QCOMPARE(model.exposureState().value(keysRole), metadata.value(keysRole));
    QCOMPARE(stringField(model.exposureState().value(entriesRole).toMap().value(size).toMap(), "label"), QStringLiteral("Width"));
    QVERIFY(model.unexpose(size));
    QCOMPARE(numberField(state(size), "value"), double(brush::MaxBrushDiameter));
    const auto candidates = model.exposureCandidates();
    QVERIFY(std::ranges::any_of(candidates, [&](const auto& row) { return stringField(row.toMap(), "key") == size; }));
    QCOMPARE(model.settingSections(), sections);
}

void TestBrushStudioModel::settingsRejectNonsense() {
    BrushStudioModel model;
    QVERIFY(!model.setParameterValue(QStringLiteral("size"), double(brush::MaxBrushDiameter) + 1.0));
    QVERIFY(model.error().contains(QStringLiteral("size")) || model.error().contains(QStringLiteral("Size")));
    QVERIFY(!model.setParameterValue(QStringLiteral("no-such-setting"), 1.0));
    QVERIFY(!model.error().isEmpty());
    // Nothing was written by a rejected edit.
    QCOMPARE(model.parameterValue(QStringLiteral("size")), 16.0);
    QVERIFY(!model.canUndo());
}

void TestBrushStudioModel::componentRegistrySettingsReachTheSelectedComponent() {
    BrushStudioModel model;
    const QString first = model.selectedId();
    const auto original = model.document().components.front();
    const QString edited = model.addComponent(int(brush::ComponentKind::ProceduralShape));
    QVERIFY(!edited.isEmpty());
    QCOMPARE(model.selectedId(), edited);
    const std::pair<const char*, double> settings[] = {
        {"component-opacity", 25.0}, {"component-scale", 150.0},
        {"component-rotation", -45.0}, {"component-aspect", 75.0},
        {"component-offset-x", -80.0}, {"component-offset-y", 35.0},
        {"component-weight", 2.0}, {"tip-shape", 3.0},
    };
    for (const auto& [key, value] : settings) {
        const auto name = QString::fromLatin1(key);
        QVERIFY2(model.setParameterValue(name, value), qPrintable(model.error()));
        QCOMPARE(model.parameterValue(name), value);
        QCOMPARE(numberField(model.parameterInfo(name), "value"), value);
        QVERIFY(!model.document().parameters.contains(key));
    }
    const auto& doc = model.document();
    QVERIFY(*doc.component(first.toStdString()) == original);
    const auto& c = *doc.component(edited.toStdString());
    QCOMPARE(c.opacity, 0.25);
    QCOMPARE(c.transform.scale, 1.5);
    QCOMPARE(c.transform.aspect, 0.75);
    QCOMPARE(c.transform.rotationDegrees, -45.0);
    QCOMPARE(c.transform.offsetX, -80.0);
    QCOMPARE(c.transform.offsetY, 35.0);
    QCOMPARE(c.weight, 2.0);
    QCOMPARE(c.parameters.value("tip-shape"), 3.0);

    const auto requests = brush::BrushEvaluator::evaluate(doc, {});
    QCOMPARE(requests.size(), std::size_t(2));
    QCOMPARE(requests.back().componentId, edited.toStdString());
    QCOMPARE(requests.back().opacity, 0.25);
    QCOMPARE(requests.back().scale, 1.5);
    QCOMPARE(requests.back().aspect, 0.75);
    QCOMPARE(requests.back().rotationDegrees, -45.0);
    QCOMPARE(requests.back().offsetX, -0.8);
    QCOMPARE(requests.back().offsetY, 0.35);
    QVERIFY(doc.validate().ok());

    model.setSelectedIndex(0);
    QCOMPARE(model.parameterValue(QStringLiteral("component-opacity")), 100.0);
    const auto before = doc;
    QSignalSpy history(&model, &BrushStudioModel::historyChanged);
    QVERIFY(model.setComponentVisible(first, true)); // already the requested value
    QVERIFY(model.setParameterValue(QStringLiteral("size"), 16.0));
    QCOMPARE(history.count(), 0);
    QVERIFY(doc == before);
}

void TestBrushStudioModel::invalidComponentEditsAreTransactional_data() {
    QTest::addColumn<QString>("operation");
    QTest::addColumn<QString>("key");
    QTest::addColumn<double>("value");
    QTest::addColumn<bool>("missing");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    using namespace Qt::StringLiterals;
    for (const auto* op : {"name", "visible", "opacity", "blend", "erases", "transform", "weight"}) {
        QTest::newRow(qPrintable(u"missing-"_s + QString::fromLatin1(op)))
            << QString::fromLatin1(op) << u"scale"_s << 0.5 << true;
    }
    QTest::newRow("empty-name") << u"name"_s << u"   "_s << 0.0 << false;
    QTest::newRow("negative-opacity") << u"opacity"_s << QString{} << -0.2 << false;
    QTest::newRow("high-opacity") << u"opacity"_s << QString{} << 5.0 << false;
    QTest::newRow("nan-opacity") << u"opacity"_s << QString{} << nan << false;
    QTest::newRow("unknown-blend") << u"blend"_s << QString{} << 99.0 << false;
    QTest::newRow("negative-blend") << u"blend"_s << QString{} << -1.0 << false;
    QTest::newRow("unknown-transform") << u"transform"_s << u"wrong"_s << 1.0 << false;
    QTest::newRow("zero-scale") << u"transform"_s << u"scale"_s << 0.0 << false;
    QTest::newRow("negative-aspect") << u"transform"_s << u"aspect"_s << -1.0 << false;
    QTest::newRow("infinite-rotation") << u"transform"_s << u"rotation"_s << inf << false;
    QTest::newRow("nan-offset-x") << u"transform"_s << u"offsetX"_s << nan << false;
    QTest::newRow("out-of-range-offset-y") << u"transform"_s << u"offsetY"_s << 201.0 << false;
    QTest::newRow("negative-weight") << u"weight"_s << QString{} << -2.0 << false;
    QTest::newRow("nonfinite-weight") << u"weight"_s << QString{} << inf << false;
    QTest::newRow("last-positive-weight") << u"weighted"_s << QString{} << 0.0 << false;
    QTest::newRow("missing-mode-group") << u"mode"_s << QString{} << 1.0 << true;
    QTest::newRow("missing-count-group") << u"count"_s << QString{} << 2.0 << true;
    QTest::newRow("invalid-mode") << u"mode"_s << QString{} << 17.0 << false;
    QTest::newRow("weighted-with-no-weight") << u"mode-zero"_s << QString{} << 2.0 << false;
    QTest::newRow("invalid-count") << u"count"_s << QString{} << 0.0 << false;
    QTest::newRow("fractional-tip-shape") << u"registry"_s << u"tip-shape"_s << 2.5 << false;
    QTest::newRow("no-component-selected") << u"registry"_s << u"component-opacity"_s << 20.0 << true;
    QTest::newRow("invalid-registry-scope-input") << u"registry"_s << u"component-scale"_s << nan << false;
    QTest::newRow("bitmap-tip-shape") << u"bitmap"_s << u"tip-shape"_s << 1.0 << false;
}

void TestBrushStudioModel::invalidComponentEditsAreTransactional() {
    QFETCH(QString, operation);
    QFETCH(QString, key);
    QFETCH(double, value);
    QFETCH(bool, missing);
    BrushStudioModel model;
    QString id = model.selectedId();
    QString group;
    if (operation == QStringLiteral("weighted") || operation == QStringLiteral("mode") ||
        operation == QStringLiteral("count")) {
        group = model.createGroup(QStringLiteral("Group"));
        QVERIFY(model.assignToGroup(id, group));
        QVERIFY(model.setGroupMode(group, int(brush::SelectionMode::Weighted)));
    }
    if (operation == QStringLiteral("mode-zero")) {
        group = model.createGroup(QStringLiteral("Group"));
        QVERIFY(model.assignToGroup(id, group));
        QVERIFY(model.setComponentWeight(id, 0.0));
    }
    if (operation == QStringLiteral("bitmap")) {
        id = model.addComponent(int(brush::ComponentKind::Image));
        QVERIFY(!id.isEmpty());
    }
    if (missing) {
        id = group = QStringLiteral("missing");
        if (operation == QStringLiteral("registry")) model.setSelectedIndex(-1);
    }
    model.setModified(false);
    const auto before = model.document();
    const bool couldUndo = model.canUndo();
    const auto selection = model.selectedId();
    QSignalSpy history(&model, &BrushStudioModel::historyChanged);
    QSignalSpy changes(&model, &BrushStudioModel::documentChanged);
    bool result = true;
    if (operation == QStringLiteral("name")) result = model.setComponentName(id, key);
    else if (operation == QStringLiteral("visible")) result = model.setComponentVisible(id, false);
    else if (operation == QStringLiteral("opacity")) result = model.setComponentOpacity(id, value);
    else if (operation == QStringLiteral("blend")) result = model.setComponentBlend(id, int(value));
    else if (operation == QStringLiteral("erases")) result = model.setComponentErases(id, true);
    else if (operation == QStringLiteral("transform")) result = model.setComponentTransform(id, key, value);
    else if (operation == QStringLiteral("weight") || operation == QStringLiteral("weighted"))
        result = model.setComponentWeight(id, value);
    else if (operation == QStringLiteral("mode") || operation == QStringLiteral("mode-zero"))
        result = model.setGroupMode(group, int(value));
    else if (operation == QStringLiteral("count")) result = model.setGroupMembersPerStamp(group, int(value));
    else result = model.setParameterValue(key, value);
    QVERIFY(!result);
    QVERIFY(!model.error().isEmpty());
    QVERIFY(model.document() == before);
    QCOMPARE(model.selectedId(), selection);
    QCOMPARE(model.canUndo(), couldUndo);
    QVERIFY(!model.isModified());
    QCOMPARE(history.count(), 0);
    QCOMPARE(changes.count(), 0);
}

void TestBrushStudioModel::exposureCanBeRenamedReorderedAndReset() {
    BrushStudioModel model;
    const QVariantList initial = model.exposedProperties();
    QCOMPARE(int(initial.size()), 7);
    // Group order decides the panel: the tip's settings come first, and size
    // follows them, exactly as the thirteen groups are ordered. Aspect and Tip
    // Angle are exposed by default for marker-like flat nibs.
    QCOMPARE(stringField(initial.at(0).toMap(), "key"), QStringLiteral("hardness"));
    QCOMPARE(stringField(initial.at(0).toMap(), "section"), QStringLiteral("Brush Tip"));
    QCOMPARE(stringField(initial.at(3).toMap(), "key"), QStringLiteral("size"));
    QCOMPARE(stringField(initial.at(3).toMap(), "section"), QStringLiteral("Size"));
    QVERIFY(model.isExposed(QStringLiteral("aspect")));
    QVERIFY(model.isExposed(QStringLiteral("tip-angle")));

    QVERIFY(model.expose(QStringLiteral("scatter-x")));
    QCOMPARE(int(model.exposedProperties().size()), 8);
    QVERIFY(model.isExposed(QStringLiteral("scatter-x")));
    QVERIFY(!model.expose(QStringLiteral("no-such-setting")));

    QVariantMap fields;
    fields.insert(QStringLiteral("label"), QStringLiteral("Spread"));
    fields.insert(QStringLiteral("control"), QStringLiteral("number"));
    QVERIFY(model.setExposure(QStringLiteral("scatter-x"), fields));
    const QVariantList afterRename = model.exposedProperties();
    const QVariantMap renamed = afterRename.last().toMap();
    QCOMPARE(stringField(renamed, "label"), QStringLiteral("Spread"));
    QCOMPARE(stringField(renamed, "section"), QStringLiteral("Scatter"));
    QCOMPARE(stringField(renamed, "control"), QStringLiteral("number"));

    // Groups come in the panel's own order: the tip before size, size before
    // opacity, scatter after both -- not alphabetically.
    const QVariantList grouped = model.exposedProperties();
    QCOMPARE(stringField(grouped.at(0).toMap(), "section"), QStringLiteral("Brush Tip"));
    QCOMPARE(stringField(grouped.at(3).toMap(), "section"), QStringLiteral("Size"));
    QCOMPARE(stringField(grouped.at(4).toMap(), "section"), QStringLiteral("Opacity & Flow"));

    // Ordering happens inside a group; a move that would leave the group is
    // refused rather than quietly doing nothing.
    QVERIFY(model.expose(QStringLiteral("scatter-y")));
    const QVariantList scatterRows = model.exposedProperties();
    QCOMPARE(stringField(scatterRows.at(scatterRows.size() - 2).toMap(), "key"),
             QStringLiteral("scatter-x"));
    QVERIFY(model.moveExposure(QStringLiteral("scatter-y"), -1));
    QCOMPARE(stringField(model.exposedProperties().at(scatterRows.size() - 2).toMap(), "key"),
             QStringLiteral("scatter-y"));
    QVERIFY(!model.moveExposure(QStringLiteral("scatter-y"), -1));
    QVERIFY(!model.moveExposure(QStringLiteral("nope"), 1));

    QVERIFY(model.unexpose(QStringLiteral("scatter-x")));
    QVERIFY(model.unexpose(QStringLiteral("scatter-y")));
    QCOMPARE(int(model.exposedProperties().size()), 7);

    model.expose(QStringLiteral("texture-strength"));
    model.resetExposure();
    QCOMPARE(int(model.exposedProperties().size()), 7);
    QCOMPARE(stringField(model.exposedProperties().at(3).toMap(), "key"), QStringLiteral("size"));
    QVERIFY(!model.isExposed(QStringLiteral("texture-strength")));
}

void TestBrushStudioModel::undoAndRedoRestoreExactly() {
    BrushStudioModel model;
    const brush::BrushDocument before = model.document();

    const QString id = model.addComponent(int(brush::ComponentKind::BristleGroup));
    const auto originalName = model.document().components.back().name;
    QVERIFY(model.setComponentName(id, QStringLiteral("Tufts")));
    QVERIFY(model.setParameterValue(QStringLiteral("spacing"), 30.0));
    QVERIFY(model.expose(QStringLiteral("bristle-count")));
    QVERIFY(model.canUndo());

    model.undo();   // exposure
    QVERIFY(!model.isExposed(QStringLiteral("bristle-count")));
    model.undo();   // spacing
    QCOMPARE(model.parameterValue(QStringLiteral("spacing")), 15.0);
    model.undo();   // name
    QCOMPARE(model.document().component(id.toStdString())->name, originalName);
    model.undo();   // the component itself
    QCOMPARE(int(model.document().components.size()), 1);
    QVERIFY(model.document() == before);
    QVERIFY(!model.canUndo());

    model.redo();
    QCOMPARE(int(model.document().components.size()), 2);
    model.redo();
    QCOMPARE(QString::fromStdString(model.document().component(id.toStdString())->name),
             QStringLiteral("Tufts"));
    model.redo();
    QCOMPARE(model.parameterValue(QStringLiteral("spacing")), 30.0);
    model.redo();
    QVERIFY(model.isExposed(QStringLiteral("bristle-count")));

    // A new edit after undo drops the redo branch, as everywhere else.
    model.undo();
    QVERIFY(model.canRedo());
    QVERIFY(model.setParameterValue(QStringLiteral("flow"), 55.0));
    QVERIFY(!model.canRedo());
}

void TestBrushStudioModel::exposedEditsAreValidatedAndUndoable() {
    BrushStudioModel model;
    const auto key = QStringLiteral("opacity");
    const auto original = model.document();
    QSignalSpy history(&model, &BrushStudioModel::historyChanged);
    const QVariantMap bad = {{QStringLiteral("label"), QStringLiteral("Transparency")},
                             {QStringLiteral("maximum"), std::numeric_limits<double>::infinity()}};
    QVERIFY(!model.setExposure(key, bad));
    QVERIFY(!model.setExposure(key, {{QStringLiteral("order"), 1.25}}));
    QVERIFY(!model.setExposure(key, {{QStringLiteral("control"), QStringLiteral("invalid")}}));
    QVERIFY(!model.setExposure(key, {{QStringLiteral("minimum"), 75.0}, {QStringLiteral("maximum"), 15.0}}));
    QVERIFY(!model.error().isEmpty());
    QVERIFY(model.document() == original);
    QCOMPARE(history.count(), 0);
    QVERIFY(!model.isModified());

    // A partial range starts from the registry's other endpoint, not zero.
    const QVariantMap valid = {{QStringLiteral("label"), QStringLiteral("Transparency")},
                               {QStringLiteral("minimum"), 10.0}};
    QVERIFY(model.setExposure(key, valid));
    const auto edited = model.document();
    const auto prop = std::ranges::find(edited.exposed, std::string("opacity"), &brush::ExposedProperty::parameter);
    QVERIFY(prop != edited.exposed.end());
    QVERIFY(prop->hasRangeOverride);
    QCOMPARE(prop->minimumOverride, 10.0);
    QCOMPARE(prop->maximumOverride, 100.0);
    QVERIFY(model.moveExposure(key, 1)); // past flow, within the same section
    const auto reordered = model.document();
    QVERIFY(reordered != edited);
    QCOMPARE(history.count(), 2);
    QVERIFY(!model.moveExposure(key, std::numeric_limits<int>::max()));
    QVERIFY(model.document() == reordered);
    QCOMPARE(history.count(), 2);

    model.undo();
    QVERIFY(model.document() == edited);
    model.undo();
    QVERIFY(model.document() == original);
    model.redo();
    model.redo();
    QVERIFY(model.document() == reordered);
}

void TestBrushStudioModel::largeHistorySharesImmutablePixelsAndReplays() {
    BrushStudioModel model;
    auto seed = model.document();
    auto& tip = seed.components.front();
    tip.kind = brush::ComponentKind::AlphaMask;
    tip.parameters.clear();
    tip.resourceId = "large-mask";
    brush::BrushResource resource;
    resource.id = tip.resourceId;
    resource.width = 16 * 1024;
    resource.height = 6400;
    constexpr std::size_t imageBytes = 100u * 1024u * 1024u;
    QCOMPARE(resource.expectedBytes(), imageBytes);
    std::vector<std::uint8_t> pixels(imageBytes, 0x35);
    pixels.back() = 0x7d;
    const auto* image = pixels.data();
    resource.setPixels(std::move(pixels));
    QVERIFY(seed.addResource(std::move(resource)));
    QVERIFY(seed.validate().ok());
    QVERIFY(model.setDocument(std::move(seed)));
    model.setModified(false);
    constexpr int editCount = 512;
    for (int i = 1; i <= editCount; ++i) {
        QVERIFY2(model.setParameterValue(QStringLiteral("size"), 16 + i), qPrintable(model.error()));
        QCOMPARE(model.document().resources.front().pixels().data(), image);
    }
    const auto measured = model.retainedStorage();
    QCOMPARE(measured.historyStates, std::size_t(editCount + 1));
    QCOMPARE(measured.pixelBytes, imageBytes);
    QVERIFY(measured.metadataBytes < 2u * 1024u * 1024u);
    for (int i = editCount - 1; i >= 0; --i) {
        QVERIFY(model.canUndo());
        model.undo();
        QCOMPARE(model.parameterValue(QStringLiteral("size")), double(16 + i));
        QCOMPARE(model.document().resources.front().pixels().data(), image);
    }
    QVERIFY(!model.isModified());
    QCOMPARE(model.retainedStorage().pixelBytes, imageBytes); // redo storage is counted too
    QCOMPARE(model.retainedStorage().historyStates, measured.historyStates);
    for (int i = 1; i <= editCount; ++i) {
        QVERIFY(model.canRedo());
        model.redo();
        QCOMPARE(model.parameterValue(QStringLiteral("size")), double(16 + i));
    }
    QVERIFY(!model.canRedo());
    QVERIFY(model.isModified());

    // Replacing a resource must not mutate the bytes held by older revisions.
    auto replacement = model.document();
    auto& mask = replacement.resources.front();
    mask.width = 2;
    mask.height = 2;
    mask.setPixels({1, 2, 3, 4});
    QVERIFY(model.setDocument(std::move(replacement)));
    QCOMPARE(model.document().resources.front().pixels().size(), std::size_t(4));
    QCOMPARE(model.retainedStorage().pixelBytes, imageBytes + 4);
    model.undo();
    QCOMPARE(model.document().resources.front().pixels().data(), image);
    QCOMPARE(model.document().resources.front().pixels().back(), std::uint8_t(0x7d));
    model.redo();
    QCOMPARE(model.document().resources.front().pixels().back(), std::uint8_t(4));
    model.undo();
    QVERIFY(model.setParameterValue(QStringLiteral("size"), 26));
    QVERIFY(!model.canRedo());
    QCOMPARE(model.retainedStorage().pixelBytes, imageBytes); // discarded branch released
}

void TestBrushStudioModel::pressureRejectsNewEditsWithoutDroppingRedo() {
    struct Pressure {
        bool reject = false;
        int attempts = 0;
    } pressure;
    BrushStudioModel model(nullptr, [](void* context, std::size_t bytes) noexcept {
        auto& state = *static_cast<Pressure*>(context);
        ++state.attempts;
        return !state.reject || bytes == 0;
    }, &pressure);
    const auto original = model.document();
    const QString first = model.selectedId();
    const QString second = model.addComponent(int(brush::ComponentKind::Image));
    QVERIFY(!second.isEmpty());
    const auto edited = model.document();
    model.undo();
    QVERIFY(!model.canUndo());
    QVERIFY(model.canRedo());
    QVERIFY(!model.isModified());
    const auto beforeBytes = model.retainedStorage();
    QSignalSpy documents(&model, &BrushStudioModel::documentChanged);
    QSignalSpy history(&model, &BrushStudioModel::historyChanged);
    QSignalSpy messages(&model, &BrushStudioModel::errorChanged);
    QSignalSpy selections(&model, &BrushStudioModel::selectionChanged);

    pressure.reject = true;
    model.beginStep(QStringLiteral("Cancelled brush gesture"));
    QVERIFY(!model.setComponentOpacity(first, 0.30));
    QVERIFY(!model.setParameterValue(QStringLiteral("size"), 12000.0));
    QVERIFY(model.addComponent(int(brush::ComponentKind::Image)).isEmpty());
    QVERIFY(!model.setDocument(edited));
    QVERIFY(model.error().contains(QStringLiteral("available memory")));
    QVERIFY(messages.count() >= 1);
    model.commitStep();
    QVERIFY(model.document() == original);
    QCOMPARE(model.selectedId(), first);
    QVERIFY(!model.isModified());
    QVERIFY(!model.canUndo() && model.canRedo());
    QCOMPARE(documents.count(), 0);
    QCOMPARE(history.count(), 0);
    QCOMPARE(selections.count(), 0);
    QCOMPARE(model.retainedStorage().historyStates, beforeBytes.historyStates);
    QCOMPARE(model.retainedStorage().pixelBytes, beforeBytes.pixelBytes);

    const int attempts = pressure.attempts;
    model.redo();
    QVERIFY(model.document() == edited);
    QCOMPARE(model.selectedId(), second);
    QVERIFY(model.isModified());
    model.undo();
    QVERIFY(model.document() == original);
    QCOMPARE(model.selectedId(), first);
    QCOMPARE(pressure.attempts, attempts); // replay needed no new memory admission
    QVERIFY(model.error().isEmpty());

    pressure.reject = false;
    model.beginStep(QStringLiteral("Brush gesture"));
    QVERIFY(model.setParameterValue(QStringLiteral("size"), 12345.0));
    const auto accepted = model.document();
    pressure.reject = true;
    QVERIFY(!model.setParameterValue(QStringLiteral("flow"), 20.0));
    QVERIFY(model.document() == accepted);
    pressure.reject = false;
    QVERIFY(model.setParameterValue(QStringLiteral("flow"), 30.0));
    model.commitStep();
    QVERIFY(!model.canRedo());
    QCOMPARE(model.retainedStorage().historyStates, std::size_t(1));
    model.undo();
    QVERIFY(model.document() == original);
    QVERIFY(!model.isModified());
}

void TestBrushStudioModel::batchesAndSavePointFollowReplay() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BrushStudioModel model;
    const auto firstFile = dir.filePath(QStringLiteral("before.kb"));
    const auto secondFile = dir.filePath(QStringLiteral("after.kb"));
    QVERIFY2(model.save(firstFile), qPrintable(model.error()));
    const auto before = model.document();
    model.beginStep(QStringLiteral("Empty gesture"));
    model.commitStep();
    QVERIFY(!model.canUndo());
    QVERIFY(!model.isModified());

    model.beginStep(QStringLiteral("Build tip"));
    const auto id = model.addComponent(int(brush::ComponentKind::Image));
    QVERIFY(!id.isEmpty());
    QVERIFY(model.setComponentOpacity(id, 0.5));
    QVERIFY(model.setParameterValue(QStringLiteral("size"), 77.0));
    model.commitStep();
    const auto after = model.document();
    QCOMPARE(model.retainedStorage().historyStates, std::size_t(1));
    model.undo();
    QVERIFY(model.document() == before);
    QVERIFY(!model.isModified());
    model.redo();
    QVERIFY(model.document() == after);
    QCOMPARE(model.selectedId(), id);
    QVERIFY(model.isModified());
    QVERIFY2(model.save(secondFile), qPrintable(model.error()));
    model.undo();
    QVERIFY(model.document() == before);
    QVERIFY(model.isModified()); // the newer file is the saved revision now
    model.redo();
    QVERIFY(model.document() == after);
    QVERIFY(!model.isModified());
    QVERIFY2(model.load(firstFile), qPrintable(model.error()));
    QVERIFY(model.document() == before);
    QVERIFY(!model.isModified());
    model.undo();
    QVERIFY(model.document() == after);
    QVERIFY(model.isModified());
    model.redo();
    QVERIFY(model.document() == before);
    QVERIFY(!model.isModified());
}

void TestBrushStudioModel::saveAndLoadRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("brush.kb"));

    BrushStudioModel source;
    source.setName(QStringLiteral("Foliage test"));
    const QString leaf = source.addComponent(int(brush::ComponentKind::Image));
    QVERIFY(source.setComponentName(leaf, QStringLiteral("Leaf")));
    QVERIFY(source.setComponentTransform(leaf, QStringLiteral("scale"), 1.7));
    QVERIFY(source.setParameterValue(QStringLiteral("size"), 64.0));
    const QString group = source.createGroup(QStringLiteral("Leaves"));
    QVERIFY(source.assignToGroup(leaf, group));
    QVERIFY(source.save(path));
    QVERIFY(!source.isModified());
    QVERIFY(QFileInfo(path).size() > 0);

    BrushStudioModel loaded;
    QVERIFY(loaded.load(path));
    QVERIFY(!loaded.isModified());
    QCOMPARE(loaded.name(), QStringLiteral("Foliage test"));
    QCOMPARE(int(loaded.document().components.size()), 2);
    QCOMPARE(int(loaded.groups().size()), 1);
    QCOMPARE(loaded.parameterValue(QStringLiteral("size")), 64.0);
    QVERIFY(loaded.document() == source.document());
    // Byte-identical when written again: the format is deterministic.
    const std::vector<std::uint8_t> firstBytes = brush::encodeBrush(source.document());
    const std::vector<std::uint8_t> secondBytes = brush::encodeBrush(loaded.document());
    QVERIFY(firstBytes == secondBytes);

    QVERIFY(loaded.save(dir.filePath(QStringLiteral("again.kb"))));
    QVERIFY(loaded.load(dir.filePath(QStringLiteral("again.kb"))));
    QVERIFY(loaded.document() == source.document());
}

void TestBrushStudioModel::anUnsaveableBrushIsRefused() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BrushStudioModel model;
    // Break the document the way a half-finished edit could: a component that
    // points at a resource nobody stored.
    const QString id = model.addComponent(int(brush::ComponentKind::ProceduralShape));
    QVERIFY(model.setComponentTransform(id, QStringLiteral("scale"), 2.0));
    brush::BrushDocument broken = model.document();
    broken.components.back().kind = brush::ComponentKind::Image;
    QVERIFY(model.setDocument(std::move(broken)));
    QVERIFY(!model.save(dir.filePath(QStringLiteral("broken.kb"))));
    QVERIFY(model.error().contains(QStringLiteral("cannot be saved")));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("broken.kb"))));
}

void TestBrushStudioModel::failedOverwriteLeavesUnicodeBrushAndDraftIntact() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("\u7b46 \u00e9.kb"));
    BrushStudioModel model;
    model.setName(QStringLiteral("Original"));
    QVERIFY2(model.save(path), qPrintable(model.error()));
    const auto original = model.document();
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto originalBytes = file.readAll();
    file.close();

    auto broken = original;
    broken.components.front().resourceId.clear();
    broken.components.front().kind = brush::ComponentKind::Image;
    QVERIFY(model.setDocument(broken));
    QVERIFY(!model.save(path));
    QVERIFY(model.document() == broken);
    QVERIFY(model.isModified());
    QVERIFY(!model.error().isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), originalBytes);
    file.close();
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 1);

    BrushStudioModel restored;
    QVERIFY2(restored.load(path), qPrintable(restored.error()));
    QVERIFY(restored.document() == original);
    restored.setName(QStringLiteral("Next draft"));
    QVERIFY2(restored.save(path), qPrintable(restored.error()));
    QVERIFY2(model.load(path), qPrintable(model.error()));
    QCOMPARE(model.name(), QStringLiteral("Next draft"));
}

void TestBrushStudioModel::unsupportedDataSurvivesOpenAndResave() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("unsupported.kb"));
    const QString resaved = dir.filePath(QStringLiteral("resaved.kb"));

    auto document = BrushStudioModel().document();
    QVERIFY(document.setParameter("stroke-delay", 42.0));
    QVERIFY(document.setParameter("taper-in", 0.0));
    QVERIFY(brush::saveBrushFile(QFileInfo(source).filesystemFilePath(), document));

    BrushStudioModel opened;
    QVERIFY2(opened.openDraft(QUrl::fromLocalFile(source)), qPrintable(opened.error()));
    QCOMPARE(opened.document(), document);
    const auto values = opened.settingState().value(QStringLiteral("values")).toMap();
    const auto texture = values.value(QStringLiteral("stroke-delay")).toMap();
    QVERIFY(!texture.value(QStringLiteral("supported")).toBool());
    QVERIFY(!texture.value(QStringLiteral("reason")).toString().isEmpty());
    QVERIFY(!opened.setParameterValue(QStringLiteral("stroke-delay"), 20.0));
    QVERIFY(opened.error().contains(QStringLiteral("Stroke delay")));
    QCOMPARE(opened.document(), document);

    // Editing a supported setting must not drop the stored unsupported data.
    QVERIFY(opened.setParameterValue(QStringLiteral("size"), 33.0));
    QVERIFY2(opened.saveDraft(QUrl::fromLocalFile(resaved)), qPrintable(opened.error()));
    BrushStudioModel reread;
    QVERIFY2(reread.openDraft(QUrl::fromLocalFile(resaved)), qPrintable(reread.error()));
    QCOMPARE(reread.document().parameters.value("stroke-delay"), 42.0);
    QCOMPARE(reread.document().parameters.value("taper-in"), 0.0);
    QCOMPARE(reread.document().parameters.value("size"), 33.0);
    QVERIFY(!reread.document().parameters.contains("taper-out"));
}

void TestBrushStudioModel::aCorruptFileChangesNothing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("broken.kb"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a brush file at all");
    file.close();

    BrushStudioModel model;
    const brush::BrushDocument before = model.document();
    QVERIFY(!model.load(path));
    QVERIFY(!model.error().isEmpty());
    QVERIFY(model.document() == before);
    QCOMPARE(int(model.document().components.size()), 1);

    QVERIFY(!model.load(dir.filePath(QStringLiteral("missing.kb"))));
    QVERIFY(!model.error().isEmpty());

    QVERIFY(!model.save(dir.filePath(QStringLiteral("no/such/folder/brush.kb"))));
    QVERIFY(!model.error().isEmpty());
}

QTEST_GUILESS_MAIN(TestBrushStudioModel)
#include "TestBrushStudioModel.moc"
