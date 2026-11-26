#include "SubToolLibrary.h"
#include "BrushSettings.h"
#include "ToolSettings.h"
#include "drawing/BrushDocumentAdapter.h"
#include "drawing/DigitalBrushCapabilities.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QResource>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

static void initializeBrushPresetResources()
{
    Q_INIT_RESOURCE(brush_presets);
}

namespace komapaint {
namespace {

QString makeId(const QString& prefix)
{
    return prefix + QStringLiteral("-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
}

bool isStrokeBrushTool(const QString& t)
{
    return t == QStringLiteral("pen") || t == QStringLiteral("pencil") ||
           t == QStringLiteral("brush") || t == QStringLiteral("airbrush") ||
           t == QStringLiteral("eraser") || t == QStringLiteral("boundary_eraser") ||
           t == QStringLiteral("smudge");
}

bool isSelectionTool(const QString& t)
{
    return t == QStringLiteral("selection") || t == QStringLiteral("lasso") ||
           t == QStringLiteral("wand");
}

bool isShapeTool(const QString& t)
{
    return t == QStringLiteral("line") || t == QStringLiteral("curveline") ||
           t == QStringLiteral("rectangle") || t == QStringLiteral("circle");
}

QString parameterUnitName(brush::ParameterUnit unit)
{
    switch (unit) {
    case brush::ParameterUnit::Percent: return QStringLiteral("%");
    case brush::ParameterUnit::Pixels: return QStringLiteral("px");
    case brush::ParameterUnit::Degrees: return QStringLiteral("°");
    case brush::ParameterUnit::Ratio: return QStringLiteral("%");
    case brush::ParameterUnit::Unit: break;
    }
    return {};
}

// The compact native Sub-Tool surface. It follows the brush's creator
// exposure, then adds the controls that define a chisel-like tip when the
// document actually uses them. Anything the Digital renderer does not consume
// is omitted rather than presented as an editable lie.
QVariantList nativeControlRows(const brush::BrushDocument& doc,
                               const QMap<QString, double>& overrides)
{
    QVariantList rows;
    QStringList included;
    auto append = [&](const QString& key, const QString& label, double min, double max,
                      const QString& unit, const QString& control, double value) {
        if (included.contains(key)) return;
        included.append(key);
        rows.append(QVariantMap{
            {QStringLiteral("key"), key},
            {QStringLiteral("label"), label},
            {QStringLiteral("minimum"), min},
            {QStringLiteral("maximum"), max},
            {QStringLiteral("unit"), unit},
            {QStringLiteral("control"), control},
            {QStringLiteral("value"), value},
        });
    };
    for (const auto* property : doc.visibleExposed()) {
        const auto* info = brush::findParameter(property->parameter);
        if (!info || info->scope != brush::ParameterScope::Brush ||
            !drawing::digitalSupportsParameter(info->name)) continue;
        const QString key = QString::fromStdString(property->parameter);
        const QString label = property->label.empty() ? QString::fromUtf8(info->label)
                                                       : QString::fromStdString(property->label);
        const double minimum = property->hasRangeOverride ? property->minimumOverride : info->minimum;
        const double maximum = property->hasRangeOverride ? property->maximumOverride : info->maximum;
        QString control = QString::fromUtf8(brush::toString(property->control));
        if (control == QStringLiteral("choice") || control == QStringLiteral("curve")) control = QStringLiteral("slider");
        append(key, label, minimum, maximum, parameterUnitName(info->unit), control,
               overrides.value(key, doc.parameters.value(info->name, info->defaultValue)));
    }
    const double aspect = doc.parameters.value("aspect", 100.0);
    const double tipAngle = doc.parameters.value("tip-angle", 0.0);
    const bool chisel = aspect != 100.0 || tipAngle != 0.0;
    auto addDefault = [&](std::string_view key) {
        const auto* info = brush::findParameter(key);
        const QString name = QString::fromStdString(std::string(key));
        if (!info || included.contains(name) || !drawing::digitalSupportsParameter(key)) return;
        const QString control = (info->minimum == 0.0 && info->maximum == 1.0)
            ? QStringLiteral("toggle") : QStringLiteral("slider");
        append(name, QString::fromUtf8(info->label), info->minimum, info->maximum,
               parameterUnitName(info->unit), control,
               overrides.value(name, doc.parameters.value(key, info->defaultValue)));
    };
    if (rows.isEmpty()) {
        for (const std::string_view key : drawing::recommendedDigitalControls())
            addDefault(key);
    }
    if (chisel) {
        addDefault("aspect");
        addDefault("tip-angle");
        addDefault("follow-direction");
    }
    return rows;
}

} // namespace

bool SubToolLibrary::isStrokePaintToolName(const QString& t)
{
    return t == QStringLiteral("pen") || t == QStringLiteral("pencil") ||
           t == QStringLiteral("brush") || t == QStringLiteral("airbrush") ||
           t == QStringLiteral("eraser") || t == QStringLiteral("smudge");
}

bool SubToolLibrary::isKnownViewOnlyTool(const QString& t)
{
    return t.isEmpty() || t == QStringLiteral("zoom") ||
           t == QStringLiteral("hand") || t == QStringLiteral("eyedropper");
}

SubToolLibrary::SubToolLibrary(BrushSettings* brush, QObject* parent)
    : QObject(parent), m_brush(brush)
{
    initializeBrushPresetResources();
    QFile file(QStringLiteral(":/brushes/presets.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        m_loadError = QStringLiteral("The bundled brush preset catalog could not be opened.");
    } else {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        const QJsonObject root = document.object();
        const QJsonArray groups = root.value(QStringLiteral("groups")).toArray();
        if (parseError.error != QJsonParseError::NoError || !document.isObject()
            || root.value(QStringLiteral("version")).toInt(-1) != 1 || groups.isEmpty()) {
            m_loadError = QStringLiteral("The bundled brush preset catalog is invalid.");
        } else {
            QString colErr;
            if (!BrushLibraryStore::loadBundledCollections(root, &m_bundledCollections, &colErr)) {
                m_loadError = colErr;
            } else {
                m_collections = m_bundledCollections;
            }
        }
    }
    if (!m_loadError.isEmpty()) qWarning("[KomaPaint] %s", qUtf8Printable(m_loadError));

    auto onSettingsMutated = [this] {
        if (m_applying || (m_brush && m_brush->hasNativeBrush())) return;
        if (!hasCollections()) return;
        capture();
        persist();
        emit subToolsChanged();
        emit currentChanged();
    };
    if (m_brush) {
        connect(m_brush, &BrushSettings::changed, this, onSettingsMutated);
        connect(m_brush, &BrushSettings::materialMixtureChanged, this, onSettingsMutated);
        connect(m_brush, &BrushSettings::brushEngineChanged, this, [this] {
            if (!m_categoryFilter.isEmpty() && !categories().contains(m_categoryFilter)) {
                m_categoryFilter.clear();
                emit filterChanged();
            }
            if (isBrushTool()) apply();
            emit tabsChanged();
            emit subToolsChanged();
            emit currentChanged();
        });
    }
    if (m_toolSettings) {
        connect(m_toolSettings, &ToolSettings::changed, this, onSettingsMutated);
    }
}

void SubToolLibrary::setToolSettings(ToolSettings* toolSettings)
{
    if (m_toolSettings == toolSettings) return;
    if (m_toolSettings) disconnect(m_toolSettings, nullptr, this, nullptr);
    m_toolSettings = toolSettings;
    if (m_toolSettings) {
        connect(m_toolSettings, &ToolSettings::changed, this, [this] {
            if (m_applying || !hasCollections() || (m_brush && m_brush->hasNativeBrush())) return;
            capture();
            persist();
            emit subToolsChanged();
            emit currentChanged();
        });
    }
    if (hasCollections()) apply();
}

bool SubToolLibrary::initializePersistence(const QString& filePath)
{
    m_persistencePath = filePath;
    if (filePath.isEmpty()) return false;

    const bool existed = QFile::exists(filePath);
    BrushLibraryDocument loaded;
    QString err;
    m_persistenceLoadFailed = !BrushLibraryStore::load(filePath, m_bundledCollections, &loaded, &err);
    if (m_persistenceLoadFailed) {
        setLastError(err);
        m_collections = m_bundledCollections;
    } else {
        m_collections = std::move(loaded);
        setLastError({});
    }
    if (!existed && !persist() && err.isEmpty()) err = m_lastError;
    apply();
    emit tabsChanged();
    emit subToolsChanged();
    emit currentChanged();
    return err.isEmpty();
}

bool SubToolLibrary::isPaintTool() const
{
    return isStrokePaintToolName(m_tool);
}

bool SubToolLibrary::hasCollections() const
{
    return !m_tool.isEmpty() && (isBrushTool() || m_collections.toolCollections.contains(m_tool));
}

QString SubToolLibrary::executableTool() const
{
    const auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    if (tab && idx >= 0) {
        const auto& exec = tab->brushes[idx].current.executableTool;
        if (!exec.isEmpty()) return exec;
    }
    return m_tool;
}

QString SubToolLibrary::currentCollectionKey() const
{
    if (isBrushTool()) {
        return (m_brush && m_brush->isRealisticEngine())
            ? QStringLiteral("brush:realistic")
            : QStringLiteral("brush:digital");
    }
    return m_tool;
}

EngineCollectionData* SubToolLibrary::collectionForTool(const QString& toolFamily, const QString& engineOverride)
{
    if (toolFamily == QStringLiteral("brush")) {
        const bool realistic = !engineOverride.isEmpty()
            ? (engineOverride == QStringLiteral("realistic"))
            : (m_brush && m_brush->isRealisticEngine());
        return realistic ? &m_collections.realistic : &m_collections.digital;
    }
    auto it = m_collections.toolCollections.find(toolFamily);
    if (it != m_collections.toolCollections.end()) return &it.value();
    return nullptr;
}

const EngineCollectionData* SubToolLibrary::collectionForTool(const QString& toolFamily, const QString& engineOverride) const
{
    if (toolFamily == QStringLiteral("brush")) {
        const bool realistic = !engineOverride.isEmpty()
            ? (engineOverride == QStringLiteral("realistic"))
            : (m_brush && m_brush->isRealisticEngine());
        return realistic ? &m_collections.realistic : &m_collections.digital;
    }
    auto it = m_collections.toolCollections.constFind(toolFamily);
    if (it != m_collections.toolCollections.constEnd()) return &it.value();
    return nullptr;
}

EngineCollectionData* SubToolLibrary::activeCollectionPtr()
{
    return collectionForTool(m_tool);
}

const EngineCollectionData* SubToolLibrary::activeCollectionPtr() const
{
    return collectionForTool(m_tool);
}

int SubToolLibrary::findTabIndex(const EngineCollectionData& col, const QString& tabId) const
{
    for (int i = 0; i < col.tabs.size(); ++i)
        if (col.tabs[i].id == tabId) return i;
    return -1;
}

BrushTabData* SubToolLibrary::currentTabPtr()
{
    auto* col = activeCollectionPtr();
    if (!col) return nullptr;
    int idx = findTabIndex(*col, col->currentTabId);
    if (idx < 0 && !col->tabs.isEmpty()) {
        idx = 0;
        col->currentTabId = col->tabs[0].id;
    }
    return idx >= 0 ? &col->tabs[idx] : nullptr;
}

const BrushTabData* SubToolLibrary::currentTabPtr() const
{
    const auto* col = activeCollectionPtr();
    if (!col) return nullptr;
    int idx = findTabIndex(*col, col->currentTabId);
    if (idx < 0 && !col->tabs.isEmpty()) idx = 0;
    return idx >= 0 ? &col->tabs[idx] : nullptr;
}

int SubToolLibrary::currentBrushIndexInTab(const BrushTabData* tab) const
{
    if (!tab || tab->brushes.isEmpty()) return -1;
    for (int i = 0; i < tab->brushes.size(); ++i)
        if (tab->brushes[i].id == tab->currentBrushId) return i;
    return 0;
}

QString SubToolLibrary::activeEngine() const
{
    return m_brush ? m_brush->brushEngine() : QStringLiteral("digital");
}

bool SubToolLibrary::paints(const QString& tool) const
{
    return isStrokePaintToolName(tool);
}

bool SubToolLibrary::usesSubTools(const QString& tool) const
{
    if (isKnownViewOnlyTool(tool)) return false;
    return tool == QStringLiteral("brush") || m_collections.toolCollections.contains(tool);
}

QStringList SubToolLibrary::paintTools() const
{
    return {
        QStringLiteral("pen"),
        QStringLiteral("pencil"),
        QStringLiteral("brush"),
        QStringLiteral("airbrush"),
        QStringLiteral("eraser")
    };
}

QStringList SubToolLibrary::supportedTools() const
{
    QStringList out = m_collections.toolCollections.keys();
    if (!out.contains(QStringLiteral("brush"))) out.append(QStringLiteral("brush"));
    out.sort();
    return out;
}

QStringList SubToolLibrary::compatibleToolsFor(const QString& toolOrExecutable) const
{
    const QString t = toolOrExecutable.trimmed();
    if (isStrokeBrushTool(t)) {
        return {
            QStringLiteral("pen"),
            QStringLiteral("pencil"),
            QStringLiteral("brush"),
            QStringLiteral("airbrush"),
            QStringLiteral("eraser")
        };
    }
    if (isSelectionTool(t)) {
        return { QStringLiteral("selection"), QStringLiteral("lasso"), QStringLiteral("wand") };
    }
    if (isShapeTool(t)) {
        return { QStringLiteral("line"), QStringLiteral("curveline"),
                 QStringLiteral("rectangle"), QStringLiteral("circle") };
    }
    if (t == QStringLiteral("smart_fill") || t == QStringLiteral("fill")) {
        return { QStringLiteral("fill") };
    }
    if (!t.isEmpty() && m_collections.toolCollections.contains(t)) {
        return { t };
    }
    return {};
}

bool SubToolLibrary::ensureToolCollection(const QString& toolFamily)
{
    const QString key = toolFamily.trimmed();
    if (key.isEmpty() || isKnownViewOnlyTool(key) || key == QStringLiteral("brush")) return false;
    if (m_collections.toolCollections.contains(key)) return true;

    EngineCollectionData col;
    BrushTabData tab;
    tab.id = QStringLiteral("%1-default").arg(key);
    tab.name = QStringLiteral("Default");
    BrushEntryData entry;
    entry.id = QStringLiteral("builtin-%1-default").arg(key);
    entry.name = key;
    entry.category = QStringLiteral("Default");
    entry.builtIn = true;
    entry.defaults.executableTool = key;
    entry.current = entry.defaults;
    tab.brushes.append(entry);
    tab.currentBrushId = entry.id;
    col.defaultTabId = tab.id;
    col.currentTabId = tab.id;
    col.tabs.append(tab);
    m_collections.toolCollections.insert(key, std::move(col));
    persist();
    if (m_tool == key) {
        emit toolChanged();
        emit tabsChanged();
        emit subToolsChanged();
        emit currentChanged();
    }
    return true;
}

void SubToolLibrary::setTool(const QString& tool)
{
    if (tool == m_tool) return;
    m_tool = tool;
    if (!m_categoryFilter.isEmpty() && !categories().contains(m_categoryFilter)) {
        m_categoryFilter.clear();
        emit filterChanged();
    }
    apply();
    emit toolChanged();
    emit tabsChanged();
    emit subToolsChanged();
    emit currentChanged();
}

void SubToolLibrary::setSearchText(const QString& text)
{
    const QString normalized = text.trimmed();
    if (m_searchText == normalized) return;
    m_searchText = normalized;
    emit filterChanged();
    emit subToolsChanged();
}

void SubToolLibrary::setCategoryFilter(const QString& category)
{
    const QString normalized = category.trimmed();
    const QString accepted = categories().contains(normalized) ? normalized : QString();
    if (m_categoryFilter == accepted) return;
    m_categoryFilter = accepted;
    emit filterChanged();
    emit subToolsChanged();
}

QStringList SubToolLibrary::categories() const
{
    QStringList result;
    const auto* tab = currentTabPtr();
    if (!tab) return result;
    for (const auto& b : tab->brushes)
        if (!result.contains(b.category)) result.append(b.category);
    return result;
}

QVariantList SubToolLibrary::engineTabs() const
{
    QVariantList list;
    const auto* col = activeCollectionPtr();
    if (!col) return list;
    for (int i = 0; i < col->tabs.size(); ++i) {
        const auto& tab = col->tabs[i];
        QVariantMap item;
        item.insert(QStringLiteral("index"), i);
        item.insert(QStringLiteral("id"), tab.id);
        item.insert(QStringLiteral("name"), tab.name);
        item.insert(QStringLiteral("isDefault"), tab.id == col->defaultTabId);
        item.insert(QStringLiteral("brushCount"), tab.brushes.size());
        list.push_back(item);
    }
    return list;
}

QVariantList SubToolLibrary::tabsForTool(const QString& targetTool) const
{
    QVariantList list;
    const auto* col = collectionForTool(targetTool);
    if (!col) return list;
    for (int i = 0; i < col->tabs.size(); ++i) {
        QVariantMap item;
        item.insert(QStringLiteral("index"), i);
        item.insert(QStringLiteral("id"), col->tabs[i].id);
        item.insert(QStringLiteral("name"), col->tabs[i].name);
        item.insert(QStringLiteral("brushCount"), col->tabs[i].brushes.size());
        list.push_back(item);
    }
    return list;
}

int SubToolLibrary::currentTabIndex() const
{
    const auto* col = activeCollectionPtr();
    if (!col) return -1;
    const int idx = findTabIndex(*col, col->currentTabId);
    return idx >= 0 ? idx : (col->tabs.isEmpty() ? -1 : 0);
}

QString SubToolLibrary::currentTabId() const
{
    const auto* col = activeCollectionPtr();
    const int idx = currentTabIndex();
    return (col && idx >= 0) ? col->tabs[idx].id : QString();
}

QString SubToolLibrary::currentTabName() const
{
    const auto* col = activeCollectionPtr();
    const int idx = currentTabIndex();
    return (col && idx >= 0) ? col->tabs[idx].name : QString();
}

QString SubToolLibrary::defaultTabId() const
{
    const auto* col = activeCollectionPtr();
    return col ? col->defaultTabId : QString();
}

void SubToolLibrary::selectTab(int index)
{
    auto* col = activeCollectionPtr();
    if (!col || index < 0 || index >= col->tabs.size()) return;
    if (col->currentTabId == col->tabs[index].id) return;
    col->currentTabId = col->tabs[index].id;
    if (!m_categoryFilter.isEmpty()) {
        m_categoryFilter.clear();
        emit filterChanged();
    }
    if (hasCollections()) apply();
    emit tabsChanged();
    emit subToolsChanged();
    emit currentChanged();
}

void SubToolLibrary::selectTabById(const QString& tabId)
{
    const auto* col = activeCollectionPtr();
    if (!col) return;
    const int idx = findTabIndex(*col, tabId);
    if (idx >= 0) selectTab(idx);
}

QString SubToolLibrary::createTab(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setLastError(QStringLiteral("Tab name cannot be empty."));
        return {};
    }
    auto* col = activeCollectionPtr();
    if (!col) {
        if (!ensureToolCollection(m_tool)) return {};
        col = activeCollectionPtr();
    }
    if (!col) return {};

    BrushTabData tab;
    tab.id = makeId(QStringLiteral("tab"));
    tab.name = trimmed;
    col->tabs.append(tab);
    col->currentTabId = tab.id;
    persist();
    if (hasCollections()) apply();
    emit tabsChanged();
    emit subToolsChanged();
    emit currentChanged();
    return tab.id;
}

bool SubToolLibrary::renameTab(const QString& tabId, const QString& newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        setLastError(QStringLiteral("Tab name cannot be empty."));
        return false;
    }
    auto* col = activeCollectionPtr();
    if (!col) return false;
    const int idx = findTabIndex(*col, tabId);
    if (idx < 0) return false;
    if (col->tabs[idx].name == trimmed) return true;
    col->tabs[idx].name = trimmed;
    persist();
    emit tabsChanged();
    return true;
}

bool SubToolLibrary::moveTab(int fromIndex, int toIndex)
{
    auto* col = activeCollectionPtr();
    if (!col || fromIndex < 0 || fromIndex >= col->tabs.size()
        || toIndex < 0 || toIndex >= col->tabs.size()
        || fromIndex == toIndex) {
        return false;
    }
    col->tabs.move(fromIndex, toIndex);
    persist();
    emit tabsChanged();
    return true;
}

bool SubToolLibrary::moveTabById(const QString& tabId, int delta)
{
    const auto* col = activeCollectionPtr();
    if (!col) return false;
    const int idx = findTabIndex(*col, tabId);
    if (idx < 0) return false;
    return moveTab(idx, idx + delta);
}

bool SubToolLibrary::setDefaultTab(const QString& tabId)
{
    auto* col = activeCollectionPtr();
    if (!col || findTabIndex(*col, tabId) < 0) return false;
    if (col->defaultTabId == tabId) return true;
    col->defaultTabId = tabId;
    persist();
    emit tabsChanged();
    return true;
}

bool SubToolLibrary::deleteTab(const QString& tabId)
{
    auto* col = activeCollectionPtr();
    if (!col) return false;
    const int idx = findTabIndex(*col, tabId);
    if (idx < 0) return false;
    if (col->tabs.size() <= 1) {
        setLastError(QStringLiteral("At least one collection tab is required."));
        return false;
    }
    const bool wasCurrent = (col->currentTabId == tabId);
    const bool wasDefault = (col->defaultTabId == tabId);

    m_deletedTabBackup.collectionKey = currentCollectionKey();
    m_deletedTabBackup.index = idx;
    m_deletedTabBackup.tab = col->tabs.at(idx);
    m_deletedTabBackup.wasDefault = wasDefault;
    m_deletedTabBackup.wasCurrent = wasCurrent;
    m_hasDeletedTabBackup = true;

    col->tabs.removeAt(idx);
    const int fallbackIdx = qBound(0, idx, col->tabs.size() - 1);
    if (wasDefault) col->defaultTabId = col->tabs[fallbackIdx].id;
    if (wasCurrent) col->currentTabId = col->tabs[fallbackIdx].id;
    persist();
    if (wasCurrent && hasCollections()) apply();
    emit tabsChanged();
    emit subToolsChanged();
    emit currentChanged();
    return true;
}

bool SubToolLibrary::undoDeleteTab()
{
    if (!m_hasDeletedTabBackup) return false;
    EngineCollectionData* col = nullptr;
    if (m_deletedTabBackup.collectionKey == QStringLiteral("brush:digital")) {
        col = &m_collections.digital;
    } else if (m_deletedTabBackup.collectionKey == QStringLiteral("brush:realistic")) {
        col = &m_collections.realistic;
    } else {
        col = collectionForTool(m_deletedTabBackup.collectionKey);
    }
    if (!col) return false;

    const int insertIdx = qBound(0, m_deletedTabBackup.index, col->tabs.size());
    col->tabs.insert(insertIdx, m_deletedTabBackup.tab);
    if (m_deletedTabBackup.wasDefault) col->defaultTabId = m_deletedTabBackup.tab.id;
    if (m_deletedTabBackup.wasCurrent) col->currentTabId = m_deletedTabBackup.tab.id;
    m_hasDeletedTabBackup = false;
    persist();
    if (m_deletedTabBackup.collectionKey == currentCollectionKey() && hasCollections()) {
        apply();
    }
    emit tabsChanged();
    emit subToolsChanged();
    emit currentChanged();
    return true;
}

bool SubToolLibrary::cycleExecutableFamily(const QStringList& family)
{
    auto* tab = currentTabPtr();
    if (!tab || family.isEmpty()) return false;
    const int count = static_cast<int>(tab->brushes.size());
    if (count < 2) return false;
    const int current = currentBrushIndexInTab(tab);
    if (current < 0) return false;
    // Only presets inside the family count; walking stops at the next match so a
    // lone Fill preset without a Smart Fill sibling simply reports "no move".
    for (int step = 1; step < count; ++step) {
        const int candidate = (current + step) % count;
        // A preset may leave the executable empty, which means "the tool this
        // collection belongs to"; resolve it the same way executableTool() does.
        const QString exec = tab->brushes[candidate].current.executableTool;
        const QString resolved = exec.isEmpty() ? m_tool : exec;
        if (!family.contains(resolved)) continue;
        if (candidate == current) return false;
        select(candidate);
        return true;
    }
    return false;
}

void SubToolLibrary::select(int index)
{
    auto* tab = currentTabPtr();
    if (!tab || index < 0 || index >= tab->brushes.size()) return;
    const QString nextId = tab->brushes[index].id;
    if (tab->currentBrushId == nextId && (!m_brush || m_brush->nativeBrushIssue().isEmpty())) return;
    tab->currentBrushId = nextId;
    apply();
    emit subToolsChanged();
    emit currentChanged();
}

void SubToolLibrary::selectById(const QString& brushId)
{
    auto* tab = currentTabPtr();
    if (!tab) return;
    for (int i = 0; i < tab->brushes.size(); ++i) {
        if (tab->brushes[i].id == brushId) {
            select(i);
            return;
        }
    }
}

QVariantMap SubToolLibrary::installNativeBrush(const QUrl& file)
{
    return installNativeBrushInTab(file, QString());
}

QVariantMap SubToolLibrary::installNativeBrushInTab(const QUrl& file, const QString& targetTabId) try
{
    QVariantMap outcome{
        {QStringLiteral("installed"), false},
        {QStringLiteral("supported"), false},
        {QStringLiteral("id"), QString()},
        {QStringLiteral("name"), QString()},
        {QStringLiteral("tabId"), QString()},
        {QStringLiteral("tabName"), QString()},
        {QStringLiteral("error"), QString()},
    };
    auto failOutcome = [&](const QString& message) {
        outcome.insert(QStringLiteral("error"), message);
        setLastError(message);
        return outcome;
    };
    if (!file.isLocalFile() || file.toLocalFile().isEmpty())
        return failOutcome(tr("Choose a local .kb brush file to install."));

    brush::BrushDocument doc;
    QByteArray original;
    QString error;
    if (!BrushLibraryStore::readNativeFile(file.toLocalFile(), {}, &doc, &original, &error)) {
        // The file itself could not be read. That is an installation failure,
        // not a statement that the renderer rejects an unsupported brush.
        outcome.insert(QStringLiteral("supported"), true);
        return failOutcome(error);
    }
    const bool realistic = doc.engine == brush::Engine::Realistic;
    const auto capability = drawing::inspectNativeBrush(doc);
    outcome.insert(QStringLiteral("supported"), capability.supported);
    if (!capability.supported) {
        // The caller may already have saved this file. Do not copy an
        // unrenderable document into the library, and do not imply it was.
        return failOutcome(QString::fromStdString(capability.summary()));
    }
    // From here on the document is renderable; any failure is a library
    // failure, not a renderer-support failure.
    if (m_persistenceLoadFailed || m_persistencePath.isEmpty())
        return failOutcome(tr("The brush library is not available for writing."));

    auto next = m_collections;
    auto& collection = realistic ? next.realistic : next.digital;
    const QString engineName = realistic ? tr("Realistic") : tr("Digital");
    QString target = targetTabId;
    if (target.isEmpty()) {
        const QString current = collection.currentTabId;
        target = (m_tool == QStringLiteral("brush") && activeEngine() == engineName.toLower() &&
                  findTabIndex(collection, current) >= 0)
            ? current : collection.defaultTabId;
    }
    int targetIndex = findTabIndex(collection, target);
    if (targetIndex < 0)
        return failOutcome(tr("The target %1 brush tab no longer exists.").arg(engineName));

    const QString name = QString::fromStdString(doc.name).trimmed();
    const QString updateId = QString::fromStdString(doc.id);
    // A reinstall updates the exact entry in place. A new brush is installed
    // into the requested/current tab, never silently into Default.
    BrushEntryData* installed = nullptr;
    for (auto& tab : collection.tabs) {
        for (auto& entry : tab.brushes) {
            if (entry.isNative() && !updateId.isEmpty() && entry.nativeUpdateId == updateId) {
                installed = &entry;
                target = tab.id;
                targetIndex = findTabIndex(collection, target);
                break;
            }
        }
        if (installed) break;
    }
    if (targetIndex < 0) return failOutcome(tr("The target %1 brush tab no longer exists.").arg(engineName));

    const QString key = BrushLibraryStore::installNativeFile(m_persistencePath, original, &error);
    if (key.isEmpty()) return failOutcome(error);
    if (!installed) {
        auto& tab = collection.tabs[targetIndex];
        BrushEntryData entry;
        entry.id = makeId(QStringLiteral("native"));
        entry.name = uniqueBrushName(tab, name.isEmpty() ? tr("Native brush") : name);
        entry.category = QString::fromStdString(doc.category).trimmed();
        if (entry.category.isEmpty()) entry.category = tr("Native");
        entry.defaults.executableTool = QStringLiteral("brush");
        entry.current = entry.defaults;
        entry.nativeUpdateId = updateId;
        tab.brushes.append(std::move(entry));
        installed = &tab.brushes.last();
    }
    installed->nativeAssetKey = key;
    installed->nativeUpdateId = updateId;
    // Keep only overrides the production renderer still consumes.
    for (auto it = installed->nativeOverrides.begin(); it != installed->nativeOverrides.end();) {
        const auto* info = brush::findParameter(it.key().toStdString());
        if (!info || info->scope != brush::ParameterScope::Brush ||
            !(realistic ? drawing::realisticSupportsParameter(info->name)
                        : drawing::digitalSupportsParameter(info->name))) {
            it = installed->nativeOverrides.erase(it);
        } else {
            ++it;
        }
    }
    const QString id = installed->id;
    const QString installedName = installed->name;
    const QString installedTabName = collection.tabs[targetIndex].name;
    collection.currentTabId = target;
    collection.tabs[targetIndex].currentBrushId = id;

    if (!BrushLibraryStore::save(m_persistencePath, next, &error))
        return failOutcome(error);
    m_collections = std::move(next);
    setLastError({});
    if (isBrushTool() && activeEngine() == (realistic ? QStringLiteral("realistic") : QStringLiteral("digital"))) apply();
    emit tabsChanged();
    emit subToolsChanged();
    emit currentChanged();
    outcome.insert(QStringLiteral("installed"), true);
    outcome.insert(QStringLiteral("id"), id);
    outcome.insert(QStringLiteral("name"), installedName);
    outcome.insert(QStringLiteral("tabId"), target);
    outcome.insert(QStringLiteral("tabName"), installedTabName);
    return outcome;
} catch (const std::bad_alloc&) {
    setLastError(tr("Not enough memory to install the native brush."));
    return {{QStringLiteral("installed"), false}, {QStringLiteral("supported"), false},
            {QStringLiteral("error"), lastError()}};
} catch (const std::length_error&) {
    setLastError(tr("The native brush is too large to install on this system."));
    return {{QStringLiteral("installed"), false}, {QStringLiteral("supported"), false},
            {QStringLiteral("error"), lastError()}};
}

QString SubToolLibrary::registerNativeBrush(const QUrl& file)
{
    const auto result = installNativeBrush(file);
    return result.value(QStringLiteral("installed")).toBool()
        ? result.value(QStringLiteral("id")).toString() : QString();
}

void SubToolLibrary::resetCurrent()
{
    const QString id = currentBrushId();
    if (!id.isEmpty()) resetBrush(id);
}

void SubToolLibrary::resetBrush(const QString& brushId)
{
    auto* col = activeCollectionPtr();
    if (!col) return;
    for (auto& tab : col->tabs) {
        for (int i = 0; i < tab.brushes.size(); ++i) {
            if (tab.brushes[i].id == brushId) {
                auto& entry = tab.brushes[i];
                if (entry.isNative()) {
                    if (entry.nativeOverrides.isEmpty()) return;
                    const auto previous = entry.nativeOverrides;
                    entry.nativeOverrides.clear();
                    if (!persist()) {
                        entry.nativeOverrides = previous;
                        return;
                    }
                    if (tab.id == currentTabId() && tab.currentBrushId == brushId)
                        apply();
                    emit subToolsChanged();
                    emit currentChanged();
                    return;
                }
                if (entry.current == entry.defaults) return;
                entry.current = entry.defaults;
                persist();
                if (tab.id == currentTabId() && tab.currentBrushId == brushId)
                    apply();
                emit subToolsChanged();
                emit currentChanged();
                return;
            }
        }
    }
}

const BrushEntryData* SubToolLibrary::currentNativeEntry() const
{
    const auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    if (!tab || idx < 0 || idx >= tab->brushes.size()) return nullptr;
    return tab->brushes[idx].isNative() ? &tab->brushes[idx] : nullptr;
}

QVariantList SubToolLibrary::nativeControls() const
{
    const auto* entry = currentNativeEntry();
    if (!entry || !m_brush) return {};
    const auto document = m_brush->nativeBrushDocument();
    if (!document) return {};
    return nativeControlRows(*document, entry->nativeOverrides);
}

bool SubToolLibrary::currentNativeModified() const
{
    const auto* entry = currentNativeEntry();
    return entry && !entry->nativeOverrides.isEmpty();
}

bool SubToolLibrary::setNativeControl(const QString& key, double value) try
{
    auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    if (!tab || idx < 0 || idx >= tab->brushes.size() || !tab->brushes[idx].isNative()) {
        setLastError(tr("Select an installed native brush first."));
        return false;
    }
    const auto* info = brush::findParameter(key.toStdString());
    if (!info || info->scope != brush::ParameterScope::Brush ||
        !drawing::digitalSupportsParameter(info->name) || !brush::parameterInRange(*info, value)) {
        setLastError(tr("'%1' is not a supported native brush control.").arg(key));
        return false;
    }
    bool offered = false;
    for (const auto& row : nativeControls()) {
        if (row.toMap().value(QStringLiteral("key")).toString() == key) { offered = true; break; }
    }
    if (!offered) {
        setLastError(tr("'%1' is not exposed for this brush.").arg(key));
        return false;
    }
    const auto document = m_brush ? m_brush->nativeBrushDocument() : nullptr;
    if (!document) {
        setLastError(tr("The native brush document is not available."));
        return false;
    }
    brush::BrushDocument candidate = *document;
    candidate.setParameter(info->name, value);
    const auto issue = drawing::digitalBrushIssue(candidate);
    if (!issue.empty()) {
        setLastError(QString::fromStdString(issue));
        return false;
    }
    auto& entry = tab->brushes[idx];
    const auto previous = entry.nativeOverrides;
    entry.nativeOverrides.insert(key, value);
    if (!persist()) {
        entry.nativeOverrides = previous;
        return false;
    }
    apply();
    setLastError({});
    emit subToolsChanged();
    emit currentChanged();
    return true;
} catch (const std::bad_alloc&) {
    setLastError(tr("Not enough memory to adjust the native brush."));
    return false;
} catch (const std::length_error&) {
    setLastError(tr("The native brush adjustment is too large."));
    return false;
}

bool SubToolLibrary::resetCurrentNativeOverrides()
{
    auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    if (!tab || idx < 0 || idx >= tab->brushes.size() || !tab->brushes[idx].isNative()) return false;
    auto& entry = tab->brushes[idx];
    if (entry.nativeOverrides.isEmpty()) return true;
    const auto previous = entry.nativeOverrides;
    entry.nativeOverrides.clear();
    if (!persist()) {
        entry.nativeOverrides = previous;
        return false;
    }
    apply();
    emit subToolsChanged();
    emit currentChanged();
    return true;
}

QString SubToolLibrary::uniqueBrushName(const BrushTabData& tab, const QString& baseName) const
{
    auto exists = [&](const QString& candidate) {
        for (const auto& b : tab.brushes)
            if (b.name.compare(candidate, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    if (!exists(baseName)) return baseName;
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const QString candidate = QStringLiteral("%1 %2").arg(baseName).arg(suffix);
        if (!exists(candidate)) return candidate;
    }
    return baseName;
}

QString SubToolLibrary::createBrushFromCurrent(const QString& name)
{
    auto* tab = currentTabPtr();
    if (!tab) return {};
    const bool isRealistic = isBrushTool() && m_brush && m_brush->isRealisticEngine();
    QString execHint = executableTool();
    if (execHint.isEmpty()) execHint = m_tool;
    bool eraseHint = (m_tool == QStringLiteral("eraser")) ||
                     (m_brush && m_brush->erase());
    const int idx = currentBrushIndexInTab(tab);
    if (idx >= 0) {
        eraseHint = tab->brushes[idx].current.erase;
    }
    const bool native = idx >= 0 && tab->brushes[idx].isNative();
    const Values captured = native ? tab->brushes[idx].current
                                  : captureFromSettings(isRealistic, execHint, eraseHint);
    const QString base = name.trimmed().isEmpty()
        ? (currentName().isEmpty() ? QStringLiteral("Custom Sub-Tool") : currentName() + QStringLiteral(" Copy"))
        : name.trimmed();
    BrushEntryData entry;
    entry.id = makeId(QStringLiteral("subtool"));
    entry.name = uniqueBrushName(*tab, base);
    entry.category = tab->name;
    entry.builtIn = false;
    entry.defaults = captured;
    entry.current = captured;
    if (native) entry.nativeAssetKey = tab->brushes[idx].nativeAssetKey;
    tab->brushes.append(entry);
    tab->currentBrushId = entry.id;
    persist();
    apply();
    emit tabsChanged();
    emit subToolsChanged();
    emit currentChanged();
    return entry.id;
}

QString SubToolLibrary::duplicateBrush(int index)
{
    auto* tab = currentTabPtr();
    if (!tab || index < 0 || index >= tab->brushes.size()) return {};
    return duplicateBrushById(tab->brushes[index].id);
}

QString SubToolLibrary::duplicateBrushById(const QString& brushId)
{
    auto* tab = currentTabPtr();
    if (!tab) return {};
    for (int i = 0; i < tab->brushes.size(); ++i) {
        if (tab->brushes[i].id != brushId) continue;
        BrushEntryData copy = tab->brushes[i];
        copy.nativeUpdateId.clear();
        copy.id = makeId(QStringLiteral("subtool"));
        copy.builtIn = false;
        copy.name = uniqueBrushName(*tab, copy.name + QStringLiteral(" Copy"));
        copy.defaults = copy.current;
        tab->brushes.insert(i + 1, copy);
        tab->currentBrushId = copy.id;
        persist();
        apply();
        emit tabsChanged();
        emit subToolsChanged();
        emit currentChanged();
        return copy.id;
    }
    return {};
}

bool SubToolLibrary::renameBrush(const QString& brushId, const QString& newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        setLastError(QStringLiteral("Sub-tool name cannot be empty."));
        return false;
    }
    auto* col = activeCollectionPtr();
    if (!col) return false;
    for (auto& tab : col->tabs) {
        for (auto& b : tab.brushes) {
            if (b.id == brushId) {
                if (b.name == trimmed) return true;
                b.name = trimmed;
                persist();
                emit subToolsChanged();
                if (b.id == currentBrushId()) emit currentChanged();
                return true;
            }
        }
    }
    return false;
}

bool SubToolLibrary::deleteBrush(const QString& brushId)
{
    auto* col = activeCollectionPtr();
    if (!col) return false;
    for (auto& tab : col->tabs) {
        for (int i = 0; i < tab.brushes.size(); ++i) {
            if (tab.brushes[i].id != brushId) continue;
            const bool wasCurrent = (tab.id == currentTabId() && tab.currentBrushId == brushId);
            tab.brushes.removeAt(i);
            if (tab.brushes.isEmpty()) {
                tab.currentBrushId.clear();
            } else if (tab.currentBrushId == brushId) {
                tab.currentBrushId = tab.brushes[qBound(0, i, tab.brushes.size() - 1)].id;
            }
            persist();
            if (wasCurrent) apply();
            emit tabsChanged();
            emit subToolsChanged();
            emit currentChanged();
            return true;
        }
    }
    return false;
}

bool SubToolLibrary::moveBrushToTab(const QString& brushId, const QString& targetTabId)
{
    return moveSubToolTo(brushId, m_tool, targetTabId);
}

QString SubToolLibrary::copyBrushToTab(const QString& brushId, const QString& targetTabId)
{
    return copySubToolTo(brushId, m_tool, targetTabId);
}

bool SubToolLibrary::moveSubToolTo(const QString& brushId, const QString& targetTool,
                                   const QString& targetTabId)
{
    auto* srcCol = activeCollectionPtr();
    auto* dstCol = collectionForTool(targetTool);
    if (!srcCol || !dstCol) return false;
    const int dstIdx = findTabIndex(*dstCol, targetTabId);
    if (dstIdx < 0) return false;

    for (int srcIdx = 0; srcIdx < srcCol->tabs.size(); ++srcIdx) {
        if (srcCol == dstCol && srcIdx == dstIdx) continue;
        auto& src = srcCol->tabs[srcIdx];
        for (int i = 0; i < src.brushes.size(); ++i) {
            if (src.brushes[i].id != brushId) continue;
            const QString exec = src.brushes[i].current.executableTool.isEmpty()
                ? m_tool : src.brushes[i].current.executableTool;
            if (targetTool != m_tool && (src.brushes[i].isNative() || !compatibleToolsFor(exec).contains(targetTool))) {
                setLastError(QStringLiteral("This sub-tool is not compatible with %1.").arg(targetTool));
                return false;
            }
            BrushEntryData moved = src.brushes.takeAt(i);
            if (moved.defaults.executableTool.isEmpty()) moved.defaults.executableTool = exec;
            if (moved.current.executableTool.isEmpty()) moved.current.executableTool = exec;
            if (src.brushes.isEmpty()) {
                src.currentBrushId.clear();
            } else if (src.currentBrushId == brushId) {
                src.currentBrushId = src.brushes[qBound(0, i, src.brushes.size() - 1)].id;
            }
            auto& dst = dstCol->tabs[dstIdx];
            moved.name = uniqueBrushName(dst, moved.name);
            dst.brushes.append(moved);
            if (dst.currentBrushId.isEmpty()) dst.currentBrushId = moved.id;
            persist();
            if (src.id == currentTabId()) apply();
            emit tabsChanged();
            emit subToolsChanged();
            emit currentChanged();
            return true;
        }
    }
    return false;
}

QString SubToolLibrary::copySubToolTo(const QString& brushId, const QString& targetTool,
                                      const QString& targetTabId)
{
    const auto* srcCol = activeCollectionPtr();
    auto* dstCol = collectionForTool(targetTool);
    if (!srcCol || !dstCol) return {};
    const int dstIdx = findTabIndex(*dstCol, targetTabId);
    if (dstIdx < 0) return {};

    for (const auto& src : srcCol->tabs) {
        for (const auto& b : src.brushes) {
            if (b.id != brushId) continue;
            const QString exec = b.current.executableTool.isEmpty()
                ? m_tool : b.current.executableTool;
            if (targetTool != m_tool && (b.isNative() || !compatibleToolsFor(exec).contains(targetTool))) {
                setLastError(QStringLiteral("This sub-tool is not compatible with %1.").arg(targetTool));
                return {};
            }
            auto& dst = dstCol->tabs[dstIdx];
            BrushEntryData copy = b;
            copy.nativeUpdateId.clear();
            copy.id = makeId(QStringLiteral("subtool"));
            copy.builtIn = false;
            if (copy.defaults.executableTool.isEmpty()) copy.defaults.executableTool = exec;
            if (copy.current.executableTool.isEmpty()) copy.current.executableTool = exec;
            copy.name = uniqueBrushName(dst, b.name);
            copy.defaults = copy.current;
            dst.brushes.append(copy);
            if (dst.currentBrushId.isEmpty()) dst.currentBrushId = copy.id;
            persist();
            emit tabsChanged();
            emit subToolsChanged();
            return copy.id;
        }
    }
    return {};
}

bool SubToolLibrary::reorderBrush(int fromIndex, int toIndex)
{
    auto* tab = currentTabPtr();
    if (!tab || fromIndex < 0 || fromIndex >= tab->brushes.size()
        || toIndex < 0 || toIndex >= tab->brushes.size()
        || fromIndex == toIndex) {
        return false;
    }
    tab->brushes.move(fromIndex, toIndex);
    persist();
    emit subToolsChanged();
    emit currentChanged();
    return true;
}

void SubToolLibrary::applyValuesToSettings(const Values& values, bool fallbackErase)
{
    m_applying = true;
    if (m_brush && isPaintTool()) {
        m_brush->setSize(values.size);
        m_brush->setOpacity(values.opacity);
        m_brush->setFlow(values.flow);
        m_brush->setSpacing(values.spacing);
        m_brush->setHardness(values.hardness);
        m_brush->setStabilization(values.stabilization);
        m_brush->setTipStyle(values.tipStyle);
        m_brush->setTextured(values.textured);
        m_brush->setPressureSize(values.pressureSize);
        m_brush->setPressureOpacity(values.pressureOpacity);
        m_brush->setPressureFlow(values.pressureFlow);
        m_brush->setPressureGamma(values.pressureGamma);
        m_brush->setMinSizeRatio(values.minSizeRatio);
        m_brush->setTiltSize(values.tiltSize);
        m_brush->setTiltAngle(values.tiltAngle);
        m_brush->setStylusRotation(values.stylusRotation);
        m_brush->setBlendStrength(values.blendStrength);
        m_brush->setColorMix(values.colorMix);
        m_brush->setTraditionalMedia(values.traditionalMedia);
        m_brush->setPersistentReservoir(values.persistentReservoir);
        m_brush->setMaterialPresetId(values.materialPresetId);
        m_brush->setMediumId(values.mediumId);
        m_brush->setPaintLoad(values.paintLoad);
        m_brush->setPigmentConcentration(values.pigmentConcentration);
        m_brush->setWaterAmount(values.waterAmount);
        m_brush->setPaintBody(values.paintBody);
        m_brush->setPickup(values.pickup);
        m_brush->setAspectRatio(values.aspectRatio);
        m_brush->setTipAngle(values.tipAngle);
        m_brush->setFollowDirection(values.followDirection);
        m_brush->setDepletion(values.depletion);
        m_brush->setEdgeAccumulation(values.edgeAccumulation);
        m_brush->setToothResponse(values.toothResponse);
        m_brush->setGranulation(values.granulation);
        m_brush->setDryingSpeed(values.dryingSpeed);
        m_brush->setPressureToAmount(values.pressureToAmount);
        m_brush->setPressureToContact(values.pressureToContact);
        if (values.materialMixture.isEmpty()) m_brush->clearMaterialMixture();
        else m_brush->loadMaterialMixture(values.materialMixture);
        const bool effectiveErase = values.erase || fallbackErase ||
            values.executableTool == QStringLiteral("eraser") ||
            values.executableTool == QStringLiteral("boundary_eraser");
        m_brush->setErase(effectiveErase);
    }
    if (m_toolSettings) {
        const QString exec = values.executableTool.isEmpty() ? m_tool : values.executableTool;
        m_toolSettings->fromMap(exec, values.toolParams);
    }
    m_applying = false;
}

SubToolLibrary::Values SubToolLibrary::captureFromSettings(bool traditionalMedia,
                                                           const QString& execToolHint,
                                                           bool eraseHint) const
{
    Values values;
    if (m_brush && isPaintTool()) {
        values.size = m_brush->size();
        values.opacity = m_brush->opacity();
        values.flow = m_brush->flow();
        values.spacing = m_brush->spacing();
        values.hardness = m_brush->hardness();
        values.stabilization = m_brush->stabilization();
        values.textured = m_brush->textured();
        values.tipStyle = m_brush->tipStyle();
        values.pressureSize = m_brush->pressureSize();
        values.pressureOpacity = m_brush->pressureOpacity();
        values.pressureFlow = m_brush->pressureFlow();
        values.pressureGamma = m_brush->pressureGamma();
        values.minSizeRatio = m_brush->minSizeRatio();
        values.tiltSize = m_brush->tiltSize();
        values.tiltAngle = m_brush->tiltAngle();
        values.stylusRotation = m_brush->stylusRotation();
        values.blendStrength = m_brush->blendStrength();
        values.colorMix = m_brush->colorMix();
        values.erase = m_brush->erase() || eraseHint;
        values.traditionalMedia = traditionalMedia;
        values.persistentReservoir = m_brush->persistentReservoir();
        values.mediumId = m_brush->mediumId();
        values.materialPresetId = m_brush->materialPresetId();
        values.paintLoad = m_brush->paintLoad();
        values.pigmentConcentration = m_brush->pigmentConcentration();
        values.waterAmount = m_brush->waterAmount();
        values.paintBody = m_brush->paintBody();
        values.pickup = m_brush->pickup();
        values.aspectRatio = m_brush->aspectRatio();
        values.tipAngle = m_brush->tipAngle();
        values.followDirection = m_brush->followDirection();
        values.depletion = m_brush->depletion();
        values.edgeAccumulation = m_brush->edgeAccumulation();
        values.toothResponse = m_brush->toothResponse();
        values.granulation = m_brush->granulation();
        values.dryingSpeed = m_brush->dryingSpeed();
        values.pressureToAmount = m_brush->pressureToAmount();
        values.pressureToContact = m_brush->pressureToContact();
        values.materialMixture = m_brush->materialMixture();
    } else {
        values.erase = eraseHint;
        values.traditionalMedia = traditionalMedia;
    }
    values.executableTool = execToolHint;
    if (m_toolSettings) {
        if (!m_toolSettings->executableTool().isEmpty()) {
            values.executableTool = m_toolSettings->executableTool();
        }
        values.toolParams = m_toolSettings->toMap();
    }
    return values;
}

void SubToolLibrary::apply()
{
    auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    const BrushEntryData* entry = idx < 0 ? nullptr : &tab->brushes[idx];
    if (entry) tab->currentBrushId = entry->id;
    if (m_brush) {
        std::shared_ptr<const brush::BrushDocument> native;
        QString issue;
        if (entry && entry->isNative()) {
            try {
                brush::BrushDocument doc;
                const QString path = BrushLibraryStore::nativeFilePath(m_persistencePath, entry->nativeAssetKey);
                if (BrushLibraryStore::readNativeFile(path, entry->nativeAssetKey, &doc, nullptr, &issue)) {
                    for (auto it = entry->nativeOverrides.cbegin(); it != entry->nativeOverrides.cend(); ++it) {
                        const auto* info = brush::findParameter(it.key().toStdString());
                        if (info && info->scope == brush::ParameterScope::Brush &&
                            drawing::digitalSupportsParameter(info->name) &&
                            brush::parameterInRange(*info, it.value())) {
                            doc.setParameter(info->name, it.value());
                        }
                    }
                    issue = QString::fromStdString(drawing::nativeBrushIssue(doc));
                    if (issue.isEmpty()) native = std::make_shared<const brush::BrushDocument>(std::move(doc));
                }
            } catch (const std::bad_alloc&) {
                issue = tr("Not enough memory to select this native brush.");
            } catch (const std::length_error&) {
                issue = tr("The native brush is too large to load on this system.");
            }
        }
        m_applying = true;
        const bool realisticNative = native && native->engine == brush::Engine::Realistic;
        m_brush->setNativeBrush(std::move(native), issue);
        if (issue.isEmpty() || realisticNative)
            m_brush->setBrushEngine(realisticNative ? QStringLiteral("realistic") : QStringLiteral("digital"));
        m_applying = false;
    }
    if (entry) applyValuesToSettings(entry->current, m_tool == QStringLiteral("eraser"));
}

void SubToolLibrary::capture()
{
    auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    if (!tab || idx < 0) return;
    auto& entry = tab->brushes[idx];
    if (entry.isNative()) return;
    const bool trad = isBrushTool()
        ? (m_brush && m_brush->isRealisticEngine())
        : (m_brush ? m_brush->traditionalMedia() : entry.current.traditionalMedia);
    const QString execHint = entry.current.executableTool.isEmpty() ? m_tool : entry.current.executableTool;
    entry.current = captureFromSettings(trad, execHint, entry.current.erase);
}

bool SubToolLibrary::persist()
{
    if (m_persistenceLoadFailed) return false;
    if (m_persistencePath.isEmpty()) return true;
    QString err;
    if (!BrushLibraryStore::save(m_persistencePath, m_collections, &err)) {
        setLastError(err);
        return false;
    }
    setLastError({});
    return true;
}

void SubToolLibrary::setLastError(const QString& message)
{
    if (m_lastError == message) return;
    m_lastError = message;
    emit lastErrorChanged();
}

QVariantList SubToolLibrary::subTools() const
{
    QVariantList list;
    const auto* tab = currentTabPtr();
    if (!tab) return list;

    for (int index = 0; index < tab->brushes.size(); ++index) {
        const auto& entry = tab->brushes[index];
        const auto& cur = entry.current;
        const auto& def = entry.defaults;
        if (!m_categoryFilter.isEmpty() && entry.category != m_categoryFilter) continue;
        if (!m_searchText.isEmpty() && !entry.name.contains(m_searchText, Qt::CaseInsensitive)
            && !entry.category.contains(m_searchText, Qt::CaseInsensitive)) continue;

        QVariantMap item;
        item.insert(QStringLiteral("id"), entry.id);
        item.insert(QStringLiteral("index"), index);
        item.insert(QStringLiteral("name"), entry.name);
        item.insert(QStringLiteral("category"), entry.category);
        item.insert(QStringLiteral("builtIn"), entry.builtIn);
        item.insert(QStringLiteral("nativeBrush"), entry.isNative());
        item.insert(QStringLiteral("executableTool"), cur.executableTool.isEmpty() ? m_tool : cur.executableTool);
        item.insert(QStringLiteral("erase"), cur.erase);
        item.insert(QStringLiteral("size"), cur.size);
        item.insert(QStringLiteral("opacity"), cur.opacity);
        item.insert(QStringLiteral("flow"), cur.flow);
        item.insert(QStringLiteral("spacing"), cur.spacing);
        item.insert(QStringLiteral("textured"), cur.textured);
        item.insert(QStringLiteral("tipStyle"), cur.tipStyle);
        item.insert(QStringLiteral("aspectRatio"), cur.aspectRatio);
        item.insert(QStringLiteral("tipAngle"), cur.tipAngle);
        item.insert(QStringLiteral("followDirection"), cur.followDirection);
        item.insert(QStringLiteral("pressureSize"), cur.pressureSize);
        item.insert(QStringLiteral("pressureOpacity"), cur.pressureOpacity);
        item.insert(QStringLiteral("blendStrength"), cur.blendStrength);
        item.insert(QStringLiteral("hardness"), cur.hardness);
        item.insert(QStringLiteral("stabilization"), cur.stabilization);
        item.insert(QStringLiteral("traditionalMedia"), cur.traditionalMedia);
        item.insert(QStringLiteral("realisticOnly"), def.traditionalMedia);
        item.insert(QStringLiteral("mediumId"), cur.mediumId);
        item.insert(QStringLiteral("toolParams"), cur.toolParams);
        item.insert(QStringLiteral("modified"), entry.isNative() ? !entry.nativeOverrides.isEmpty()
                                                                 : !(cur == def));
        item.insert(QStringLiteral("nativeModified"), entry.isNative() && !entry.nativeOverrides.isEmpty());
        list.push_back(item);
    }
    return list;
}

int SubToolLibrary::currentIndex() const
{
    return currentBrushIndexInTab(currentTabPtr());
}

QString SubToolLibrary::currentBrushId() const
{
    const auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    return (tab && idx >= 0) ? tab->brushes[idx].id : QString();
}

QString SubToolLibrary::currentName() const
{
    const auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    return (tab && idx >= 0) ? tab->brushes[idx].name : QString();
}

bool SubToolLibrary::currentBrushBuiltIn() const
{
    const auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    return (tab && idx >= 0) ? tab->brushes[idx].builtIn : false;
}

bool SubToolLibrary::currentRequiresRealistic() const
{
    if (isBrushTool()) return activeEngine() == QStringLiteral("realistic");
    const auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    return (tab && idx >= 0) ? tab->brushes[idx].defaults.traditionalMedia : false;
}

bool SubToolLibrary::isModified() const
{
    const auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    if (!tab || idx < 0) return false;
    const auto& e = tab->brushes[idx];
    return !(e.current == e.defaults);
}

} // namespace komapaint
