#include "DockModel.h"

#include "../core/Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <functional>

namespace komapaint {

namespace {

constexpr int kLayoutVersion = 1;
constexpr qreal kMinFraction = 0.05;

QString sideName(const QString& side)
{
    const QString s = side.toLower();
    if (s == QLatin1String("left") || s == QLatin1String("right")
        || s == QLatin1String("top") || s == QLatin1String("bottom"))
        return s;
    return QString();
}

void normaliseFractions(QVector<qreal>* fractions)
{
    if (!fractions) return;
    qreal total = 0.0;
    for (qreal f : *fractions) total += std::max<qreal>(f, 0.01);
    if (total <= 0.0) total = 1.0;
    for (qreal& f : *fractions) f = std::max<qreal>(f, 0.01) / total;
}

QString jsonString(const QVariantMap& map)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact));
}

QVariantMap jsonMap(const QString& text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    return doc.isObject() ? doc.object().toVariantMap() : QVariantMap();
}

}  // namespace

// ------------------------------------------------------------------ panels

const QList<DockModel::PanelInfo>& DockModel::panelTable()
{
    static const QList<PanelInfo> table = {
        { QStringLiteral("subtool"),      QStringLiteral("Sub Tool"),      QStringLiteral("left"),   true  },
        { QStringLiteral("toolproperty"), QStringLiteral("Tool Property"), QStringLiteral("right"),  true  },
        { QStringLiteral("brushsize"),    QStringLiteral("Brush Size"),    QStringLiteral("left"),   true  },
        { QStringLiteral("layers"),       QStringLiteral("Layers"),        QStringLiteral("right"),  true  },
        { QStringLiteral("properties"),   QStringLiteral("Properties"),    QStringLiteral("right"),  true  },
        { QStringLiteral("color"),        QStringLiteral("Color"),         QStringLiteral("right"),  true  },
        { QStringLiteral("palette"),      QStringLiteral("Palette"),       QStringLiteral("right"),  true  },
        { QStringLiteral("colormixer"),   QStringLiteral("Color Mixer"),   QStringLiteral("right"),  true  },
        { QStringLiteral("timeline"),     QStringLiteral("Timeline"),      QStringLiteral("bottom"), false },
        { QStringLiteral("history"),      QStringLiteral("History"),       QStringLiteral("right"),  false },
        { QStringLiteral("assets"),       QStringLiteral("Assets"),        QStringLiteral("right"),  true  },
        { QStringLiteral("xsheet"),       QStringLiteral("Exposure Sheet"), QStringLiteral("bottom"), false },
    };
    return table;
}

QVariantList DockModel::panels() const
{
    QVariantList list;
    list.reserve(panelTable().size());
    for (const auto& entry : panelTable()) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), entry.id);
        item.insert(QStringLiteral("title"), entry.title);
        item.insert(QStringLiteral("home"), entry.home);
        item.insert(QStringLiteral("visible"), isVisible(entry.id));
        list.append(item);
    }
    return list;
}

const DockModel::PanelInfo* DockModel::info(const QString& panelId) const
{
    for (const auto& entry : panelTable())
        if (entry.id == panelId) return &entry;
    return nullptr;
}

bool DockModel::isKnownPanel(const QString& panelId) const
{
    return info(panelId) != nullptr;
}

QString DockModel::homeFor(const QString& panelId) const
{
    const PanelInfo* entry = info(panelId);
    return entry ? entry->home : QStringLiteral("right");
}

// ------------------------------------------------------------------ construction

DockModel::DockModel(QObject* parent) : QObject(parent)
{
    buildDefault();
}

DockModel::~DockModel()
{
    if (m_saveTimer) m_saveTimer->stop();
    if (m_settings) saveNow();
}

int DockModel::nextId() { return m_nextId++; }

DockModel::Node DockModel::makeTabs(const QString& panelId)
{
    Node node;
    node.id = nextId();
    node.kind = Node::Tabs;
    node.panels = { panelId };
    node.active = 0;
    return node;
}

DockModel::Node DockModel::makeCanvas()
{
    Node node;
    node.id = nextId();
    node.kind = Node::Canvas;
    return node;
}

void DockModel::buildDefault()
{
    m_nextId = 1;

    auto tabs = [this](const QString& id) { return makeTabs(id); };
    auto column = [this](std::vector<Node> children, QVector<qreal> fractions) {
        Node split;
        split.id = nextId();
        split.kind = Node::Split;
        split.orientation = Qt::Vertical;
        split.children = std::move(children);
        split.fractions = std::move(fractions);
        return split;
    };

    Node left = column({ tabs(QStringLiteral("subtool")),
                         tabs(QStringLiteral("brushsize")) },
                       { 0.72, 0.28 });
    Node centre = makeCanvas();
    Node colorTabs = tabs(QStringLiteral("color"));
    colorTabs.panels.append(QStringLiteral("palette"));
    colorTabs.panels.append(QStringLiteral("colormixer"));
    Node propTabs = tabs(QStringLiteral("properties"));
    propTabs.panels.append(QStringLiteral("assets"));
    Node right = column({ tabs(QStringLiteral("toolproperty")),
                          std::move(colorTabs),
                          tabs(QStringLiteral("layers")),
                          std::move(propTabs) },
                        { 0.34, 0.28, 0.24, 0.14 });

    m_root = Node();
    m_root.id = nextId();
    m_root.kind = Node::Split;
    m_root.orientation = Qt::Horizontal;
    m_root.children = { std::move(left), std::move(centre), std::move(right) };
    m_root.fractions = { 0.175, 0.615, 0.210 };

    m_floating.clear();
    m_hidden.clear();
    m_stash.clear();
    m_collapsedAreas.clear();
    for (const auto& entry : panelTable())
        if (!entry.visibleByDefault) m_hidden.append(entry.id);
    m_active = QStringLiteral("layers");
}

// ------------------------------------------------------------------ serialisation

QVariantMap DockModel::nodeToVariant(const Node& node)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), node.id);
    switch (node.kind) {
    case Node::Tabs: {
        map.insert(QStringLiteral("kind"), QStringLiteral("tabs"));
        map.insert(QStringLiteral("panels"), node.panels);
        map.insert(QStringLiteral("active"), node.active);
        map.insert(QStringLiteral("collapsed"), node.collapsed);
        break;
    }
    case Node::Canvas:
        map.insert(QStringLiteral("kind"), QStringLiteral("canvas"));
        break;
    case Node::Split: {
        map.insert(QStringLiteral("kind"), QStringLiteral("split"));
        map.insert(QStringLiteral("orientation"),
                   node.orientation == Qt::Horizontal ? QStringLiteral("horizontal")
                                                      : QStringLiteral("vertical"));
        QVariantList children;
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            QVariantMap child;
            child.insert(QStringLiteral("fraction"), i < static_cast<std::size_t>(node.fractions.size())
                                                         ? node.fractions.at(static_cast<int>(i))
                                                         : 1.0 / static_cast<qreal>(node.children.size()));
            child.insert(QStringLiteral("node"), nodeToVariant(node.children[i]));
            children.append(child);
        }
        map.insert(QStringLiteral("children"), children);
        break;
    }
    }
    return map;
}

bool DockModel::variantToNode(const QVariantMap& map, Node* node, QSet<QString>* seen,
                              QSet<int>* usedIds)
{
    if (!node) return false;
    const QString kind = map.value(QStringLiteral("kind")).toString();
    // Ids are re-issued when missing or duplicated so splitter bindings stay
    // unambiguous after a restore.
    node->id = map.value(QStringLiteral("id"), 0).toInt();
    if (node->id <= 0 || usedIds->contains(node->id)) node->id = nextId();
    usedIds->insert(node->id);

    if (kind == QLatin1String("canvas")) {
        node->kind = Node::Canvas;
        node->panels.clear();
        node->children.clear();
        node->fractions.clear();
        return true;
    }
    if (kind == QLatin1String("tabs")) {
        node->kind = Node::Tabs;
        node->children.clear();
        node->fractions.clear();
        const QStringList stored = map.value(QStringLiteral("panels")).toStringList();
        node->panels.clear();
        for (const QString& id : stored) {
            if (!isKnownPanel(id)) continue;         // panel no longer exists
            if (seen->contains(id)) continue;        // duplicate: keep the first
            seen->insert(id);
            node->panels.append(id);
        }
        if (node->panels.isEmpty()) return false;     // empty group: prune
        node->active = std::clamp(map.value(QStringLiteral("active"), 0).toInt(), 0,
                                  static_cast<int>(node->panels.size()) - 1);
        node->collapsed = map.value(QStringLiteral("collapsed"), false).toBool();
        return true;
    }
    if (kind != QLatin1String("split")) return false;

    node->kind = Node::Split;
    node->orientation = map.value(QStringLiteral("orientation")).toString() == QLatin1String("vertical")
                            ? Qt::Vertical
                            : Qt::Horizontal;
    node->children.clear();
    node->fractions.clear();
    const QVariantList children = map.value(QStringLiteral("children")).toList();
    for (const QVariant& value : children) {
        const QVariantMap childMap = value.toMap();
        Node child;
        if (!variantToNode(childMap.value(QStringLiteral("node")).toMap(), &child, seen, usedIds)) continue;
        node->children.push_back(std::move(child));
        node->fractions.append(childMap.value(QStringLiteral("fraction"), 0.0).toDouble());
    }
    if (node->children.empty()) return false;
    if (node->children.size() == 1) {
        Node only = std::move(node->children.front());
        *node = std::move(only);
        return true;
    }
    node->fractions.resize(static_cast<int>(node->children.size()));
    normaliseFractions(&node->fractions);
    return true;
}

QVariantMap DockModel::serialise() const
{
    QVariantMap map;
    map.insert(QStringLiteral("version"), kLayoutVersion);
    map.insert(QStringLiteral("root"), nodeToVariant(m_root));
    QVariantList floating;
    for (const auto& window : m_floating) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), window.id);
        entry.insert(QStringLiteral("panels"), window.panels);
        entry.insert(QStringLiteral("active"), window.active);
        entry.insert(QStringLiteral("x"), window.geometry.x());
        entry.insert(QStringLiteral("y"), window.geometry.y());
        entry.insert(QStringLiteral("width"), window.geometry.width());
        entry.insert(QStringLiteral("height"), window.geometry.height());
        floating.append(entry);
    }
    map.insert(QStringLiteral("floating"), floating);
    map.insert(QStringLiteral("hidden"), m_hidden);
    map.insert(QStringLiteral("collapsedAreas"), collapsedAreas());
    // Only the placement of a hidden panel matters, and the order has to be
    // stable so two saves of the same workspace compare equal.
    QVector<QPair<QString, Placement>> stashed;
    for (auto it = m_stash.constBegin(); it != m_stash.constEnd(); ++it)
        if (m_hidden.contains(it.key())) stashed.append({ it.key(), it.value() });
    std::sort(stashed.begin(), stashed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    QVariantList stash;
    for (const auto& entry : stashed) {
        QVariantMap item;
        item.insert(QStringLiteral("panel"), entry.first);
        item.insert(QStringLiteral("group"), entry.second.group);
        item.insert(QStringLiteral("index"), entry.second.index);
        item.insert(QStringLiteral("anchor"), entry.second.anchor);
        item.insert(QStringLiteral("anchorSide"), entry.second.anchorSide);
        item.insert(QStringLiteral("fraction"), entry.second.fraction);
        stash.append(item);
    }
    map.insert(QStringLiteral("stash"), stash);
    map.insert(QStringLiteral("active"), m_active);
    return map;
}

bool DockModel::applyLayout(const QVariantMap& map)
{
    if (map.isEmpty()) return false;
    if (map.value(QStringLiteral("version"), 0).toInt() != kLayoutVersion) return false;

    QSet<QString> seen;
    QSet<int> usedIds;
    Node root;
    if (!variantToNode(map.value(QStringLiteral("root")).toMap(), &root, &seen, &usedIds)) return false;

    std::vector<Floating> floating;
    for (const QVariant& value : map.value(QStringLiteral("floating")).toList()) {
        const QVariantMap entry = value.toMap();
        Floating window;
        window.id = entry.value(QStringLiteral("id"), 0).toInt();
        if (window.id <= 0 || usedIds.contains(window.id)) window.id = nextId();
        usedIds.insert(window.id);
        window.active = entry.value(QStringLiteral("active"), 0).toInt();
        window.geometry = QRect(entry.value(QStringLiteral("x"), 120).toInt(),
                                entry.value(QStringLiteral("y"), 120).toInt(),
                                std::max(160, entry.value(QStringLiteral("width"), 320).toInt()),
                                std::max(120, entry.value(QStringLiteral("height"), 260).toInt()));
        for (const QString& id : entry.value(QStringLiteral("panels")).toStringList()) {
            if (!isKnownPanel(id) || seen.contains(id)) continue;
            seen.insert(id);
            window.panels.append(id);
        }
        if (window.panels.isEmpty()) continue;
        window.active = std::clamp(window.active, 0, static_cast<int>(window.panels.size()) - 1);
        floating.push_back(window);
    }

    QStringList hidden;
    for (const QString& id : map.value(QStringLiteral("hidden")).toStringList()) {
        if (!isKnownPanel(id) || seen.contains(id)) continue;
        hidden.append(id);
    }

    m_root = std::move(root);
    m_floating = std::move(floating);
    m_hidden = hidden;

    m_collapsedAreas.clear();
    for (const QString& side : map.value(QStringLiteral("collapsedAreas")).toStringList()) {
        const QString s = sideName(side);
        if (!s.isEmpty() && !m_collapsedAreas.contains(s)) m_collapsedAreas.append(s);
    }
    m_stash.clear();
    for (const QVariant& value : map.value(QStringLiteral("stash")).toList()) {
        const QVariantMap entry = value.toMap();
        const QString panel = entry.value(QStringLiteral("panel")).toString();
        const int group = entry.value(QStringLiteral("group"), -1).toInt();
        if (!isKnownPanel(panel) || group <= 0) continue;
        Placement placement;
        placement.group = group;
        placement.index = entry.value(QStringLiteral("index"), -1).toInt();
        placement.anchor = entry.value(QStringLiteral("anchor"), -1).toInt();
        placement.anchorSide = sideName(entry.value(QStringLiteral("anchorSide")).toString());
        placement.fraction = entry.value(QStringLiteral("fraction"), 0.0).toDouble();
        m_stash.insert(panel, placement);
    }
    m_nextId = std::max(m_nextId, 1);
    // Node ids have to stay unique after a restore: collect the largest one.
    std::function<void(const Node&)> scan = [&](const Node& node) {
        m_nextId = std::max(m_nextId, node.id + 1);
        for (const auto& child : node.children) scan(child);
    };
    scan(m_root);
    for (const auto& window : m_floating) m_nextId = std::max(m_nextId, window.id + 1);

    // A panel that is known in this build but absent from the stored layout is
    // put back at its home side, so a panel added in a later version can never
    // be unreachable. Panels that are hidden stay hidden.
    QStringList restoreVisible;
    for (const auto& entry : panelTable()) {
        if (seen.contains(entry.id)) continue;
        if (m_hidden.contains(entry.id)) continue;   // deliberately hidden by the user
        if (entry.id == QStringLiteral("palette") || entry.id == QStringLiteral("colormixer") || entry.id == QStringLiteral("assets")) {
            m_hidden.append(entry.id);
            continue; // Existing layouts remain untouched; View can open these.
        }
        if (entry.visibleByDefault) restoreVisible.append(entry.id);
        else m_hidden.append(entry.id);
    }
    for (const QString& id : restoreVisible) {
        m_hidden.removeAll(id);
        showPanel(id);
    }

    const QString storedActive = map.value(QStringLiteral("active")).toString();
    m_active = isVisible(storedActive) ? storedActive : QString();
    if (m_active.isEmpty()) {
        const QStringList visible = visiblePanels();
        m_active = visible.isEmpty() ? QString() : visible.front();
    }
    return true;
}

// ------------------------------------------------------------------ tree helpers

DockModel::Node* DockModel::findNode(int nodeId)
{
    std::function<Node*(Node&)> walk = [&](Node& node) -> Node* {
        if (node.id == nodeId) return &node;
        for (auto& child : node.children)
            if (Node* found = walk(child)) return found;
        return nullptr;
    };
    return walk(m_root);
}

const DockModel::Node* DockModel::findNode(int nodeId) const
{
    std::function<const Node*(const Node&)> walk = [&](const Node& node) -> const Node* {
        if (node.id == nodeId) return &node;
        for (const auto& child : node.children)
            if (const Node* found = walk(child)) return found;
        return nullptr;
    };
    return walk(m_root);
}

DockModel::Node* DockModel::findSplit(int nodeId)
{
    Node* node = findNode(nodeId);
    return (node && node->kind == Node::Split) ? node : nullptr;
}

DockModel::Node* DockModel::panelTabsNode(const QString& panelId)
{
    std::function<Node*(Node&)> walk = [&](Node& node) -> Node* {
        if (node.kind == Node::Tabs && node.panels.contains(panelId)) return &node;
        for (auto& child : node.children)
            if (Node* found = walk(child)) return found;
        return nullptr;
    };
    return walk(m_root);
}

QStringList DockModel::collectPanelsOf(const Node& node) const
{
    QStringList result;
    std::function<void(const Node&)> walk = [&](const Node& n) {
        if (n.kind == Node::Tabs) result.append(n.panels);
        for (const auto& child : n.children) walk(child);
    };
    walk(node);
    return result;
}

void DockModel::removePanelFromTree(const QString& panelId)
{
    std::function<bool(Node&)> remove = [&](Node& node) -> bool {
        if (node.kind == Node::Tabs) {
            node.panels.removeAll(panelId);
            if (!node.panels.isEmpty() && node.active >= node.panels.size())
                node.active = static_cast<int>(node.panels.size()) - 1;
            return !node.panels.isEmpty();
        }
        if (node.kind == Node::Canvas) return true;
        std::vector<Node> kept;
        QVector<qreal> fractions;
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            if (!remove(node.children[i])) continue;
            kept.push_back(std::move(node.children[i]));
            fractions.append(i < static_cast<std::size_t>(node.fractions.size())
                                 ? node.fractions.at(static_cast<int>(i))
                                 : 1.0 / static_cast<qreal>(node.children.size()));
        }
        node.children = std::move(kept);
        node.fractions = fractions;
        if (node.children.empty()) return false;
        if (node.children.size() == 1) {
            Node only = std::move(node.children.front());
            node = std::move(only);
            return true;
        }
        normaliseFractions(&node.fractions);
        return true;
    };
    remove(m_root);
}

// ------------------------------------------------------------------ queries

QVariantMap DockModel::tree() const { return nodeToVariant(m_root); }

QVariantList DockModel::floating() const
{
    QVariantList result;
    for (const auto& window : m_floating) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), window.id);
        entry.insert(QStringLiteral("panels"), window.panels);
        entry.insert(QStringLiteral("active"), window.active);
        entry.insert(QStringLiteral("x"), window.geometry.x());
        entry.insert(QStringLiteral("y"), window.geometry.y());
        entry.insert(QStringLiteral("width"), window.geometry.width());
        entry.insert(QStringLiteral("height"), window.geometry.height());
        result.append(entry);
    }
    return result;
}

QStringList DockModel::hiddenPanels() const { return m_hidden; }

QString DockModel::activePanel() const { return m_active; }

QString DockModel::title(const QString& panelId) const
{
    const PanelInfo* entry = info(panelId);
    return entry ? entry->title : QString();
}

bool DockModel::isVisible(const QString& panelId) const
{
    if (!isKnownPanel(panelId) || m_hidden.contains(panelId)) return false;
    for (const auto& window : m_floating)
        if (window.panels.contains(panelId)) return true;
    QStringList inTree;
    std::function<void(const Node&)> walk = [&](const Node& node) {
        if (node.kind == Node::Tabs && node.panels.contains(panelId)) inTree.append(panelId);
        for (const auto& child : node.children) walk(child);
    };
    walk(m_root);
    return !inTree.isEmpty();
}

bool DockModel::groupHasPanel(int nodeId, const QString& panelId) const
{
    const Node* node = findNode(nodeId);
    return node && node->kind == Node::Tabs && node->panels.contains(panelId);
}

bool DockModel::isCollapsed(const QString& panelId) const
{
    for (const auto& window : m_floating)
        if (window.panels.contains(panelId)) return window.collapsed;
    const Node* node = const_cast<DockModel*>(this)->panelTabsNode(panelId);
    return node ? node->collapsed : false;
}

bool DockModel::isFloating(const QString& panelId) const
{
    return const_cast<DockModel*>(this)->floatingFor(panelId) != nullptr;
}

int DockModel::groupOf(const QString& panelId) const
{
    const Node* node = const_cast<DockModel*>(this)->panelTabsNode(panelId);
    return node ? node->id : -1;
}

int DockModel::floatingIdOf(const QString& panelId) const
{
    const Floating* window = const_cast<DockModel*>(this)->floatingFor(panelId);
    return window ? window->id : -1;
}

QVariantList DockModel::fractionsOf(int splitId) const
{
    QVariantList result;
    const Node* split = findNode(splitId);
    if (!split || split->kind != Node::Split) return result;
    for (qreal fraction : split->fractions) result.append(fraction);
    return result;
}

// ------------------------------------------------------------------ dock areas

int DockModel::childContainingCanvas(const Node& split) const
{
    if (split.kind != Node::Split) return -1;
    for (std::size_t i = 0; i < split.children.size(); ++i) {
        const Node& child = split.children[i];
        if (child.kind == Node::Canvas) return static_cast<int>(i);
        if (child.kind == Node::Split && childContainingCanvas(child) >= 0) return static_cast<int>(i);
    }
    return -1;
}

QString DockModel::areaSideForIndex(const Node& split, int index, int canvasIndex) const
{
    if (index < 0 || canvasIndex < 0 || index == canvasIndex) return QString();
    if (index >= static_cast<int>(split.children.size())) return QString();
    if (split.orientation == Qt::Horizontal)
        return index < canvasIndex ? QStringLiteral("left") : QStringLiteral("right");
    return index < canvasIndex ? QStringLiteral("top") : QStringLiteral("bottom");
}

void DockModel::collectAreaRefs(const Node& node, QVector<AreaRef>* out) const
{
    const int canvas = childContainingCanvas(node);
    if (canvas < 0) return;   // this branch does not hold the canvas: no areas here
    for (int i = 0; i < static_cast<int>(node.children.size()); ++i) {
        const QString side = areaSideForIndex(node, i, canvas);
        if (side.isEmpty()) continue;
        AreaRef ref;
        ref.side = side;
        ref.nodeId = node.children[static_cast<std::size_t>(i)].id;
        ref.splitId = node.id;
        ref.childIndex = i;
        out->append(ref);
    }
    // The canvas may sit in a split of its own; the chain continues there.
    const Node& canvasBranch = node.children[static_cast<std::size_t>(canvas)];
    if (canvasBranch.kind == Node::Split) collectAreaRefs(canvasBranch, out);
}

QVector<DockModel::AreaRef> DockModel::areaRefs() const
{
    QVector<AreaRef> refs;
    collectAreaRefs(m_root, &refs);
    return refs;
}

bool DockModel::nodeTreeContains(const Node& node, int nodeId) const
{
    if (node.id == nodeId) return true;
    for (const auto& child : node.children)
        if (nodeTreeContains(child, nodeId)) return true;
    return false;
}

QString DockModel::areaSideOf(int nodeId) const
{
    for (const AreaRef& ref : areaRefs()) {
        const Node* node = findNode(ref.nodeId);
        if (node && nodeTreeContains(*node, nodeId)) return ref.side;
    }
    return QString();
}

QString DockModel::areaSideOfChild(int nodeId) const
{
    for (const AreaRef& ref : areaRefs())
        if (ref.nodeId == nodeId) return ref.side;
    return QString();
}

QVariantList DockModel::areas() const
{
    // One entry per side, however many columns that side has: the side is what
    // collapses, so two left columns are one band with one strip and one menu
    // entry, not two of each.
    QVariantList list;
    QHash<QString, int> indexOfSide;
    for (const AreaRef& ref : areaRefs()) {
        const Node* node = findNode(ref.nodeId);
        if (!node) continue;
        const QStringList panels = collectPanelsOf(*node);
        const auto known = indexOfSide.constFind(ref.side);
        if (known != indexOfSide.constEnd()) {
            QVariantMap entry = list.at(known.value()).toMap();
            QStringList merged = entry.value(QStringLiteral("panels")).toStringList();
            for (const QString& panel : panels)
                if (!merged.contains(panel)) merged.append(panel);
            entry.insert(QStringLiteral("panels"), merged);
            list[known.value()] = entry;
            continue;
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("side"), ref.side);
        entry.insert(QStringLiteral("nodeId"), ref.nodeId);
        entry.insert(QStringLiteral("splitId"), ref.splitId);
        entry.insert(QStringLiteral("collapsed"), isAreaCollapsed(ref.side));
        entry.insert(QStringLiteral("panels"), panels);
        indexOfSide.insert(ref.side, static_cast<int>(list.size()));
        list.append(entry);
    }
    return list;
}

bool DockModel::hasArea(const QString& side) const
{
    const QString s = QString(side).toLower();
    for (const AreaRef& ref : areaRefs())
        if (ref.side == s) return true;
    return false;
}

bool DockModel::isAreaCollapsed(const QString& side) const
{
    // A side that no longer holds anything is not collapsed, whatever a stored
    // layout said about it.
    return hasArea(side) && m_collapsedAreas.contains(QString(side).toLower());
}

QStringList DockModel::collapsedAreas() const
{
    QStringList result;
    for (const QString& side : m_collapsedAreas)
        if (hasArea(side) && !result.contains(side)) result.append(side);
    return result;
}

bool DockModel::setAreaCollapsed(const QString& side, bool collapsed)
{
    const QString s = QString(side).toLower();
    if (!hasArea(s)) return false;
    const bool now = m_collapsedAreas.contains(s);
    if (now == collapsed) return true;
    if (collapsed) m_collapsedAreas.append(s);
    else m_collapsedAreas.removeAll(s);
    emit layoutChanged();
    scheduleSave();
    return true;
}

bool DockModel::toggleArea(const QString& side)
{
    return setAreaCollapsed(side, !isAreaCollapsed(side));
}

QVariantMap DockModel::nodeById(int nodeId) const
{
    const Node* node = findNode(nodeId);
    return node ? nodeToVariant(*node) : QVariantMap();
}

DockModel::Floating* DockModel::floatingFor(const QString& panelId)
{
    for (auto& window : m_floating)
        if (window.panels.contains(panelId)) return &window;
    return nullptr;
}

// ------------------------------------------------------------------ structure changes

void DockModel::detachFromFloating(const QString& panelId)
{
    for (auto it = m_floating.begin(); it != m_floating.end(); ++it) {
        if (!it->panels.contains(panelId)) continue;
        it->panels.removeAll(panelId);
        if (it->panels.isEmpty()) {
            m_floating.erase(it);
        } else if (it->active >= it->panels.size()) {
            it->active = static_cast<int>(it->panels.size()) - 1;
        }
        return;
    }
}

bool DockModel::moveToGroup(const QString& panelId, int groupId, int index)
{
    if (!isKnownPanel(panelId)) return false;
    Node* target = findNode(groupId);
    if (!target || target->kind != Node::Tabs) return false;
    const int current = target->panels.indexOf(panelId);
    if (current >= 0) {
        const int wanted = (index < 0 || index > target->panels.size())
                               ? static_cast<int>(target->panels.size()) - 1
                               : index;
        if (wanted == current) return false;   // already exactly there
    }

    detachFromFloating(panelId);
    removePanelFromTree(panelId);
    m_hidden.removeAll(panelId);

    target = findNode(groupId);
    if (!target || target->kind != Node::Tabs) {
        // The group was merged away while removing the panel; fall back to the
        // panel's home side so it cannot be lost.
        return dockToWorkspace(panelId, homeFor(panelId));
    }
    const int at = (index < 0 || index > target->panels.size()) ? target->panels.size() : index;
    target->panels.insert(at, panelId);
    target->active = at;
    m_active = panelId;
    revealAreaOf(panelId);
    emit layoutChanged();
    scheduleSave();
    return true;
}

bool DockModel::moveToTabs(const QString& panelId, const QString& targetPanelId, int index)
{
    if (panelId == targetPanelId) return false;
    const int groupId = groupOf(targetPanelId);
    if (groupId < 0) return false;
    return moveToGroup(panelId, groupId, index);
}

// Replaces the target tab group with a new split holding that group and the
// panel being docked. The group keeps its node id (and so its tab state).
bool DockModel::splitAround(int targetNodeId, const QString& panelId, const QString& side)
{
    const bool horizontal = (side == QLatin1String("left") || side == QLatin1String("right"));
    const bool before = (side == QLatin1String("left") || side == QLatin1String("top"));
    Node newTabs = makeTabs(panelId);

    if (m_root.kind != Node::Split && m_root.id == targetNodeId) {
        Node split;
        split.id = nextId();
        split.kind = Node::Split;
        split.orientation = horizontal ? Qt::Horizontal : Qt::Vertical;
        split.fractions = { 0.5, 0.5 };
        Node old = std::move(m_root);
        if (before) split.children = { std::move(newTabs), std::move(old) };
        else split.children = { std::move(old), std::move(newTabs) };
        m_root = std::move(split);
        return true;
    }

    std::function<bool(Node&)> walk = [&](Node& node) -> bool {
        if (node.kind != Node::Split) return false;
        for (auto& child : node.children) {
            if (child.kind != Node::Split && child.id == targetNodeId) {
                Node split;
                split.id = nextId();
                split.kind = Node::Split;
                split.orientation = horizontal ? Qt::Horizontal : Qt::Vertical;
                split.fractions = { 0.5, 0.5 };
                Node old = std::move(child);
                if (before) split.children = { std::move(newTabs), std::move(old) };
                else split.children = { std::move(old), std::move(newTabs) };
                child = std::move(split);
                return true;
            }
            if (walk(child)) return true;
        }
        return false;
    };
    return walk(m_root);
}

bool DockModel::dockToNodeSide(const QString& panelId, int nodeId, const QString& side)
{
    const QString s = sideName(side);
    if (!isKnownPanel(panelId) || s.isEmpty() || nodeId <= 0) return false;
    const Node* target = findNode(nodeId);
    if (!target || target->kind == Node::Split) return false;
    if (target->kind == Node::Tabs && target->panels.size() == 1
        && target->panels.front() == panelId) return false;

    detachFromFloating(panelId);
    removePanelFromTree(panelId);
    m_hidden.removeAll(panelId);

    if (!splitAround(nodeId, panelId, s)) return dockToWorkspace(panelId, s);
    m_active = panelId;
    revealAreaOf(panelId);
    emit layoutChanged();
    scheduleSave();
    return true;
}

bool DockModel::dockToGroupSide(const QString& panelId, int groupId, const QString& side)
{
    return dockToNodeSide(panelId, groupId, side);
}

bool DockModel::dockToSide(const QString& panelId, const QString& targetPanelId, const QString& side)
{
    if (panelId == targetPanelId) return false;
    const int groupId = groupOf(targetPanelId);
    if (groupId < 0) return false;
    return dockToGroupSide(panelId, groupId, side);
}

// Inserts a new tab group above or below the canvas, which is where the
// Timeline / Exposure Sheet belong. The canvas keeps its own split, so the
// rest of the workspace is untouched.
bool DockModel::dockBesideCanvas(const QString& panelId, const QString& side)
{
    // A band that already exists on that side takes the panel as a tab. One
    // band per side is what the collapsed strip and its chevron assume, and it
    // is how the reference application behaves.
    for (const AreaRef& ref : areaRefs()) {
        if (ref.side != side) continue;
        const Node* area = findNode(ref.nodeId);
        const int groupId = area ? firstTabsNodeOf(*area) : -1;
        if (groupId > 0 && moveToGroup(panelId, groupId)) return true;
    }

    const bool before = (side == QLatin1String("top"));
    Node newTabs = makeTabs(panelId);
    bool placed = false;

    std::function<void(Node&)> walk = [&](Node& node) {
        if (placed || node.kind != Node::Split) return;
        if (node.orientation == Qt::Vertical) {
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (node.children[i].kind != Node::Canvas) continue;
                const int at = static_cast<int>(i) + (before ? 0 : 1);
                const qreal share = 0.25;
                for (qreal& fraction : node.fractions) fraction *= (1.0 - share);
                node.children.insert(node.children.begin() + at, std::move(newTabs));
                node.fractions.insert(at, share);
                normaliseFractions(&node.fractions);
                placed = true;
                return;
            }
        }
        for (auto& child : node.children) walk(child);
    };
    if (m_root.kind == Node::Split) walk(m_root);
    if (placed) return true;

    // No column around the canvas: wrap the canvas itself.
    std::function<bool(Node&)> wrap = [&](Node& node) -> bool {
        for (auto& child : node.children) {
            if (child.kind == Node::Canvas) {
                Node split;
                split.id = nextId();
                split.kind = Node::Split;
                split.orientation = Qt::Vertical;
                split.fractions = { 0.75, 0.25 };
                Node canvas = std::move(child);
                if (before) split.children = { std::move(newTabs), std::move(canvas) };
                else split.children = { std::move(canvas), std::move(newTabs) };
                child = std::move(split);
                return true;
            }
            if (wrap(child)) return true;
        }
        return false;
    };
    if (m_root.kind == Node::Canvas) {
        Node split;
        split.id = nextId();
        split.kind = Node::Split;
        split.orientation = Qt::Vertical;
        split.fractions = { 0.75, 0.25 };
        Node canvas = std::move(m_root);
        if (before) split.children = { std::move(newTabs), std::move(canvas) };
        else split.children = { std::move(canvas), std::move(newTabs) };
        m_root = std::move(split);
        return true;
    }
    return wrap(m_root);
}

bool DockModel::dockToWorkspace(const QString& panelId, const QString& side)
{
    const QString s = sideName(side);
    if (!isKnownPanel(panelId) || s.isEmpty()) return false;
    detachFromFloating(panelId);
    removePanelFromTree(panelId);
    m_hidden.removeAll(panelId);

    if (s == QLatin1String("top") || s == QLatin1String("bottom")) {
        const bool ok = dockBesideCanvas(panelId, s);
        // Dropping into a collapsed band has to open it, or the panel would be
        // docked into a strip nobody can see.
        if (ok) revealAreaOf(panelId);
        m_active = panelId;
        emit layoutChanged();
        scheduleSave();
        return ok;
    }

    const bool horizontal = (s == QLatin1String("left") || s == QLatin1String("right"));
    const bool before = (s == QLatin1String("left") || s == QLatin1String("top"));
    Node newTabs = makeTabs(panelId);

    if (m_root.kind != Node::Split || (m_root.orientation == Qt::Horizontal) != horizontal) {
        Node split;
        split.id = nextId();
        split.kind = Node::Split;
        split.orientation = horizontal ? Qt::Horizontal : Qt::Vertical;
        split.fractions = { 0.22, 0.78 };
        Node old = std::move(m_root);
        if (before) split.children = { std::move(newTabs), std::move(old) };
        else split.children = { std::move(old), std::move(newTabs) };
        m_root = std::move(split);
    } else {
        const qreal share = 0.22;
        for (qreal& fraction : m_root.fractions) fraction *= (1.0 - share);
        if (before) {
            m_root.children.insert(m_root.children.begin(), std::move(newTabs));
            m_root.fractions.prepend(share);
        } else {
            m_root.children.push_back(std::move(newTabs));
            m_root.fractions.append(share);
        }
        normaliseFractions(&m_root.fractions);
    }
    m_active = panelId;
    revealAreaOf(panelId);
    emit layoutChanged();
    scheduleSave();
    return true;
}

bool DockModel::floatPanel(const QString& panelId, int x, int y, int width, int height)
{
    if (!isKnownPanel(panelId)) return false;
    if (floatingFor(panelId)) return false;

    Floating window;
    // Remember the slot before the panel is taken out of the tree; the group
    // may merge away, in which case its position in the parent split is what
    // has to be remembered.
    const int groupId = groupOf(panelId);
    if (const Node* group = groupId > 0 ? findNode(groupId) : nullptr) {
        window.originGroupId = groupId;
        window.originTabIndex = static_cast<int>(std::max<qsizetype>(0, group->panels.indexOf(panelId)));
        std::function<bool(Node&)> locate = [&](Node& node) -> bool {
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                if (node.children[i].id == groupId) {
                    window.originParentId = node.id;
                    window.originIndex = int(i);
                    window.originFraction = i < std::size_t(node.fractions.size())
                                            ? node.fractions.at(int(i)) : 0.0;
                    return true;
                }
                if (locate(node.children[i])) return true;
            }
            return false;
        };
        locate(m_root);
    }

    removePanelFromTree(panelId);
    m_hidden.removeAll(panelId);

    window.id = nextId();
    window.panels = { panelId };
    window.active = 0;
    window.geometry = QRect(x, y, std::max(200, width), std::max(160, height));
    m_floating.push_back(window);
    m_active = panelId;
    emit layoutChanged();
    scheduleSave();
    return true;
}

bool DockModel::dockBack(int floatingId)
{
    Floating* window = nullptr;
    for (auto& candidate : m_floating)
        if (candidate.id == floatingId) window = &candidate;
    if (!window || window->panels.isEmpty()) return false;
    const QStringList panels = window->panels;
    const Floating origin = *window;
    m_floating.erase(std::remove_if(m_floating.begin(), m_floating.end(),
                                    [&](const Floating& f) { return f.id == floatingId; }),
                     m_floating.end());
    const QString first = panels.front();
    auto restoreRest = [&]() {
        for (int i = 1; i < panels.size(); ++i) moveToGroup(panels.at(i), groupOf(first));
        return true;
    };

    // The group the panel came from is still there (it had other tabs): slide
    // the panel back in as a tab, where it was in the tab order.
    if (Node* group = origin.originGroupId > 0 ? findNode(origin.originGroupId) : nullptr) {
        if (group->kind == Node::Tabs && moveToGroup(first, origin.originGroupId,
                                                     origin.originTabIndex))
            return restoreRest();
    }

    // The group merged away with the panel; the split it lived in may still be
    // there, so re-create the group at the same index and size.
    if (Node* parent = origin.originParentId > 0 ? findNode(origin.originParentId) : nullptr) {
        if (parent->kind == Node::Split) {
            const int count = static_cast<int>(parent->children.size());
            const int at = qBound(0, origin.originIndex, count);
            const qreal wanted = origin.originFraction > 0.02 ? origin.originFraction
                                                              : 1.0 / (count + 1);
            const qreal share = qBound<qreal>(0.05, wanted, 0.95);
            qreal others = 0.0;
            for (qreal fraction : parent->fractions) others += std::max<qreal>(fraction, 0.01);
            if (others <= 0.0) others = 1.0;
            QVector<qreal> fractions;
            fractions.reserve(count + 1);
            for (int i = 0; i < count; ++i)
                fractions.append(std::max<qreal>(0.01, parent->fractions.at(i) / others * (1.0 - share)));
            fractions.insert(at, share);
            parent->children.insert(parent->children.begin() + at, makeTabs(first));
            parent->fractions = fractions;
            m_hidden.removeAll(first);
            m_active = first;
            revealAreaOf(first);
            emit layoutChanged();
            scheduleSave();
            return restoreRest();
        }
    }

    // Nothing of the old slot survives: fall back to the panel's home side.
    bool ok = dockToWorkspace(first, homeFor(first));
    for (int i = 1; i < panels.size(); ++i)
        ok = moveToGroup(panels.at(i), groupOf(first)) || ok;
    return ok;
}

void DockModel::setFloatingGeometry(int floatingId, int x, int y, int width, int height)
{
    for (auto& window : m_floating) {
        if (window.id != floatingId) continue;
        const QRect next(x, y, std::max(200, width), std::max(160, height));
        if (window.geometry == next) return;
        window.geometry = next;
        emit floatingGeometryChanged(floatingId);
        scheduleSave();
        return;
    }
}

void DockModel::setActive(const QString& panelId)
{
    if (m_active == panelId) return;
    for (auto& window : m_floating) {
        const int index = window.panels.indexOf(panelId);
        if (index < 0) continue;
        window.active = index;
        m_active = panelId;
        emit activeTabChanged(panelId);
        scheduleSave();
        return;
    }
    if (Node* node = panelTabsNode(panelId)) {
        node->active = node->panels.indexOf(panelId);
        m_active = panelId;
        emit activeTabChanged(panelId);
        scheduleSave();
    }
}

bool DockModel::setCollapsed(const QString& panelId, bool collapsed)
{
    for (auto& window : m_floating) {
        if (!window.panels.contains(panelId)) continue;
        if (window.collapsed == collapsed) return true;
        window.collapsed = collapsed;
        emit layoutChanged();
        scheduleSave();
        return true;
    }
    Node* node = panelTabsNode(panelId);
    if (!node) return false;
    if (node->collapsed == collapsed) return true;
    node->collapsed = collapsed;
    emit layoutChanged();
    scheduleSave();
    return true;
}

void DockModel::setFraction(int splitId, int index, qreal fraction, int extent, int minPixels)
{
    Node* split = findSplit(splitId);
    if (!split) return;
    const int count = static_cast<int>(split->children.size());
    if (index < 0 || index + 1 >= count) return;
    if (split->fractions.size() != count) return;

    const qreal minFraction = extent > 0
                                  ? std::clamp(static_cast<qreal>(minPixels) / static_cast<qreal>(extent),
                                               kMinFraction, 0.45)
                                  : kMinFraction;
    const qreal pair = split->fractions.at(index) + split->fractions.at(index + 1);
    const qreal low = std::min(minFraction, pair / 2.0);
    const qreal high = std::max(pair - minFraction, pair / 2.0);
    const qreal value = std::clamp(fraction, low, high);
    if (std::abs(split->fractions.at(index) - value) < 0.0005) return;
    split->fractions[index] = value;
    split->fractions[index + 1] = pair - value;
    emit fractionsChanged(splitId);
    scheduleSave();
}

// ------------------------------------------------------------------ visibility

bool DockModel::findParentSplit(const Node& node, int nodeId, int* splitId, int* childIndex) const
{
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        const Node& child = node.children[i];
        if (child.id == nodeId) {
            if (node.kind != Node::Split) return false;
            *splitId = node.id;
            *childIndex = static_cast<int>(i);
            return true;
        }
        if (findParentSplit(child, nodeId, splitId, childIndex)) return true;
    }
    return false;
}

int DockModel::firstTabsNodeOf(const Node& node) const
{
    if (node.kind == Node::Tabs && !node.panels.isEmpty()) return node.id;
    for (const auto& child : node.children)
        if (const int found = firstTabsNodeOf(child); found > 0) return found;
    return -1;
}

void DockModel::rememberPlacement(const QString& panelId)
{
    const int groupId = groupOf(panelId);
    if (groupId <= 0) return;
    const Node* group = findNode(groupId);
    if (!group || group->kind != Node::Tabs) return;

    Placement placement;
    placement.group = groupId;
    placement.index = static_cast<int>(group->panels.indexOf(panelId));
    if (group->panels.size() > 1) {
        // The group lives on without the panel: it simply goes back into it.
        m_stash.insert(panelId, placement);
        return;
    }

    // This is the panel's own group, so hiding it takes the slot with it. Note
    // the neighbour to come back next to, and how much room the slot had.
    int splitId = -1;
    int childIndex = -1;
    if (!findParentSplit(m_root, groupId, &splitId, &childIndex) || splitId <= 0) {
        m_stash.insert(panelId, placement);
        return;
    }
    const Node* split = findNode(splitId);
    if (!split || split->children.size() < 2) {
        m_stash.insert(panelId, placement);
        return;
    }
    const int count = static_cast<int>(split->children.size());
    // Prefer the neighbour after the slot (the canvas, usually), then the one
    // before it; either way the side is what matters.
    for (const int candidate : { childIndex + 1, childIndex - 1 }) {
        if (candidate < 0 || candidate >= count) continue;
        const int anchor = firstTabsNodeOf(split->children[static_cast<std::size_t>(candidate)]);
        if (anchor <= 0) continue;
        placement.anchor = anchor;
        const bool before = childIndex < candidate;
        if (split->orientation == Qt::Horizontal) placement.anchorSide = before ? QStringLiteral("left") : QStringLiteral("right");
        else placement.anchorSide = before ? QStringLiteral("top") : QStringLiteral("bottom");
        break;
    }
    if (childIndex < split->fractions.size()) placement.fraction = split->fractions.at(childIndex);
    m_stash.insert(panelId, placement);
}

bool DockModel::restoreBesideAnchor(const QString& panelId, const Placement& placement)
{
    if (placement.anchor <= 0 || placement.anchorSide.isEmpty()) return false;
    if (!findNode(placement.anchor)) return false;   // the neighbour is gone as well
    if (!dockToNodeSide(panelId, placement.anchor, placement.anchorSide)) return false;

    const qreal fraction = placement.fraction;
    if (fraction <= 0.02 || fraction >= 0.98) return true;
    const int groupId = groupOf(panelId);
    if (groupId <= 0) return true;

    // The split that was just made is the neighbour's parent; give the slot back
    // the share of the space it used to have.
    int splitId = -1;
    int anchorIndex = -1;
    if (!findParentSplit(m_root, placement.anchor, &splitId, &anchorIndex) || splitId <= 0) return true;
    Node* split = findNode(splitId);
    if (!split || split->children.size() != 2) return true;
    int ours = -1;
    for (std::size_t i2 = 0; i2 < split->children.size(); ++i2)
        if (nodeTreeContains(split->children[i2], groupId)) ours = static_cast<int>(i2);
    if (ours < 0) return true;
    split->fractions.resize(2);
    split->fractions[ours] = fraction;
    split->fractions[1 - ours] = 1.0 - fraction;
    emit layoutChanged();
    return true;
}

void DockModel::revealAreaOf(const QString& panelId)
{
    const int groupId = groupOf(panelId);
    if (groupId <= 0) return;
    const QString side = areaSideOf(groupId);
    if (!side.isEmpty()) m_collapsedAreas.removeAll(side);
}

void DockModel::hidePanel(const QString& panelId)
{
    if (!isKnownPanel(panelId)) return;
    if (isVisible(panelId)) rememberPlacement(panelId);
    bool changed = false;
    if (Floating* window = floatingFor(panelId)) {
        window->panels.removeAll(panelId);
        const int windowId = window->id;
        if (window->panels.isEmpty())
            m_floating.erase(std::remove_if(m_floating.begin(), m_floating.end(),
                                            [&](const Floating& f) { return f.id == windowId; }),
                             m_floating.end());
        changed = true;
    } else if (isVisible(panelId)) {
        removePanelFromTree(panelId);
        changed = true;
    }
    if (!m_hidden.contains(panelId)) {
        m_hidden.append(panelId);
        changed = true;
    }
    if (m_active == panelId) {
        const QStringList visible = visiblePanelsList();
        m_active = visible.isEmpty() ? QString() : visible.front();
        emit activeTabChanged(m_active);
        changed = true;
    }
    if (changed) {
        emit layoutChanged();
        scheduleSave();
    }
}

void DockModel::showPanel(const QString& panelId)
{
    if (!isKnownPanel(panelId)) return;
    if (isVisible(panelId)) return;

    m_hidden.removeAll(panelId);

    // Back where it was: same tab group, same position in the tab strip. The
    // group only has to still exist — if the user closed the whole group while
    // the panel was hidden the fallback below finds a new home.
    const Placement placement = m_stash.value(panelId);
    if (placement.group > 0) {
        if (Node* group = findNode(placement.group); group && group->kind == Node::Tabs) {
            const int count = static_cast<int>(group->panels.size());
            const int index = std::clamp(placement.index < 0 ? count : placement.index, 0, count);
            group->panels.insert(index, panelId);
            group->active = index;
            m_active = panelId;
            // A panel that comes back into a collapsed area reopens the area:
            // the user asked to see it.
            const QString side = areaSideOf(group->id);
            if (!side.isEmpty()) m_collapsedAreas.removeAll(side);
            emit layoutChanged();
            emit activeTabChanged(m_active);
            scheduleSave();
            return;
        }
    }

    // If both colour-tool tabs were closed, their old group may have dissolved.
    // Rejoin the remaining colour tool rather than creating a second group.
    if (panelId == QStringLiteral("palette") || panelId == QStringLiteral("colormixer")) {
        const QString other = panelId == QStringLiteral("palette") ? QStringLiteral("colormixer") : QStringLiteral("palette");
        const int otherGroup = groupOf(other);
        if (otherGroup > 0 && moveToGroup(panelId, otherGroup)) return;
    }

    // Its tab group dissolved while it was hidden: rebuild the slot next to the
    // neighbour it used to sit beside.
    if (restoreBesideAnchor(panelId, placement)) {
        emit activeTabChanged(m_active);
        scheduleSave();
        return;
    }

    // Newly introduced colour tools belong together below the colour wheel,
    // without moving an existing saved layout until the user opens them.
    if (panelId == QStringLiteral("palette") || panelId == QStringLiteral("colormixer")) {
        const QString other = panelId == QStringLiteral("palette") ? QStringLiteral("colormixer") : QStringLiteral("palette");
        const int otherGroup = groupOf(other);
        if (otherGroup > 0 && moveToGroup(panelId, otherGroup)) return;
        const int colorGroup = groupOf(QStringLiteral("color"));
        if (colorGroup > 0 && dockToNodeSide(panelId, colorGroup, QStringLiteral("bottom"))) return;
    }
    if (panelId == QStringLiteral("assets")) {
        int anchor = groupOf(QStringLiteral("palette"));
        if (anchor <= 0) anchor = groupOf(QStringLiteral("colormixer"));
        if (anchor <= 0) anchor = groupOf(QStringLiteral("color"));
        if (anchor > 0 && dockToNodeSide(panelId, anchor, QStringLiteral("bottom"))) return;
    }

    // Prefer the last tab group of the panel's home side so a reopened panel
    // lands next to its default neighbours instead of opening a new column.
    const QString home = homeFor(panelId);
    int groupId = -1;
    std::function<void(const Node&)> walk = [&](const Node& node) {
        if (node.kind == Node::Tabs && !node.panels.isEmpty()) {
            const PanelInfo* first = info(node.panels.front());
            if (first && first->home == home) groupId = node.id;
        }
        for (const auto& child : node.children) walk(child);
    };
    walk(m_root);

    if (groupId >= 0) {
        if (Node* group = findNode(groupId)) {
            group->panels.append(panelId);
            group->active = group->panels.size() - 1;
            m_active = panelId;
            emit layoutChanged();
            scheduleSave();
            return;
        }
    }
    dockToWorkspace(panelId, home);
}

void DockModel::togglePanel(const QString& panelId)
{
    if (isVisible(panelId)) hidePanel(panelId);
    else showPanel(panelId);
}

void DockModel::resetToDefault()
{
    buildDefault();
    emit layoutChanged();
    scheduleSave();
}

QStringList DockModel::visiblePanelsList() const
{
    QStringList result;
    const QStringList inTree = collectPanelsOf(m_root);
    for (const QString& id : inTree)
        if (!m_hidden.contains(id)) result.append(id);
    for (const auto& window : m_floating)
        for (const QString& id : window.panels)
            if (!m_hidden.contains(id) && !result.contains(id)) result.append(id);
    return result;
}

QStringList DockModel::visiblePanels() const
{
    QStringList result;
    for (const auto& entry : panelTable())
        if (isVisible(entry.id)) result.append(entry.id);
    return result;
}

// ------------------------------------------------------------------ presets & persistence

QStringList DockModel::presetNames() const
{
    if (!m_settings) return {};
    const QVariantMap presets = jsonMap(m_settings->value(QStringLiteral("workspace/presets")).toString());
    QStringList names = presets.keys();
    names.sort();
    return names;
}

void DockModel::storePreset(const QString& name, const QVariantMap& layout)
{
    if (!m_settings) return;
    QVariantMap presets = jsonMap(m_settings->value(QStringLiteral("workspace/presets")).toString());
    presets.insert(name, layout);
    m_settings->setValue(QStringLiteral("workspace/presets"), jsonString(presets));
    m_settings->sync();
}

bool DockModel::savePreset(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || !m_settings) return false;
    storePreset(trimmed, serialise());
    emit layoutChanged();
    return true;
}

bool DockModel::applyPreset(const QString& name)
{
    if (!m_settings) return false;
    const QVariantMap presets = jsonMap(m_settings->value(QStringLiteral("workspace/presets")).toString());
    if (!presets.contains(name)) return false;
    if (!restore(presets.value(name).toMap())) return false;
    emit layoutChanged();
    scheduleSave();
    return true;
}

bool DockModel::deletePreset(const QString& name)
{
    if (!m_settings) return false;
    QVariantMap presets = jsonMap(m_settings->value(QStringLiteral("workspace/presets")).toString());
    if (!presets.remove(name)) return false;
    m_settings->setValue(QStringLiteral("workspace/presets"), jsonString(presets));
    m_settings->sync();
    emit layoutChanged();
    return true;
}

bool DockModel::restore(const QVariantMap& map)
{
    if (!applyLayout(map)) return false;
    emit layoutChanged();
    scheduleSave();
    return true;
}

void DockModel::setSettings(Settings* settings)
{
    m_settings = settings;
    if (!m_saveTimer) {
        m_saveTimer = new QTimer(this);
        m_saveTimer->setSingleShot(true);
        m_saveTimer->setInterval(350);
        connect(m_saveTimer, &QTimer::timeout, this, &DockModel::saveNow);
    }
    loadFromSettings();
    emit layoutChanged();
}

void DockModel::loadFromSettings()
{
    if (!m_settings) return;
    const QVariantMap stored = jsonMap(m_settings->value(QStringLiteral("workspace/layout")).toString());
    if (stored.isEmpty() || !applyLayout(stored)) {
        buildDefault();
        scheduleSave();
    }
}

void DockModel::scheduleSave()
{
    if (!m_settings) return;
    if (!m_saveTimer) {
        m_saveTimer = new QTimer(this);
        m_saveTimer->setSingleShot(true);
        m_saveTimer->setInterval(350);
        connect(m_saveTimer, &QTimer::timeout, this, &DockModel::saveNow);
    }
    m_saveTimer->start();
}

void DockModel::saveNow()
{
    if (!m_settings) return;
    m_settings->setValue(QStringLiteral("workspace/layout"), jsonString(serialise()));
    m_settings->sync();
}

}  // namespace komapaint
