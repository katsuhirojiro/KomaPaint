// The docking workspace model: the tree of splits, tab groups and floating
// windows that every panel lives in. These tests drive the model directly
// (splitting, merging tabs, resizing, floating, hiding, presets and defensive
// restoration) without a QML engine; TestDockUi covers the same behaviour
// through the real widgets.
#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>

#include <memory>

#include "app/DockModel.h"
#include "app/PanelManager.h"
#include "core/Settings.h"

using namespace komapaint;

namespace {

QVariantMap node(const QVariantMap& tree, int id)
{
    if (tree.isEmpty()) return {};
    if (tree.value(QStringLiteral("id")).toInt() == id) return tree;
    for (const QVariant& child : tree.value(QStringLiteral("children")).toList()) {
        const QVariantMap found = node(child.toMap().value(QStringLiteral("node")).toMap(), id);
        if (!found.isEmpty()) return found;
    }
    return {};
}

QStringList allPanels(const QVariantMap& tree)
{
    QStringList result;
    if (tree.value(QStringLiteral("kind")).toString() == QLatin1String("tabs"))
        result += tree.value(QStringLiteral("panels")).toStringList();
    for (const QVariant& child : tree.value(QStringLiteral("children")).toList())
        result += allPanels(child.toMap().value(QStringLiteral("node")).toMap());
    return result;
}

// The fraction a node has in its parent split (0 when it is the root).
qreal fractionOf(const QVariantMap& tree, int nodeId)
{
    for (const QVariant& child : tree.value(QStringLiteral("children")).toList()) {
        const QVariantMap entry = child.toMap();
        const QVariantMap node = entry.value(QStringLiteral("node")).toMap();
        if (node.value(QStringLiteral("id")).toInt() == nodeId)
            return entry.value(QStringLiteral("fraction")).toDouble();
        const qreal nested = fractionOf(node, nodeId);
        if (nested > 0.0) return nested;
    }
    return 0.0;
}

int countKind(const QVariantMap& tree, const QString& kind)
{
    int count = tree.value(QStringLiteral("kind")).toString() == kind ? 1 : 0;
    for (const QVariant& child : tree.value(QStringLiteral("children")).toList())
        count += countKind(child.toMap().value(QStringLiteral("node")).toMap(), kind);
    return count;
}

// The tab group that directly holds a node, and its index inside that split.
QVariantMap parentOf(const QVariantMap& tree, int id)
{
    if (tree.value(QStringLiteral("kind")).toString() != QLatin1String("split")) return {};
    const QVariantList children = tree.value(QStringLiteral("children")).toList();
    for (int i = 0; i < children.size(); ++i) {
        const QVariantMap child = children.at(i).toMap().value(QStringLiteral("node")).toMap();
        if (child.value(QStringLiteral("id")).toInt() == id) return tree;
        const QVariantMap nested = parentOf(child, id);
        if (!nested.isEmpty()) return nested;
    }
    return {};
}

// Every split must have at least two children, every tab group at least one
// panel, and no panel may appear twice: the invariants of a sane layout.
QString layoutProblem(const QVariantMap& tree)
{
    const QString kind = tree.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("tabs")) {
        const QStringList panels = tree.value(QStringLiteral("panels")).toStringList();
        if (panels.isEmpty()) return QStringLiteral("empty tab group");
        if (QSet<QString>(panels.begin(), panels.end()).size() != panels.size())
            return QStringLiteral("duplicate panel in a tab group");
        return {};
    }
    if (kind == QLatin1String("canvas")) return {};
    if (kind != QLatin1String("split")) return QStringLiteral("unknown node kind ") + kind;
    const QVariantList children = tree.value(QStringLiteral("children")).toList();
    if (children.size() < 2) return QStringLiteral("split with fewer than two children");
    for (const QVariant& child : children) {
        const QString problem = layoutProblem(child.toMap().value(QStringLiteral("node")).toMap());
        if (!problem.isEmpty()) return problem;
    }
    return {};
}

QString uniqueProblem(const QVariantMap& tree)
{
    const QStringList panels = allPanels(tree);
    if (QSet<QString>(panels.begin(), panels.end()).size() != panels.size())
        return QStringLiteral("a panel appears in two groups");
    return {};
}

}  // namespace

class TestDocking : public QObject
{
    Q_OBJECT
private slots:
    void defaultLayoutPlacesEveryPanelOnce();
    void tabIntoExistingGroup();
    void droppingOnAnEdgeSplitsTheGroup();
    void mergingAllPanelsBackLeavesNoEmptyGroups();
    void hidingAndShowingKeepsEveryPanelReachable();
    void collapsedGroupIsRemembered();
    void resizingRespectsMinimumSizes();
    void floatingWindowsCanBeMovedAndDockedBack();
    void serialiseRestoreRoundTrip();
    void restoreDropsUnknownPanelsAndRecoversMissingOnes();
    void restoreDropsDuplicatesAndEmptyGroups();
    void restoreRejectsGarbageWithoutBreakingTheLayout();
    void resetToDefaultRestoresTheInitialWorkspace();
    void presetsRoundTripThroughSettings();
    void layoutPersistsAcrossRestartsWithSameSettings();
    void activeTabDoesNotRebuildTheLayout();
    void panelManagerForwardsToTheSharedModel();
    void dockAreasFollowTheCanvas();
    void collapsingAnAreaLeavesTheLayoutAlone();
    void collapsedAreasPersistAcrossRestart();
    void aHiddenStandalonePanelReturnsToItsSlot();
    void aHiddenSharedPanelReturnsToItsTabPosition();
    void showingAPanelReopensItsCollapsedArea();
    void aStoredLayoutKeepsAreaStateAndPlacements();
};

void TestDocking::defaultLayoutPlacesEveryPanelOnce()
{
    DockModel dock;
    const QVariantMap tree = dock.tree();
    QCOMPARE(countKind(tree, QStringLiteral("canvas")), 1);
    QVERIFY(layoutProblem(tree).isEmpty());
    QVERIFY(uniqueProblem(tree).isEmpty());

    QStringList expected;
    for (const auto& entry : DockModel::panelTable())
        if (entry.visibleByDefault) expected.append(entry.id);
    QStringList actual = allPanels(tree);
    actual.sort();
    expected.sort();
    QCOMPARE(actual, expected);
    QVERIFY(!dock.isVisible(QStringLiteral("timeline")));   // optional panels start hidden
    QCOMPARE(dock.hiddenPanels(), QStringList({ QStringLiteral("timeline"), QStringLiteral("history"),
                                                QStringLiteral("assets"), QStringLiteral("xsheet") }));
    QVERIFY(dock.isVisible(QStringLiteral("layers")));
    QCOMPARE(dock.title(QStringLiteral("layers")), QStringLiteral("Layers"));
    QCOMPARE(dock.title(QStringLiteral("nope")), QString());
    QVERIFY(!dock.isKnown(QStringLiteral("nope")));
}

void TestDocking::tabIntoExistingGroup()
{
    DockModel dock;
    const int layersGroup = dock.groupOf(QStringLiteral("layers"));
    const int colorGroup = dock.groupOf(QStringLiteral("color"));
    QVERIFY(layersGroup > 0);
    QVERIFY(colorGroup > 0);
    QVERIFY(layersGroup != colorGroup);

    QSignalSpy changed(&dock, &DockModel::layoutChanged);
    QVERIFY(dock.moveToGroup(QStringLiteral("color"), layersGroup, 1));
    QCOMPARE(changed.count(), 1);

    const QVariantMap group = dock.nodeById(layersGroup);
    QCOMPARE(group.value(QStringLiteral("panels")).toStringList(),
             QStringList({ QStringLiteral("layers"), QStringLiteral("color") }));
    QCOMPARE(group.value(QStringLiteral("active")).toInt(), 1);
    QCOMPARE(dock.activePanel(), QStringLiteral("color"));
    QCOMPARE(dock.groupOf(QStringLiteral("color")), layersGroup);
    // The old group still exists: Properties was left behind.
    QVERIFY(dock.groupOf(QStringLiteral("properties")) > 0);
    QVERIFY(dock.isVisible(QStringLiteral("properties")));
    QVERIFY(layoutProblem(dock.tree()).isEmpty());
    QVERIFY(uniqueProblem(dock.tree()).isEmpty());

    // Appending without an index puts the tab last.
    QVERIFY(dock.moveToGroup(QStringLiteral("properties"), layersGroup));
    QCOMPARE(dock.nodeById(layersGroup).value(QStringLiteral("panels")).toStringList(),
             QStringList({ QStringLiteral("layers"), QStringLiteral("color"), QStringLiteral("properties") }));
    QVERIFY(layoutProblem(dock.tree()).isEmpty());
}

void TestDocking::droppingOnAnEdgeSplitsTheGroup()
{
    DockModel dock;
    const int layersGroup = dock.groupOf(QStringLiteral("layers"));
    QVERIFY(dock.dockToGroupSide(QStringLiteral("color"), layersGroup, QStringLiteral("bottom")));
    const int colorGroup = dock.groupOf(QStringLiteral("color"));
    QVERIFY(colorGroup > 0);
    QVERIFY(colorGroup != layersGroup);

    // The two groups now share a vertical split, with Layers first.
    const QVariantMap split = parentOf(dock.tree(), layersGroup);
    QVERIFY(!split.isEmpty());
    QCOMPARE(split.value(QStringLiteral("orientation")).toString(), QStringLiteral("vertical"));
    const QVariantList children = split.value(QStringLiteral("children")).toList();
    QCOMPARE(children.size(), 2);
    QCOMPARE(children.at(0).toMap().value(QStringLiteral("node")).toMap().value(QStringLiteral("id")).toInt(), layersGroup);
    QCOMPARE(children.at(1).toMap().value(QStringLiteral("node")).toMap().value(QStringLiteral("id")).toInt(), colorGroup);
    QVERIFY(qAbs(children.at(0).toMap().value(QStringLiteral("fraction")).toDouble() - 0.5) < 0.001);

    // A horizontal edge split wraps too.
    QVERIFY(dock.dockToGroupSide(QStringLiteral("history"), colorGroup, QStringLiteral("right")));
    const QVariantMap row = parentOf(dock.tree(), colorGroup);
    QCOMPARE(row.value(QStringLiteral("orientation")).toString(), QStringLiteral("horizontal"));
    QVERIFY(dock.isVisible(QStringLiteral("history")));
    QVERIFY(layoutProblem(dock.tree()).isEmpty());
    QVERIFY(uniqueProblem(dock.tree()).isEmpty());

    // Docking to the workspace edge adds a whole band.
    QVERIFY(dock.dockToWorkspace(QStringLiteral("assets"), QStringLiteral("left")));
    const QVariantMap root = dock.tree();
    QCOMPARE(root.value(QStringLiteral("orientation")).toString(), QStringLiteral("horizontal"));
    const QVariantList rootChildren = root.value(QStringLiteral("children")).toList();
    QCOMPARE(rootChildren.at(0).toMap().value(QStringLiteral("node")).toMap()
                 .value(QStringLiteral("panels")).toStringList(),
             QStringList({ QStringLiteral("assets") }));
    QVERIFY(layoutProblem(dock.tree()).isEmpty());
}

void TestDocking::mergingAllPanelsBackLeavesNoEmptyGroups()
{
    DockModel dock;
    // Split everything apart first.
    QVERIFY(dock.dockToGroupSide(QStringLiteral("color"), dock.groupOf(QStringLiteral("layers")),
                                 QStringLiteral("top")));
    QVERIFY(dock.dockToGroupSide(QStringLiteral("properties"), dock.groupOf(QStringLiteral("layers")),
                                 QStringLiteral("left")));
    const int target = dock.groupOf(QStringLiteral("layers"));
    for (const QString& id : { QStringLiteral("color"), QStringLiteral("properties"),
                               QStringLiteral("subtool"), QStringLiteral("toolproperty"),
                               QStringLiteral("brushsize"), QStringLiteral("timeline") }) {
        QVERIFY2(dock.moveToGroup(id, target), qPrintable(id));
    }
    const QVariantMap tree = dock.tree();
    QVERIFY2(layoutProblem(tree).isEmpty(), qPrintable(layoutProblem(tree)));
    QVERIFY(uniqueProblem(tree).isEmpty());
    QCOMPARE(dock.nodeById(target).value(QStringLiteral("panels")).toStringList().size(), 7);
    // Everything else is the canvas: a single row of canvas + one tab group.
    QCOMPARE(countKind(tree, QStringLiteral("tabs")), 1);
    QCOMPARE(countKind(tree, QStringLiteral("canvas")), 1);
    QVERIFY(dock.isVisible(QStringLiteral("canvas")) == false);   // canvas is not a panel
}

void TestDocking::hidingAndShowingKeepsEveryPanelReachable()
{
    DockModel dock;
    dock.hidePanel(QStringLiteral("layers"));
    QVERIFY(!dock.isVisible(QStringLiteral("layers")));
    QVERIFY(dock.hiddenPanels().contains(QStringLiteral("layers")));
    QVERIFY(dock.groupOf(QStringLiteral("layers")) < 0);
    // Its neighbours stay where they are.
    QCOMPARE(dock.groupOf(QStringLiteral("properties")), dock.groupOf(QStringLiteral("properties")));
    QVERIFY(dock.isVisible(QStringLiteral("properties")));

    // Hiding every panel of the right column removes that column.
    for (const QString& id : { QStringLiteral("properties"), QStringLiteral("color") })
        dock.hidePanel(id);
    QVERIFY(!dock.isVisible(QStringLiteral("properties")));
    QVERIFY(!dock.isVisible(QStringLiteral("color")));
    QVERIFY(layoutProblem(dock.tree()).isEmpty());

    dock.showPanel(QStringLiteral("color"));
    QVERIFY(dock.isVisible(QStringLiteral("color")));
    QVERIFY(!dock.isVisible(QStringLiteral("properties")));
    QVERIFY(layoutProblem(dock.tree()).isEmpty());

    // Showing a panel that is already visible changes nothing.
    const QVariantMap before = dock.serialise();
    dock.showPanel(QStringLiteral("color"));
    QCOMPARE(dock.serialise(), before);

    // Hidden panels are recorded, and toggle flips.
    dock.togglePanel(QStringLiteral("color"));
    QVERIFY(!dock.isVisible(QStringLiteral("color")));
    QVERIFY(dock.hiddenPanels().contains(QStringLiteral("color")));
    dock.togglePanel(QStringLiteral("color"));
    QVERIFY(dock.isVisible(QStringLiteral("color")));
    QVERIFY(!dock.hiddenPanels().contains(QStringLiteral("color")));
    QVERIFY(uniqueProblem(dock.tree()).isEmpty());
}

void TestDocking::collapsedGroupIsRemembered()
{
    DockModel dock;
    QVERIFY(!dock.isCollapsed(QStringLiteral("layers")));
    QVERIFY(dock.setCollapsed(QStringLiteral("layers"), true));
    QVERIFY(dock.isCollapsed(QStringLiteral("layers")));
    const QVariantMap tree = dock.nodeById(dock.groupOf(QStringLiteral("layers")));
    QCOMPARE(tree.value(QStringLiteral("collapsed")).toBool(), true);
    QVERIFY(dock.setCollapsed(QStringLiteral("layers"), false));
    QVERIFY(!dock.isCollapsed(QStringLiteral("layers")));
    QVERIFY(!dock.setCollapsed(QStringLiteral("nope"), true));
}

void TestDocking::resizingRespectsMinimumSizes()
{
    DockModel dock;
    const QVariantMap root = dock.tree();
    const int splitId = root.value(QStringLiteral("id")).toInt();
    const QVariantList before = root.value(QStringLiteral("children")).toList();

    // Shrink the right column to nothing: clamped so it stays usable.
    dock.setFraction(splitId, 1, 0.99, 1000, 200);
    QVariantList after = dock.nodeById(splitId).value(QStringLiteral("children")).toList();
    QCOMPARE(after.size(), before.size());
    qreal total = 0.0;
    for (const QVariant& child : after) total += child.toMap().value(QStringLiteral("fraction")).toDouble();
    QVERIFY(qAbs(total - 1.0) < 0.001);
    // Only the two children of the dragged handle (centre and right) are
    // constrained; the left column keeps its own share.
    const qreal second = after.at(2).toMap().value(QStringLiteral("fraction")).toDouble();
    QVERIFY2(second >= 0.199, qPrintable(QString::number(second)));            // 200 / 1000
    QVERIFY(qAbs(after.at(0).toMap().value(QStringLiteral("fraction")).toDouble() - 0.185) < 0.001);

    // Growing the left column pushes only its neighbour.
    dock.setFraction(splitId, 0, 0.3, 1000, 200);
    after = dock.nodeById(splitId).value(QStringLiteral("children")).toList();
    const qreal left = after.at(0).toMap().value(QStringLiteral("fraction")).toDouble();
    QVERIFY(qAbs(left - 0.3) < 0.001);
    total = 0.0;
    for (const QVariant& child : after) total += child.toMap().value(QStringLiteral("fraction")).toDouble();
    QVERIFY(qAbs(total - 1.0) < 0.001);

    // Out-of-range requests are ignored rather than corrupting the tree.
    const QVariantMap snapshot = dock.serialise();
    dock.setFraction(splitId, 5, 0.5, 1000, 200);
    dock.setFraction(9999, 0, 0.5, 1000, 200);
    QCOMPARE(dock.serialise(), snapshot);

    // The nested columns can be resized as well.
    const QStringList ids = allPanels(dock.tree());
    QVERIFY(ids.contains(QStringLiteral("layers")));
    const int innerSplit = dock.nodeById(dock.nodeById(dock.groupOf(QStringLiteral("layers")))
                                             .value(QStringLiteral("id")).toInt())
                               .value(QStringLiteral("id")).toInt();
    dock.setFraction(innerSplit, 0, 0.9, 400, 60);
    QVERIFY(layoutProblem(dock.tree()).isEmpty());
}

void TestDocking::floatingWindowsCanBeMovedAndDockedBack()
{
    DockModel dock;
    QSignalSpy geometry(&dock, &DockModel::floatingGeometryChanged);
    QSignalSpy changed(&dock, &DockModel::layoutChanged);
    QVERIFY(dock.floatPanel(QStringLiteral("layers"), 100, 120, 320, 260));
    QCOMPARE(changed.count(), 1);
    QVERIFY(dock.isFloating(QStringLiteral("layers")));
    QVERIFY(!dock.isVisible(QStringLiteral("layers")) || dock.isVisible(QStringLiteral("layers")));
    QVERIFY(dock.isVisible(QStringLiteral("layers")));
    QVERIFY(dock.groupOf(QStringLiteral("layers")) < 0);   // no longer in the tree
    QCOMPARE(dock.floating().size(), 1);
    const QVariantMap window = dock.floating().first().toMap();
    QCOMPARE(window.value(QStringLiteral("x")).toInt(), 100);
    QCOMPARE(window.value(QStringLiteral("width")).toInt(), 320);
    const int floatingId = window.value(QStringLiteral("id")).toInt();
    QCOMPARE(dock.floatingIdOf(QStringLiteral("layers")), floatingId);

    // Moving a floating window does not rebuild the dock tree.
    changed.clear();
    dock.setFloatingGeometry(floatingId, 200, 180, 400, 300);
    QCOMPARE(geometry.count(), 1);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(dock.floating().first().toMap().value(QStringLiteral("width")).toInt(), 400);
    dock.setFloatingGeometry(floatingId, 200, 180, 100, 60);   // clamped to a usable size
    const QVariantMap clamped = dock.floating().first().toMap();
    QVERIFY(clamped.value(QStringLiteral("width")).toInt() >= 200);
    QVERIFY(clamped.value(QStringLiteral("height")).toInt() >= 160);

    QVERIFY(dock.dockBack(floatingId));
    QVERIFY(!dock.isFloating(QStringLiteral("layers")));
    QVERIFY(dock.isVisible(QStringLiteral("layers")));
    QVERIFY(dock.floating().isEmpty());
    QVERIFY(dock.groupOf(QStringLiteral("layers")) > 0);
    QVERIFY(layoutProblem(dock.tree()).isEmpty());
    QVERIFY(uniqueProblem(dock.tree()).isEmpty());
    QVERIFY(!dock.dockBack(floatingId));   // already docked
}

void TestDocking::serialiseRestoreRoundTrip()
{
    DockModel dock;
    QVERIFY(dock.moveToGroup(QStringLiteral("color"), dock.groupOf(QStringLiteral("layers"))));
    QVERIFY(dock.dockToGroupSide(QStringLiteral("timeline"), dock.groupOf(QStringLiteral("layers")),
                                 QStringLiteral("bottom")));
    dock.hidePanel(QStringLiteral("brushsize"));
    QVERIFY(dock.setCollapsed(QStringLiteral("properties"), true));
    QVERIFY(dock.floatPanel(QStringLiteral("subtool"), 40, 60, 300, 280));
    dock.setFraction(dock.tree().value(QStringLiteral("id")).toInt(), 0, 0.28, 1200, 200);
    const QVariantMap saved = dock.serialise();

    DockModel other;
    QSignalSpy changed(&other, &DockModel::layoutChanged);
    QVERIFY(other.restore(saved));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(other.serialise(), saved);
    QVERIFY(other.isCollapsed(QStringLiteral("properties")));
    QVERIFY(!other.isVisible(QStringLiteral("brushsize")));
    QVERIFY(other.isFloating(QStringLiteral("subtool")));
}

void TestDocking::restoreDropsUnknownPanelsAndRecoversMissingOnes()
{
    DockModel dock;
    QVariantMap stored = dock.serialise();

    // A layout from a future version: one panel KomaPaint no longer has, and no
    // mention of a panel this build does have.
    QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(stored)).toJson(QJsonDocument::Compact);
    json.replace("\"color\"", "\"ghost\"");
    const QVariantMap tampered = QJsonDocument::fromJson(json).object().toVariantMap();
    QVERIFY(dock.restore(tampered));

    QVERIFY(!dock.isKnown(QStringLiteral("ghost")));
    QVERIFY(!allPanels(dock.tree()).contains(QStringLiteral("ghost")));
    // The panel that vanished from the stored layout is put back so it cannot
    // become unreachable.
    QVERIFY(dock.isVisible(QStringLiteral("color")));
    QVERIFY(layoutProblem(dock.tree()).isEmpty());
    QVERIFY(uniqueProblem(dock.tree()).isEmpty());

    // A panel that did not exist when the layout was saved is treated the same.
    dock.hidePanel(QStringLiteral("history"));
    QVERIFY(!dock.isVisible(QStringLiteral("history")));
}

void TestDocking::restoreDropsDuplicatesAndEmptyGroups()
{
    DockModel dock;
    QVariantMap stored = dock.serialise();
    // Put "layers" into the Sub Tool group as well: it now appears twice.
    QVariantMap root = stored.value(QStringLiteral("root")).toMap();
    QVariantList children = root.value(QStringLiteral("children")).toList();
    QVariantMap left = children.at(0).toMap().value(QStringLiteral("node")).toMap();
    QVariantList leftChildren = left.value(QStringLiteral("children")).toList();
    QVariantMap group = leftChildren.at(0).toMap().value(QStringLiteral("node")).toMap();
    group.insert(QStringLiteral("panels"), QStringList({ QStringLiteral("subtool"), QStringLiteral("layers") }));
    leftChildren[0] = QVariantMap({ { QStringLiteral("fraction"), 0.22 }, { QStringLiteral("node"), group } });
    left.insert(QStringLiteral("children"), leftChildren);
    children[0] = QVariantMap({ { QStringLiteral("fraction"), 0.185 }, { QStringLiteral("node"), left } });
    root.insert(QStringLiteral("children"), children);
    stored.insert(QStringLiteral("root"), root);

    QVERIFY(dock.restore(stored));
    const QStringList panels = allPanels(dock.tree());
    QCOMPARE(panels.count(QStringLiteral("layers")), 1);
    QCOMPARE(dock.groupOf(QStringLiteral("layers")), dock.groupOf(QStringLiteral("subtool")));
    QVERIFY(layoutProblem(dock.tree()).isEmpty());
    QVERIFY(uniqueProblem(dock.tree()).isEmpty());

    // An empty group in a stored layout is pruned instead of drawn as a blank box.
    QVariantMap again = dock.serialise();
    QVariantMap againRoot = again.value(QStringLiteral("root")).toMap();
    QVariantList againChildren = againRoot.value(QStringLiteral("children")).toList();
    QVariantMap right = againChildren.at(2).toMap().value(QStringLiteral("node")).toMap();
    QVariantList rightChildren = right.value(QStringLiteral("children")).toList();
    QVariantMap emptyGroup = rightChildren.at(0).toMap().value(QStringLiteral("node")).toMap();
    emptyGroup.insert(QStringLiteral("panels"), QStringList());
    rightChildren[0] = QVariantMap({ { QStringLiteral("fraction"), 0.42 }, { QStringLiteral("node"), emptyGroup } });
    right.insert(QStringLiteral("children"), rightChildren);
    againChildren[2] = QVariantMap({ { QStringLiteral("fraction"), 0.18 }, { QStringLiteral("node"), right } });
    againRoot.insert(QStringLiteral("children"), againChildren);
    again.insert(QStringLiteral("root"), againRoot);
    QVERIFY(dock.restore(again));
    QVERIFY(layoutProblem(dock.tree()).isEmpty());
}

void TestDocking::restoreRejectsGarbageWithoutBreakingTheLayout()
{
    DockModel dock;
    const QVariantMap before = dock.serialise();
    QVERIFY(!dock.restore({}));
    QCOMPARE(dock.serialise(), before);

    QVariantMap wrongVersion = before;
    wrongVersion.insert(QStringLiteral("version"), 99);
    QVERIFY(!dock.restore(wrongVersion));
    QCOMPARE(dock.serialise(), before);

    QVariantMap noRoot = before;
    noRoot.remove(QStringLiteral("root"));
    QVERIFY(!dock.restore(noRoot));
    QCOMPARE(dock.serialise(), before);
    QVERIFY(layoutProblem(dock.tree()).isEmpty());
}

void TestDocking::resetToDefaultRestoresTheInitialWorkspace()
{
    DockModel dock;
    const QVariantMap initial = dock.serialise();
    QVERIFY(dock.moveToGroup(QStringLiteral("color"), dock.groupOf(QStringLiteral("layers"))));
    QVERIFY(dock.floatPanel(QStringLiteral("subtool"), 10, 10, 300, 300));
    dock.hidePanel(QStringLiteral("properties"));
    QVERIFY(dock.setCollapsed(QStringLiteral("layers"), true));

    QSignalSpy changed(&dock, &DockModel::layoutChanged);
    dock.resetToDefault();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(dock.serialise(), initial);
    QVERIFY(!dock.isCollapsed(QStringLiteral("layers")));
    QVERIFY(dock.isVisible(QStringLiteral("properties")));
    QVERIFY(dock.floating().isEmpty());
}

void TestDocking::presetsRoundTripThroughSettings()
{
    Settings settings(QStringLiteral("KomaPaintTests"), QStringLiteral("DockPresets"));
    settings.setValue(QStringLiteral("workspace/presets"), QString());
    settings.setValue(QStringLiteral("workspace/layout"), QString());
    DockModel dock;
    dock.setSettings(&settings);

    QVERIFY(dock.presetNames().isEmpty());
    const QVariantMap standard = dock.serialise();
    QVERIFY(dock.savePreset(QStringLiteral("Standard")));
    QVERIFY(!dock.savePreset(QStringLiteral("   ")));

    QVERIFY(dock.moveToGroup(QStringLiteral("color"), dock.groupOf(QStringLiteral("layers"))));
    QVERIFY(dock.floatPanel(QStringLiteral("history"), 20, 20, 320, 240));
    QVERIFY(dock.savePreset(QStringLiteral("Wide")));
    QCOMPARE(dock.presetNames(), QStringList({ QStringLiteral("Standard"), QStringLiteral("Wide") }));

    QVERIFY(dock.applyPreset(QStringLiteral("Standard")));
    QCOMPARE(dock.serialise(), standard);
    QVERIFY(dock.applyPreset(QStringLiteral("Wide")));
    QVERIFY(dock.isFloating(QStringLiteral("history")));
    QVERIFY(!dock.applyPreset(QStringLiteral("Missing")));

    QVERIFY(dock.deletePreset(QStringLiteral("Standard")));
    QCOMPARE(dock.presetNames(), QStringList({ QStringLiteral("Wide") }));
    QVERIFY(!dock.deletePreset(QStringLiteral("Standard")));
}

void TestDocking::layoutPersistsAcrossRestartsWithSameSettings()
{
    Settings settings(QStringLiteral("KomaPaintTests"), QStringLiteral("DockPersistence"));
    settings.setValue(QStringLiteral("workspace/layout"), QString());
    {
        DockModel dock;
        dock.setSettings(&settings);
        QVERIFY(dock.moveToGroup(QStringLiteral("layers"), dock.groupOf(QStringLiteral("color"))));
        dock.hidePanel(QStringLiteral("toolproperty"));
        dock.showPanel(QStringLiteral("timeline"));
        QVERIFY(dock.setCollapsed(QStringLiteral("timeline"), true));
        QVERIFY(dock.floatPanel(QStringLiteral("assets"), 80, 90, 340, 260));
        // The model writes back on a short debounce instead of on every frame.
        QTRY_VERIFY_WITH_TIMEOUT(!settings.value(QStringLiteral("workspace/layout")).toString().isEmpty(), 3000);
    }
    DockModel restored;
    restored.setSettings(&settings);
    QCOMPARE(restored.groupOf(QStringLiteral("layers")), restored.groupOf(QStringLiteral("color")));
    QVERIFY(!restored.isVisible(QStringLiteral("toolproperty")));
    QVERIFY(restored.isFloating(QStringLiteral("assets")));
    QVERIFY(restored.isCollapsed(QStringLiteral("timeline")));
    QCOMPARE(restored.floating().first().toMap().value(QStringLiteral("width")).toInt(), 340);

    // A corrupt stored layout falls back to the default workspace instead of
    // leaving an empty window.
    settings.setValue(QStringLiteral("workspace/layout"), QStringLiteral("{not json"));
    DockModel fallback;
    fallback.setSettings(&settings);
    QVERIFY(fallback.isVisible(QStringLiteral("layers")));
    QVERIFY(fallback.isVisible(QStringLiteral("subtool")));
    QVERIFY(layoutProblem(fallback.tree()).isEmpty());
}

void TestDocking::activeTabDoesNotRebuildTheLayout()
{
    DockModel dock;
    const int group = dock.groupOf(QStringLiteral("layers"));
    QVERIFY(dock.moveToGroup(QStringLiteral("color"), group));
    QSignalSpy layout(&dock, &DockModel::layoutChanged);
    QSignalSpy active(&dock, &DockModel::activeTabChanged);
    dock.setActive(QStringLiteral("layers"));
    QCOMPARE(layout.count(), 0);
    QCOMPARE(active.count(), 1);
    QCOMPARE(dock.activePanel(), QStringLiteral("layers"));
    QCOMPARE(dock.nodeById(group).value(QStringLiteral("active")).toInt(), 0);
    dock.setActive(QStringLiteral("layers"));   // no change, no signal
    QCOMPARE(active.count(), 1);
    QVERIFY(dock.moveToGroup(QStringLiteral("color"), group, 1) == false);   // already exactly there
    QVERIFY(dock.moveToGroup(QStringLiteral("color"), group, 0));            // a real reorder
    QCOMPARE(dock.nodeById(group).value(QStringLiteral("panels")).toStringList(),
             QStringList({ QStringLiteral("color"), QStringLiteral("layers") }));
}

void TestDocking::dockAreasFollowTheCanvas()
{
    DockModel dock;
    // The default workspace is a row of [left column | canvas | right column].
    QVariantList areas = dock.areas();
    QCOMPARE(areas.size(), 2);
    QCOMPARE(areas.at(0).toMap().value(QStringLiteral("side")).toString(), QStringLiteral("left"));
    QCOMPARE(areas.at(1).toMap().value(QStringLiteral("side")).toString(), QStringLiteral("right"));
    QVERIFY(dock.hasArea(QStringLiteral("left")));
    QVERIFY(dock.hasArea(QStringLiteral("right")));
    QVERIFY(!dock.hasArea(QStringLiteral("bottom")));
    QVERIFY(dock.hasArea(QStringLiteral("LEFT")));   // sides are case-insensitive
    QVERIFY(!dock.hasArea(QStringLiteral("middle")));

    // A panel is inside an area; the canvas is not.
    QCOMPARE(dock.areaSideOf(dock.groupOf(QStringLiteral("layers"))), QStringLiteral("right"));
    QCOMPARE(dock.areaSideOf(dock.groupOf(QStringLiteral("subtool"))), QStringLiteral("left"));
    QCOMPARE(dock.areaSideOfChild(dock.tree().value(QStringLiteral("children")).toList()
                                      .at(0).toMap().value(QStringLiteral("node")).toMap()
                                      .value(QStringLiteral("id")).toInt()),
             QStringLiteral("left"));
    // The canvas has no area of its own.
    const QVariantMap tree = dock.tree();
    int canvasId = -1;
    for (const QVariant& child : tree.value(QStringLiteral("children")).toList()) {
        const QVariantMap node = child.toMap().value(QStringLiteral("node")).toMap();
        if (node.value(QStringLiteral("kind")).toString() == QLatin1String("canvas"))
            canvasId = node.value(QStringLiteral("id")).toInt();
    }
    QVERIFY(canvasId > 0);
    QVERIFY(dock.areaSideOf(canvasId).isEmpty());
    QVERIFY(dock.areaSideOfChild(canvasId).isEmpty());

    // A panel docked under the canvas makes a bottom area without losing the
    // side areas around the split the canvas now sits in.
    QVERIFY(dock.dockToWorkspace(QStringLiteral("timeline"), QStringLiteral("bottom")));
    QVERIFY(dock.hasArea(QStringLiteral("bottom")));
    QVERIFY(dock.hasArea(QStringLiteral("left")));
    QVERIFY(dock.hasArea(QStringLiteral("right")));
    areas = dock.areas();
    QCOMPARE(areas.size(), 3);
    bool sawBottom = false;
    for (const QVariant& area : areas)
        sawBottom = sawBottom || area.toMap().value(QStringLiteral("side")).toString()
                                    == QLatin1String("bottom");
    QVERIFY(sawBottom);
    QCOMPARE(dock.areaSideOf(dock.groupOf(QStringLiteral("timeline"))), QStringLiteral("bottom"));
}

void TestDocking::collapsingAnAreaLeavesTheLayoutAlone()
{
    DockModel dock;
    const int group = dock.groupOf(QStringLiteral("layers"));
    const qreal fraction = fractionOf(dock.tree(), group);
    const QVariantMap rootBefore = dock.tree();
    const QVariantList fractionsBefore = dock.fractionsOf(
        rootBefore.value(QStringLiteral("id")).toInt());

    QVERIFY(dock.setAreaCollapsed(QStringLiteral("left"), true));
    QVERIFY(dock.isAreaCollapsed(QStringLiteral("left")));
    QVERIFY(!dock.isAreaCollapsed(QStringLiteral("right")));
    QCOMPARE(dock.collapsedAreas(), QStringList{ QStringLiteral("left") });
    // Nothing about the tree changed: same fractions, same panels, same tabs.
    QCOMPARE(dock.tree(), rootBefore);
    QCOMPARE(dock.fractionsOf(rootBefore.value(QStringLiteral("id")).toInt()), fractionsBefore);
    QCOMPARE(fractionOf(dock.tree(), group), fraction);
    QVERIFY(dock.isVisible(QStringLiteral("subtool")));
    QVERIFY(dock.isVisible(QStringLiteral("brushsize")));

    // The active tab of a collapsed area is still its active tab.
    dock.setActive(QStringLiteral("toolproperty"));
    QCOMPARE(dock.activePanel(), QStringLiteral("toolproperty"));

    QVERIFY(dock.toggleArea(QStringLiteral("left")));
    QVERIFY(!dock.isAreaCollapsed(QStringLiteral("left")));
    QVERIFY(dock.collapsedAreas().isEmpty());
    QVERIFY(dock.toggleArea(QStringLiteral("right")));
    QVERIFY(dock.isAreaCollapsed(QStringLiteral("right")));

    // A side that does not exist cannot be collapsed.
    QVERIFY(!dock.setAreaCollapsed(QStringLiteral("bottom"), true));
    QVERIFY(!dock.hasArea(QStringLiteral("bottom")));
    QVERIFY(layoutProblem(dock.tree()).isEmpty());
    QVERIFY(uniqueProblem(dock.tree()).isEmpty());
}

void TestDocking::collapsedAreasPersistAcrossRestart()
{
    Settings settings(QStringLiteral("KomaPaintTests"), QStringLiteral("DockAreaPersistence"));
    settings.setValue(QStringLiteral("workspace/layout"), QString());
    {
        DockModel dock;
        dock.setSettings(&settings);
        QVERIFY(dock.setAreaCollapsed(QStringLiteral("left"), true));
        QVERIFY(dock.setAreaCollapsed(QStringLiteral("right"), true));
        QVERIFY(dock.dockToWorkspace(QStringLiteral("timeline"), QStringLiteral("bottom")));
        QTRY_VERIFY_WITH_TIMEOUT(!settings.value(QStringLiteral("workspace/layout")).toString().isEmpty(),
                                 3000);
    }
    DockModel restored;
    restored.setSettings(&settings);
    QVERIFY(restored.isAreaCollapsed(QStringLiteral("left")));
    QVERIFY(restored.isAreaCollapsed(QStringLiteral("right")));
    QVERIFY(!restored.isAreaCollapsed(QStringLiteral("bottom")));
    QCOMPARE(restored.collapsedAreas().size(), 2);
    // Collapsing an area never hides its panels: they are still loaded.
    QVERIFY(restored.isVisible(QStringLiteral("layers")));
    QVERIFY(restored.isVisible(QStringLiteral("subtool")));
    QVERIFY(!restored.hiddenPanels().contains(QStringLiteral("layers")));

    // A stored side that does not exist in the layout is dropped, not kept.
    QJsonObject stored = QJsonDocument::fromJson(
        settings.value(QStringLiteral("workspace/layout")).toString().toUtf8()).object();
    stored.insert(QStringLiteral("collapsedAreas"),
                  QJsonArray{ QStringLiteral("left"), QStringLiteral("sideways") });
    settings.setValue(QStringLiteral("workspace/layout"),
                      QString::fromUtf8(QJsonDocument(stored).toJson(QJsonDocument::Compact)));
    DockModel sanitised;
    sanitised.setSettings(&settings);
    QCOMPARE(sanitised.collapsedAreas(), QStringList{ QStringLiteral("left") });
}

void TestDocking::aHiddenStandalonePanelReturnsToItsSlot()
{
    DockModel dock;
    const QString panel = QStringLiteral("properties");
    const int group = dock.groupOf(panel);
    const QVariantMap parentBefore = parentOf(dock.tree(), group);
    QCOMPARE(parentBefore.value(QStringLiteral("children")).toList().size(), 3);
    const qreal fraction = fractionOf(dock.tree(), group);
    QVERIFY(fraction > 0.05);

    dock.hidePanel(panel);
    QVERIFY(!dock.isVisible(panel));
    QVERIFY(dock.tree().value(QStringLiteral("children")).toList().size() > 0);

    dock.showPanel(panel);
    QVERIFY(dock.isVisible(panel));
    // It comes back in a split of its own next to the panel it used to sit
    // beside, with the share of the column it had.
    const int restored = dock.groupOf(panel);
    QVERIFY(restored > 0);
    const QVariantMap parent = parentOf(dock.tree(), restored);
    QCOMPARE(parent.value(QStringLiteral("children")).toList().size(), 2);
    QVERIFY(qAbs(fractionOf(dock.tree(), restored) - fraction) < 1e-6);
    QStringList siblings;
    for (const QVariant& child : parent.value(QStringLiteral("children")).toList())
        siblings += allPanels(child.toMap().value(QStringLiteral("node")).toMap());
    QVERIFY(siblings.contains(panel));
    QVERIFY(siblings.contains(QStringLiteral("color")) || siblings.contains(QStringLiteral("layers")));
    QVERIFY(layoutProblem(dock.tree()).isEmpty());
    QVERIFY(uniqueProblem(dock.tree()).isEmpty());
}

void TestDocking::aHiddenSharedPanelReturnsToItsTabPosition()
{
    DockModel dock;
    const int layersGroup = dock.groupOf(QStringLiteral("layers"));
    QVERIFY(dock.moveToGroup(QStringLiteral("color"), layersGroup, 0));
    QCOMPARE(dock.nodeById(layersGroup).value(QStringLiteral("panels")).toStringList(),
             (QStringList{ QStringLiteral("color"), QStringLiteral("layers") }));

    dock.hidePanel(QStringLiteral("color"));
    QVERIFY(!dock.isVisible(QStringLiteral("color")));
    dock.showPanel(QStringLiteral("color"));
    QVERIFY(dock.isVisible(QStringLiteral("color")));
    // The group never went away, so the panel simply returns to its tab slot.
    QCOMPARE(dock.groupOf(QStringLiteral("color")), layersGroup);
    QCOMPARE(dock.nodeById(layersGroup).value(QStringLiteral("panels")).toStringList(),
             (QStringList{ QStringLiteral("color"), QStringLiteral("layers") }));

    // Hiding the last panel of a group and bringing it back keeps the group.
    dock.hidePanel(QStringLiteral("layers"));
    dock.showPanel(QStringLiteral("layers"));
    QCOMPARE(dock.groupOf(QStringLiteral("layers")), layersGroup);
    QVERIFY(dock.isVisible(QStringLiteral("color")));
}

void TestDocking::showingAPanelReopensItsCollapsedArea()
{
    DockModel dock;
    QVERIFY(dock.setAreaCollapsed(QStringLiteral("right"), true));
    dock.hidePanel(QStringLiteral("color"));
    QVERIFY(dock.isAreaCollapsed(QStringLiteral("right")));
    dock.showPanel(QStringLiteral("color"));
    QVERIFY(!dock.isAreaCollapsed(QStringLiteral("right")));
    QVERIFY(dock.isVisible(QStringLiteral("color")));

    // Docking a new panel into a collapsed area opens it too.
    QVERIFY(dock.setAreaCollapsed(QStringLiteral("left"), true));
    QVERIFY(dock.dockToWorkspace(QStringLiteral("history"), QStringLiteral("left")));
    QVERIFY(!dock.isAreaCollapsed(QStringLiteral("left")));
    QVERIFY(dock.isVisible(QStringLiteral("history")));

    // The same has to hold for a band under the canvas, which docks through a
    // different code path.
    QVERIFY(dock.dockToWorkspace(QStringLiteral("timeline"), QStringLiteral("bottom")));
    QVERIFY(dock.hasArea(QStringLiteral("bottom")));
    QVERIFY(dock.setAreaCollapsed(QStringLiteral("bottom"), true));
    QVERIFY(dock.dockToWorkspace(QStringLiteral("xsheet"), QStringLiteral("bottom")));
    QVERIFY(!dock.isAreaCollapsed(QStringLiteral("bottom")));
    QVERIFY(dock.isVisible(QStringLiteral("xsheet")));
    QCOMPARE(dock.areaSideOf(dock.groupOf(QStringLiteral("xsheet"))), QStringLiteral("bottom"));
    // It joins the band that is already there instead of making a second one,
    // so a side always has exactly one strip.
    QCOMPARE(dock.groupOf(QStringLiteral("xsheet")), dock.groupOf(QStringLiteral("timeline")));
    // History was docked to the left as a column of its own, so the left side
    // has two nodes; `areas()` still reports one entry per side for the menus.
    QStringList sides;
    for (const QVariant& area : dock.areas()) {
        const QString side = area.toMap().value(QStringLiteral("side")).toString();
        QVERIFY(!sides.contains(side));
        sides.append(side);
    }
    QCOMPARE(sides.size(), 3);
    QVERIFY(sides.contains(QStringLiteral("left")));
    QVERIFY(sides.contains(QStringLiteral("right")));
    QVERIFY(sides.contains(QStringLiteral("bottom")));
}

void TestDocking::aStoredLayoutKeepsAreaStateAndPlacements()
{
    DockModel dock;
    QVERIFY(dock.dockToWorkspace(QStringLiteral("timeline"), QStringLiteral("bottom")));
    QVERIFY(dock.setAreaCollapsed(QStringLiteral("bottom"), true));
    dock.hidePanel(QStringLiteral("properties"));
    const QVariantMap stored = dock.serialise();
    QCOMPARE(stored.value(QStringLiteral("collapsedAreas")).toStringList(),
             QStringList{ QStringLiteral("bottom") });
    const QVariantList stash = stored.value(QStringLiteral("stash")).toList();
    QCOMPARE(stash.size(), 1);
    QCOMPARE(stash.at(0).toMap().value(QStringLiteral("panel")).toString(), QStringLiteral("properties"));
    QVERIFY(stash.at(0).toMap().value(QStringLiteral("anchor")).toInt() > 0);

    DockModel restored;
    QVERIFY(restored.restore(stored));
    QVERIFY(restored.isAreaCollapsed(QStringLiteral("bottom")));
    QVERIFY(!restored.isVisible(QStringLiteral("properties")));
    // The remembered slot still works after the restart.
    restored.showPanel(QStringLiteral("properties"));
    QVERIFY(restored.isVisible(QStringLiteral("properties")));
    QVERIFY(restored.groupOf(QStringLiteral("properties")) > 0);

    // A stored placement that points at a group which no longer exists falls
    // back to the panel's home side instead of losing it.
    QVariantMap broken = stored;
    QVariantList brokenStash = broken.value(QStringLiteral("stash")).toList();
    QVariantMap entry = brokenStash.at(0).toMap();
    entry.insert(QStringLiteral("anchor"), 99999);
    entry.insert(QStringLiteral("group"), 99999);
    brokenStash[0] = entry;
    broken.insert(QStringLiteral("stash"), brokenStash);
    DockModel fallback;
    QVERIFY(fallback.restore(broken));
    fallback.showPanel(QStringLiteral("properties"));
    QVERIFY(fallback.isVisible(QStringLiteral("properties")));
    QVERIFY(layoutProblem(fallback.tree()).isEmpty());
}

void TestDocking::panelManagerForwardsToTheSharedModel()
{
    DockModel dock;
    PanelManager panels;
    panels.setDockModel(&dock);
    QSignalSpy visibility(&panels, &PanelManager::panelVisibilityChanged);
    QSignalSpy visibleChanged(&panels, &PanelManager::visiblePanelsChanged);

    QVERIFY(panels.isVisible(QStringLiteral("layers")));
    panels.setVisible(QStringLiteral("layers"), false);
    QVERIFY(!dock.isVisible(QStringLiteral("layers")));
    QVERIFY(!panels.isVisible(QStringLiteral("layers")));
    QCOMPARE(visibility.count(), 1);
    QCOMPARE(visibility.first().at(0).toString(), QStringLiteral("layers"));
    QCOMPARE(visibility.first().at(1).toBool(), false);
    QVERIFY(visibleChanged.count() >= 1);
    QVERIFY(!panels.visiblePanels().contains(QStringLiteral("layers")));

    panels.toggle(QStringLiteral("layers"));
    QVERIFY(dock.isVisible(QStringLiteral("layers")));

    // Changes made through the model reach the façade.
    dock.hidePanel(QStringLiteral("color"));
    QVERIFY(!panels.isVisible(QStringLiteral("color")));
    QVERIFY(visibility.count() >= 3);

    panels.resetLayout();
    QVERIFY(panels.isVisible(QStringLiteral("color")));
    QVERIFY(layoutProblem(dock.tree()).isEmpty());

    // The dock areas are forwarded too: the QML menus drive them through the
    // façade, so it must report and change the same state as the model.
    QCOMPARE(panels.areas().size(), dock.areas().size());
    QCOMPARE(panels.areas().at(0).toMap().value(QStringLiteral("side")).toString(),
             QStringLiteral("left"));
    QVERIFY(panels.hasArea(QStringLiteral("left")));
    QVERIFY(!panels.hasArea(QStringLiteral("bottom")));
    QCOMPARE(panels.areaSideOf(dock.groupOf(QStringLiteral("layers"))), QStringLiteral("right"));
    // A group inside a column is *in* the area, it is not the area itself: the
    // strip belongs to the column node, which is the root split's child.
    QCOMPARE(panels.areaSideOf(dock.groupOf(QStringLiteral("subtool"))), QStringLiteral("left"));
    QVERIFY(panels.areaSideOfChild(dock.groupOf(QStringLiteral("subtool"))).isEmpty());
    QCOMPARE(panels.areaSideOfChild(panels.areas().first().toMap()
                                        .value(QStringLiteral("nodeId")).toInt()),
             QStringLiteral("left"));
    QVERIFY(panels.toggleArea(QStringLiteral("left")));
    QVERIFY(dock.isAreaCollapsed(QStringLiteral("left")));
    QVERIFY(panels.isAreaCollapsed(QStringLiteral("left")));
    QCOMPARE(panels.collapsedAreas(), QStringList{ QStringLiteral("left") });
    QVERIFY(panels.setAreaCollapsed(QStringLiteral("left"), false));
    QVERIFY(!dock.isAreaCollapsed(QStringLiteral("left")));
    QVERIFY(panels.collapsedAreas().isEmpty());

    // The façade still works on its own, for embedders without an app object.
    PanelManager standalone;
    QVERIFY(standalone.isVisible(QStringLiteral("subtool")));
    standalone.setVisible(QStringLiteral("subtool"), false);
    QVERIFY(!standalone.isVisible(QStringLiteral("subtool")));
    standalone.resetLayout();
    QVERIFY(standalone.isVisible(QStringLiteral("subtool")));
}

QTEST_MAIN(TestDocking)
#include "TestDocking.moc"
