#pragma once

#include <QObject>
#include <QRect>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <vector>

class QTimer;

namespace komapaint {

class Settings;

// The workspace layout: one tree of dock nodes shared by every panel.
//
//   split  - a row (horizontal) or column (vertical) of children with fractions
//   tabs   - a tab group: one or more panels, one of them active
//   canvas - the drawing surface; it cannot be closed, but panels dock around
//            it like around any other node
//
// Every panel id appears at most once in the tree, at most once in a floating
// window, or is hidden. Panels added to KomaPaint later only need an entry in
// panelTable() and a QML component; layout, persistence, presets and the menus
// all work from that table, so nothing here is specific to the drawing
// workspace.
//
// A *dock area* is one border region of the root split: the children on one
// side of the canvas node (left, right, top or bottom). Collapsing an area
// only hides it — the nodes, their fractions, their tabs and the panel items
// stay exactly as they were — so expanding it again restores the previous
// arrangement, sizes and tab selection.
//
// Layouts are stored as JSON in QSettings ("workspace/layout" and
// "workspace/presets") and restored on the next start. Restoring is defensive:
// unknown panels are dropped, duplicates keep their first occurrence, empty
// groups are pruned, and a panel that exists in this build but not in the
// stored layout is put back at its home side so it can never become
// unreachable.
class DockModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap tree READ tree NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList floating READ floating NOTIFY layoutChanged)
    Q_PROPERTY(QStringList hiddenPanels READ hiddenPanels NOTIFY layoutChanged)
    Q_PROPERTY(QString activePanel READ activePanel NOTIFY activeTabChanged)
    Q_PROPERTY(QStringList collapsedAreas READ collapsedAreas NOTIFY layoutChanged)

public:
    struct PanelInfo {
        QString id;
        QString title;
        QString home;        // "left", "right" or "bottom"
        bool visibleByDefault = true;
    };

    explicit DockModel(QObject* parent = nullptr);
    ~DockModel() override;

    // Attaching settings turns persistence on: the stored layout is loaded
    // immediately and every later change is written back (debounced).
    void setSettings(Settings* settings);
    Settings* settings() const { return m_settings; }

    static const QList<PanelInfo>& panelTable();
    const PanelInfo* info(const QString& panelId) const;
    bool isKnownPanel(const QString& panelId) const;

    QVariantMap tree() const;
    QVariantList floating() const;
    QStringList hiddenPanels() const;
    QString activePanel() const;

    Q_INVOKABLE QString title(const QString& panelId) const;
    Q_INVOKABLE bool isVisible(const QString& panelId) const;
    Q_INVOKABLE bool isCollapsed(const QString& panelId) const;
    Q_INVOKABLE bool isFloating(const QString& panelId) const;
    Q_INVOKABLE bool isKnown(const QString& panelId) const { return isKnownPanel(panelId); }
    Q_INVOKABLE int groupOf(const QString& panelId) const;      // tabs node id, or -1
    Q_INVOKABLE int floatingIdOf(const QString& panelId) const;
    Q_INVOKABLE QVariantMap nodeById(int nodeId) const;
    Q_INVOKABLE QVariantList fractionsOf(int splitId) const;
    Q_INVOKABLE bool groupHasPanel(int nodeId, const QString& panelId) const;
    Q_INVOKABLE QStringList visiblePanels() const;

    // Re-arranging. Each returns false when the move is not possible (unknown
    // panel, a panel dropped on itself, a target that is not visible).
    Q_INVOKABLE bool moveToGroup(const QString& panelId, int groupId, int index = -1);
    Q_INVOKABLE bool dockToGroupSide(const QString& panelId, int groupId, const QString& side);
    // Same, anchored on any node: a tab group or the canvas.
    Q_INVOKABLE bool dockToNodeSide(const QString& panelId, int nodeId, const QString& side);
    Q_INVOKABLE bool moveToTabs(const QString& panelId, const QString& targetPanelId, int index = -1);
    Q_INVOKABLE bool dockToSide(const QString& panelId, const QString& targetPanelId, const QString& side);
    Q_INVOKABLE bool dockToWorkspace(const QString& panelId, const QString& side);
    Q_INVOKABLE bool floatPanel(const QString& panelId, int x, int y, int width, int height);
    Q_INVOKABLE bool dockBack(int floatingId);
    Q_INVOKABLE void setFloatingGeometry(int floatingId, int x, int y, int width, int height);
    Q_INVOKABLE void setActive(const QString& panelId);
    Q_INVOKABLE bool setCollapsed(const QString& panelId, bool collapsed);
    Q_INVOKABLE void setFraction(int splitId, int index, qreal fraction, int extent, int minPixels);

    // Dock areas. `areas()` lists the border regions the root split currently
    // has ({side, nodeId, collapsed, panels}); `areaSideOf(nodeId)` says which
    // area a node belongs to (empty when it is not inside one), so the QML side
    // can draw a strip where an area used to be. Collapsing keeps every panel
    // alive and every fraction untouched.
    Q_INVOKABLE QVariantList areas() const;
    // Every panel the model knows about, in table order: id, title, home side,
    // whether it is currently visible. The shell builds its Panels menu from
    // this, so the list of panels has exactly one home.
    Q_INVOKABLE QVariantList panels() const;
    Q_INVOKABLE QString areaSideOf(int nodeId) const;
    // Side of a node that *is* an area (a direct child of the split it belongs
    // to), empty for everything inside one — the view needs that distinction to
    // draw exactly one strip per area.
    Q_INVOKABLE QString areaSideOfChild(int nodeId) const;
    Q_INVOKABLE bool hasArea(const QString& side) const;
    Q_INVOKABLE bool isAreaCollapsed(const QString& side) const;
    Q_INVOKABLE bool setAreaCollapsed(const QString& side, bool collapsed);
    Q_INVOKABLE bool toggleArea(const QString& side);
    QStringList collapsedAreas() const;

    // Visibility. Hiding takes the panel out of the tree; showing puts it back
    // next to its default neighbours, or at its home side.
    Q_INVOKABLE void hidePanel(const QString& panelId);
    Q_INVOKABLE void showPanel(const QString& panelId);
    Q_INVOKABLE void togglePanel(const QString& panelId);
    Q_INVOKABLE void resetToDefault();

    Q_INVOKABLE QStringList presetNames() const;
    Q_INVOKABLE bool savePreset(const QString& name);
    Q_INVOKABLE bool applyPreset(const QString& name);
    Q_INVOKABLE bool deletePreset(const QString& name);

    Q_INVOKABLE QVariantMap serialise() const;
    Q_INVOKABLE bool restore(const QVariantMap& map);

signals:
    // The shape of the workspace changed: splits, tab groups, order, visibility
    // or a floating window appeared/disappeared.
    void layoutChanged();
    // Only the active tab changed. Kept separate so switching tabs does not
    // rebuild every panel.
    void activeTabChanged(const QString& panelId);
    // A floating window was moved or resized; the layout tree is unchanged.
    void floatingGeometryChanged(int floatingId);
    // Only one split's proportions changed. Kept separate from layoutChanged so
    // dragging a handle never rebuilds the panels.
    void fractionsChanged(int splitId);

private:
    struct Node {
        enum Kind { Split, Tabs, Canvas };
        int id = 0;
        Kind kind = Split;
        Qt::Orientation orientation = Qt::Horizontal;
        bool collapsed = false;
        QStringList panels;
        int active = 0;
        QVector<qreal> fractions;
        std::vector<Node> children;
    };

    struct Floating {
        int id = 0;
        QStringList panels;
        int active = 0;
        QRect geometry{ 120, 120, 320, 260 };
        bool collapsed = false;
        // Where the panel was docked when it was floated out. Docking it back
        // returns it to that slot — the same tab group if the group survived,
        // otherwise the same position in the same split, at the same size —
        // instead of appending a new column to the workspace.
        int originGroupId = 0;
        int originParentId = 0;
        int originIndex = -1;
        int originTabIndex = 0;
        qreal originFraction = 0.0;
    };

    int nextId();
    Node makeTabs(const QString& panelId);
    Node makeCanvas();
    void buildDefault();
    void loadFromSettings();
    void scheduleSave();
    void saveNow();
    void storePreset(const QString& name, const QVariantMap& layout);

    static QVariantMap nodeToVariant(const Node& node);
    bool variantToNode(const QVariantMap& map, Node* node, QSet<QString>* seen,
                       QSet<int>* usedIds);
    bool applyLayout(const QVariantMap& map);

    Node* findNode(int nodeId);
    const Node* findNode(int nodeId) const;
    Node* findSplit(int nodeId);
    Node* panelTabsNode(const QString& panelId);
    Floating* floatingFor(const QString& panelId);
    // Where a hidden panel was last docked, so showing it puts it back where it
    // came from instead of at a generic home side.
    //
    // A panel that shared its tab group is put back into that group at the same
    // position. A panel that was *alone* in its group takes the group with it
    // when it is hidden, so the slot is remembered instead: the neighbour it
    // used to sit next to, which side of it, and the share of the split it had.
    struct Placement {
        int group = -1;
        int index = -1;
        int anchor = -1;              // tab group of the neighbour
        QString anchorSide;           // side of the neighbour the panel sat on
        qreal fraction = 0.0;         // share of the parent split
    };

    // A border region of the layout: the children of one split on one side of
    // the canvas. Areas are found along the chain of splits that lead from the
    // root to the canvas, so a panel docked below the canvas (its own vertical
    // split) and panels docked beside it (the root split) are both areas.
    struct AreaRef {
        QString side;
        int nodeId = -1;
        int splitId = -1;
        int childIndex = -1;
    };

    int childContainingCanvas(const Node& split) const;
    QString areaSideForIndex(const Node& split, int index, int canvasIndex) const;
    void collectAreaRefs(const Node& node, QVector<AreaRef>* out) const;
    QVector<AreaRef> areaRefs() const;
    bool nodeTreeContains(const Node& node, int nodeId) const;
    void rememberPlacement(const QString& panelId);
    bool restoreBesideAnchor(const QString& panelId, const Placement& placement);
    bool findParentSplit(const Node& node, int nodeId, int* splitId, int* childIndex) const;
    int firstTabsNodeOf(const Node& node) const;
    // Dropping a panel into a collapsed area opens that area: the user has to
    // see what they just moved.
    void revealAreaOf(const QString& panelId);

    QStringList collectPanelsOf(const Node& node) const;
    QStringList visiblePanelsList() const;
    QString homeFor(const QString& panelId) const;

    void removePanelFromTree(const QString& panelId);
    bool splitAround(int targetNodeId, const QString& panelId, const QString& side);
    bool dockBesideCanvas(const QString& panelId, const QString& side);
    void detachFromFloating(const QString& panelId);

    Settings* m_settings = nullptr;
    QTimer* m_saveTimer = nullptr;
    Node m_root;
    std::vector<Floating> m_floating;
    QStringList m_hidden;
    QHash<QString, Placement> m_stash;
    QStringList m_collapsedAreas;
    QString m_active;
    int m_nextId = 1;
};

}  // namespace komapaint
