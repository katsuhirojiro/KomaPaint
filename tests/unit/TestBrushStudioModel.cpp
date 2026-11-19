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
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

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
    void componentEditingChangesTheDocument();
    void componentOrderAndSelectionFollowTheDocument();
    void groupsPickModeAndMembers();
    void regroupedComponentsStayConsistentThroughSave();
    void settingsComeFromTheRegistry();
    void settingsRejectNonsense();
    void exposureCanBeRenamedReorderedAndReset();
    void undoAndRedoRestoreExactly();
    void saveAndLoadRoundTrip();
    void anUnsaveableBrushIsRefused();
    void aCorruptFileChangesNothing();
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
    QVERIFY(!model.canUndo());
    QVERIFY(!model.isModified());
    // A brush with one tip and one generator is valid and immediately usable.
    QVERIFY(document.validate().ok());
    QCOMPARE(model.validationSummary(), QStringLiteral("ok"));
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
    QVERIFY(model.setComponentOpacity(id, 5.0));   // clamped, not rejected silently
    QVERIFY(model.setComponentBlend(id, int(brush::ComponentBlend::Multiply)));
    QVERIFY(model.setComponentErases(id, true));
    QVERIFY(model.setComponentTransform(id, QStringLiteral("offsetX"), 35.0));
    QVERIFY(model.setComponentTransform(id, QStringLiteral("rotation"), -22.0));
    QVERIFY(model.setComponentWeight(id, 7.5));

    const brush::BrushComponent* component = model.document().component(id.toStdString());
    QVERIFY(component);
    QCOMPARE(QString::fromStdString(component->name), QStringLiteral("Leaf"));
    QCOMPARE(component->visible, false);
    QCOMPARE(component->opacity, 1.0);
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
    model.setDocument(std::move(seed));

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
}

void TestBrushStudioModel::settingsRejectNonsense() {
    BrushStudioModel model;
    QVERIFY(!model.setParameterValue(QStringLiteral("size"), 100000.0));
    QVERIFY(model.error().contains(QStringLiteral("size")) || model.error().contains(QStringLiteral("Size")));
    QVERIFY(!model.setParameterValue(QStringLiteral("no-such-setting"), 1.0));
    QVERIFY(!model.error().isEmpty());
    // Nothing was written by a rejected edit.
    QCOMPARE(model.parameterValue(QStringLiteral("size")), 16.0);
    QVERIFY(!model.canUndo());
}

void TestBrushStudioModel::exposureCanBeRenamedReorderedAndReset() {
    BrushStudioModel model;
    const QVariantList initial = model.exposedProperties();
    QCOMPARE(int(initial.size()), 5);
    // Group order decides the panel: the tip's settings come first, and size
    // follows them, exactly as the thirteen groups are ordered.
    QCOMPARE(stringField(initial.at(0).toMap(), "key"), QStringLiteral("hardness"));
    QCOMPARE(stringField(initial.at(0).toMap(), "section"), QStringLiteral("Brush Tip"));
    QCOMPARE(stringField(initial.at(1).toMap(), "key"), QStringLiteral("size"));
    QCOMPARE(stringField(initial.at(1).toMap(), "section"), QStringLiteral("Size"));

    QVERIFY(model.expose(QStringLiteral("scatter-x")));
    QCOMPARE(int(model.exposedProperties().size()), 6);
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
    QCOMPARE(stringField(grouped.at(1).toMap(), "section"), QStringLiteral("Size"));
    QCOMPARE(stringField(grouped.at(2).toMap(), "section"), QStringLiteral("Opacity & Flow"));

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
    QCOMPARE(int(model.exposedProperties().size()), 5);

    model.expose(QStringLiteral("texture-strength"));
    model.resetExposure();
    QCOMPARE(int(model.exposedProperties().size()), 5);
    QCOMPARE(stringField(model.exposedProperties().at(1).toMap(), "key"), QStringLiteral("size"));
    QVERIFY(!model.isExposed(QStringLiteral("texture-strength")));
}

void TestBrushStudioModel::undoAndRedoRestoreExactly() {
    BrushStudioModel model;
    const brush::BrushDocument before = model.document();

    const QString id = model.addComponent(int(brush::ComponentKind::BristleGroup));
    QVERIFY(model.setComponentName(id, QStringLiteral("Tufts")));
    QVERIFY(model.setParameterValue(QStringLiteral("spacing"), 30.0));
    QVERIFY(model.expose(QStringLiteral("bristle-count")));
    QVERIFY(model.canUndo());

    model.undo();   // exposure
    QVERIFY(!model.isExposed(QStringLiteral("bristle-count")));
    model.undo();   // spacing
    QCOMPARE(model.parameterValue(QStringLiteral("spacing")), 15.0);
    model.undo();   // name
    QCOMPARE(QString::fromStdString(model.document().component(id.toStdString())->name),
             QStringLiteral("Component ") + id);
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
    model.setDocument(std::move(broken));
    QVERIFY(!model.save(dir.filePath(QStringLiteral("broken.kb"))));
    QVERIFY(model.error().contains(QStringLiteral("cannot be saved")));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("broken.kb"))));
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

QTEST_MAIN(TestBrushStudioModel)
#include "TestBrushStudioModel.moc"
