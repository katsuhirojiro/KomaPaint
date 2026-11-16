#include "SubToolLibrary.h"
#include "BrushSettings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QResource>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>

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

} // namespace

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
            for (const QJsonValue& groupValue : groups) {
                const QJsonObject object = groupValue.toObject();
                const QString tool = object.value(QStringLiteral("tool")).toString();
                const QJsonArray presets = object.value(QStringLiteral("presets")).toArray();
                if (tool.isEmpty() || !object.value(QStringLiteral("erase")).isBool() || presets.isEmpty()
                    || groupIndex(tool) >= 0) {
                    m_loadError = QStringLiteral("The bundled brush preset catalog contains an invalid tool group.");
                    m_groups.clear();
                    break;
                }
                Group group;
                group.tool = tool;
                group.erase = object.value(QStringLiteral("erase")).toBool();
                for (const QJsonValue& presetValue : presets) {
                    const QJsonObject preset = presetValue.toObject();
                    const QString name = preset.value(QStringLiteral("name")).toString().trimmed();
                    const QString category = preset.value(QStringLiteral("category")).toString().trimmed();
                    Values values;
                    if (!presetValue.isObject() || name.isEmpty() || category.isEmpty()
                        || !BrushLibraryStore::parseValues(preset, &values)) {
                        m_loadError = QStringLiteral("The bundled brush preset catalog contains an invalid preset.");
                        m_groups.clear();
                        break;
                    }
                    group.entries.push_back({name, category, values, values});
                }
                if (!m_loadError.isEmpty()) break;
                m_groups.push_back(std::move(group));
            }
            if (m_loadError.isEmpty()) {
                QString colErr;
                if (!BrushLibraryStore::loadBundledCollections(root, &m_bundledCollections, &colErr)) {
                    m_loadError = colErr;
                } else {
                    m_collections = m_bundledCollections;
                }
            }
        }
    }
    if (!m_loadError.isEmpty()) qWarning("[KomaPaint] %s", qUtf8Printable(m_loadError));

    auto onBrushMutated = [this] {
        if (m_applying) return;
        if (!isPaintTool()) return;
        capture();
        if (isBrushTool()) persist();
        emit subToolsChanged();
    };
    connect(m_brush, &BrushSettings::changed, this, onBrushMutated);
    connect(m_brush, &BrushSettings::materialMixtureChanged, this, onBrushMutated);

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

bool SubToolLibrary::initializePersistence(const QString& filePath)
{
    m_persistencePath = filePath;
    if (filePath.isEmpty()) return false;

    const bool existed = QFile::exists(filePath);
    BrushLibraryDocument loaded;
    QString err;
    if (!BrushLibraryStore::load(filePath, m_bundledCollections, &loaded, &err)) {
        setLastError(err);
        m_collections = m_bundledCollections;
    } else {
        m_collections = std::move(loaded);
        setLastError({});
    }
    if (!existed) persist();
    if (isBrushTool()) apply();
    emit tabsChanged();
    emit subToolsChanged();
    emit currentChanged();
    return err.isEmpty();
}

int SubToolLibrary::groupIndex(const QString& tool) const
{
    for (std::size_t i = 0; i < m_groups.size(); ++i)
        if (m_groups[i].tool == tool) return static_cast<int>(i);
    return -1;
}

EngineCollectionData& SubToolLibrary::activeCollection()
{
    return (m_brush && m_brush->isRealisticEngine()) ? m_collections.realistic : m_collections.digital;
}

const EngineCollectionData& SubToolLibrary::activeCollection() const
{
    return (m_brush && m_brush->isRealisticEngine()) ? m_collections.realistic : m_collections.digital;
}

int SubToolLibrary::findTabIndex(const EngineCollectionData& col, const QString& tabId) const
{
    for (int i = 0; i < col.tabs.size(); ++i)
        if (col.tabs[i].id == tabId) return i;
    return -1;
}

BrushTabData* SubToolLibrary::currentTabPtr()
{
    if (!isBrushTool()) return nullptr;
    auto& col = activeCollection();
    int idx = findTabIndex(col, col.currentTabId);
    if (idx < 0 && !col.tabs.isEmpty()) {
        idx = 0;
        col.currentTabId = col.tabs[0].id;
    }
    return idx >= 0 ? &col.tabs[idx] : nullptr;
}

const BrushTabData* SubToolLibrary::currentTabPtr() const
{
    if (!isBrushTool()) return nullptr;
    const auto& col = activeCollection();
    int idx = findTabIndex(col, col.currentTabId);
    if (idx < 0 && !col.tabs.isEmpty()) idx = 0;
    return idx >= 0 ? &col.tabs[idx] : nullptr;
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
    return tool == QStringLiteral("brush") || groupIndex(tool) >= 0;
}

QStringList SubToolLibrary::paintTools() const
{
    QStringList list;
    for (const auto& group : m_groups) list << group.tool;
    return list;
}

void SubToolLibrary::setTool(const QString& tool)
{
    if (tool == m_tool) return;
    m_tool = tool;
    m_group = groupIndex(tool);
    if (!m_categoryFilter.isEmpty() && !categories().contains(m_categoryFilter)) {
        m_categoryFilter.clear();
        emit filterChanged();
    }
    if (isPaintTool()) apply();
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
    if (isBrushTool()) {
        const auto* tab = currentTabPtr();
        if (!tab) return result;
        for (const auto& b : tab->brushes)
            if (!result.contains(b.category)) result.append(b.category);
        return result;
    }
    if (m_group < 0) return result;
    for (const auto& entry : m_groups[static_cast<std::size_t>(m_group)].entries)
        if (!result.contains(entry.category)) result.append(entry.category);
    return result;
}

QVariantList SubToolLibrary::engineTabs() const
{
    QVariantList list;
    const auto& col = activeCollection();
    for (int i = 0; i < col.tabs.size(); ++i) {
        const auto& tab = col.tabs[i];
        QVariantMap item;
        item.insert(QStringLiteral("index"), i);
        item.insert(QStringLiteral("id"), tab.id);
        item.insert(QStringLiteral("name"), tab.name);
        item.insert(QStringLiteral("isDefault"), tab.id == col.defaultTabId);
        item.insert(QStringLiteral("brushCount"), tab.brushes.size());
        list.push_back(item);
    }
    return list;
}

int SubToolLibrary::currentTabIndex() const
{
    const auto& col = activeCollection();
    const int idx = findTabIndex(col, col.currentTabId);
    return idx >= 0 ? idx : (col.tabs.isEmpty() ? -1 : 0);
}

QString SubToolLibrary::currentTabId() const
{
    const auto& col = activeCollection();
    const int idx = currentTabIndex();
    return idx >= 0 ? col.tabs[idx].id : QString();
}

QString SubToolLibrary::currentTabName() const
{
    const auto& col = activeCollection();
    const int idx = currentTabIndex();
    return idx >= 0 ? col.tabs[idx].name : QString();
}

QString SubToolLibrary::defaultTabId() const
{
    return activeCollection().defaultTabId;
}

void SubToolLibrary::selectTab(int index)
{
    auto& col = activeCollection();
    if (index < 0 || index >= col.tabs.size()) return;
    if (col.currentTabId == col.tabs[index].id) return;
    col.currentTabId = col.tabs[index].id;
    if (!m_categoryFilter.isEmpty()) {
        m_categoryFilter.clear();
        emit filterChanged();
    }
    if (isBrushTool()) apply();
    emit tabsChanged();
    emit subToolsChanged();
    emit currentChanged();
}

void SubToolLibrary::selectTabById(const QString& tabId)
{
    const int idx = findTabIndex(activeCollection(), tabId);
    if (idx >= 0) selectTab(idx);
}

QString SubToolLibrary::createTab(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setLastError(QStringLiteral("Tab name cannot be empty."));
        return {};
    }
    auto& col = activeCollection();
    BrushTabData tab;
    tab.id = makeId(QStringLiteral("tab"));
    tab.name = trimmed;
    col.tabs.append(tab);
    col.currentTabId = tab.id;
    persist();
    if (isBrushTool()) apply();
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
    auto& col = activeCollection();
    const int idx = findTabIndex(col, tabId);
    if (idx < 0) return false;
    if (col.tabs[idx].name == trimmed) return true;
    col.tabs[idx].name = trimmed;
    persist();
    emit tabsChanged();
    return true;
}

bool SubToolLibrary::moveTab(int fromIndex, int toIndex)
{
    auto& col = activeCollection();
    if (fromIndex < 0 || fromIndex >= col.tabs.size()
        || toIndex < 0 || toIndex >= col.tabs.size()
        || fromIndex == toIndex) {
        return false;
    }
    col.tabs.move(fromIndex, toIndex);
    persist();
    emit tabsChanged();
    return true;
}

bool SubToolLibrary::moveTabById(const QString& tabId, int delta)
{
    const int idx = findTabIndex(activeCollection(), tabId);
    if (idx < 0) return false;
    return moveTab(idx, idx + delta);
}

bool SubToolLibrary::setDefaultTab(const QString& tabId)
{
    auto& col = activeCollection();
    if (findTabIndex(col, tabId) < 0) return false;
    if (col.defaultTabId == tabId) return true;
    col.defaultTabId = tabId;
    persist();
    emit tabsChanged();
    return true;
}

bool SubToolLibrary::deleteTab(const QString& tabId)
{
    auto& col = activeCollection();
    const int idx = findTabIndex(col, tabId);
    if (idx < 0) return false;
    if (col.tabs.size() <= 1) {
        setLastError(QStringLiteral("At least one collection tab is required."));
        return false;
    }
    const bool wasCurrent = (col.currentTabId == tabId);
    const bool wasDefault = (col.defaultTabId == tabId);
    col.tabs.removeAt(idx);
    const int fallbackIdx = qBound(0, idx, col.tabs.size() - 1);
    if (wasDefault) col.defaultTabId = col.tabs[fallbackIdx].id;
    if (wasCurrent) col.currentTabId = col.tabs[fallbackIdx].id;
    persist();
    if (wasCurrent && isBrushTool()) apply();
    emit tabsChanged();
    emit subToolsChanged();
    emit currentChanged();
    return true;
}

void SubToolLibrary::select(int index)
{
    if (isBrushTool()) {
        auto* tab = currentTabPtr();
        if (!tab || index < 0 || index >= tab->brushes.size()) return;
        const QString nextId = tab->brushes[index].id;
        if (tab->currentBrushId == nextId) return;
        tab->currentBrushId = nextId;
        apply();
        emit subToolsChanged();
        emit currentChanged();
        return;
    }
    if (m_group < 0) return;
    auto& group = m_groups[static_cast<std::size_t>(m_group)];
    if (index < 0 || index >= static_cast<int>(group.entries.size()) || index == group.current) return;
    group.current = index;
    apply();
    emit subToolsChanged();
    emit currentChanged();
}

void SubToolLibrary::selectById(const QString& brushId)
{
    if (!isBrushTool()) return;
    auto* tab = currentTabPtr();
    if (!tab) return;
    for (int i = 0; i < tab->brushes.size(); ++i) {
        if (tab->brushes[i].id == brushId) {
            select(i);
            return;
        }
    }
}

void SubToolLibrary::resetCurrent()
{
    if (isBrushTool()) {
        const QString id = currentBrushId();
        if (!id.isEmpty()) resetBrush(id);
        return;
    }
    if (m_group < 0) return;
    auto& group = m_groups[static_cast<std::size_t>(m_group)];
    auto& entry = group.entries[static_cast<std::size_t>(group.current)];
    if (entry.current == entry.defaults) return;
    entry.current = entry.defaults;
    apply();
    emit subToolsChanged();
}

void SubToolLibrary::resetBrush(const QString& brushId)
{
    auto& col = activeCollection();
    for (auto& tab : col.tabs) {
        for (int i = 0; i < tab.brushes.size(); ++i) {
            if (tab.brushes[i].id == brushId) {
                if (tab.brushes[i].current == tab.brushes[i].defaults) return;
                tab.brushes[i].current = tab.brushes[i].defaults;
                persist();
                if (isBrushTool() && tab.id == currentTabId() && tab.currentBrushId == brushId)
                    apply();
                emit subToolsChanged();
                return;
            }
        }
    }
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
    const bool isRealistic = (m_brush && m_brush->isRealisticEngine());
    const Values captured = captureFromBrush(isRealistic);
    const QString base = name.trimmed().isEmpty()
        ? (currentName().isEmpty() ? QStringLiteral("Custom Brush") : currentName() + QStringLiteral(" Copy"))
        : name.trimmed();
    BrushEntryData entry;
    entry.id = makeId(QStringLiteral("brush"));
    entry.name = uniqueBrushName(*tab, base);
    entry.category = tab->name;
    entry.builtIn = false;
    entry.defaults = captured;
    entry.current = captured;
    tab->brushes.append(entry);
    tab->currentBrushId = entry.id;
    persist();
    if (isBrushTool()) apply();
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
        copy.id = makeId(QStringLiteral("brush"));
        copy.builtIn = false;
        copy.name = uniqueBrushName(*tab, copy.name + QStringLiteral(" Copy"));
        copy.defaults = copy.current;
        tab->brushes.insert(i + 1, copy);
        tab->currentBrushId = copy.id;
        persist();
        if (isBrushTool()) apply();
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
        setLastError(QStringLiteral("Brush name cannot be empty."));
        return false;
    }
    auto& col = activeCollection();
    for (auto& tab : col.tabs) {
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
    auto& col = activeCollection();
    for (auto& tab : col.tabs) {
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
            if (wasCurrent && isBrushTool()) apply();
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
    auto& col = activeCollection();
    const int dstIdx = findTabIndex(col, targetTabId);
    if (dstIdx < 0) return false;
    for (int srcIdx = 0; srcIdx < col.tabs.size(); ++srcIdx) {
        if (srcIdx == dstIdx) continue;
        auto& src = col.tabs[srcIdx];
        for (int i = 0; i < src.brushes.size(); ++i) {
            if (src.brushes[i].id != brushId) continue;
            BrushEntryData moved = src.brushes.takeAt(i);
            if (src.brushes.isEmpty()) {
                src.currentBrushId.clear();
            } else if (src.currentBrushId == brushId) {
                src.currentBrushId = src.brushes[qBound(0, i, src.brushes.size() - 1)].id;
            }
            auto& dst = col.tabs[dstIdx];
            moved.name = uniqueBrushName(dst, moved.name);
            dst.brushes.append(moved);
            if (dst.currentBrushId.isEmpty()) dst.currentBrushId = moved.id;
            persist();
            if (isBrushTool() && src.id == currentTabId()) apply();
            emit tabsChanged();
            emit subToolsChanged();
            emit currentChanged();
            return true;
        }
    }
    return false;
}

QString SubToolLibrary::copyBrushToTab(const QString& brushId, const QString& targetTabId)
{
    auto& col = activeCollection();
    const int dstIdx = findTabIndex(col, targetTabId);
    if (dstIdx < 0) return {};
    for (const auto& src : col.tabs) {
        for (const auto& b : src.brushes) {
            if (b.id != brushId) continue;
            auto& dst = col.tabs[dstIdx];
            BrushEntryData copy = b;
            copy.id = makeId(QStringLiteral("brush"));
            copy.builtIn = false;
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

void SubToolLibrary::applyValuesToBrush(const Values& values, bool erase)
{
    m_applying = true;
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
    m_brush->setPressureGamma(values.pressureGamma);
    m_brush->setMinSizeRatio(values.minSizeRatio);
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
    m_brush->setErase(erase);
    m_applying = false;
}

SubToolLibrary::Values SubToolLibrary::captureFromBrush(bool traditionalMedia) const
{
    Values values;
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
    values.pressureGamma = m_brush->pressureGamma();
    values.minSizeRatio = m_brush->minSizeRatio();
    values.blendStrength = m_brush->blendStrength();
    values.colorMix = m_brush->colorMix();
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
    return values;
}

void SubToolLibrary::apply()
{
    if (isBrushTool()) {
        auto* tab = currentTabPtr();
        if (!tab || tab->brushes.isEmpty()) return;
        const int idx = currentBrushIndexInTab(tab);
        if (idx < 0) return;
        tab->currentBrushId = tab->brushes[idx].id;
        applyValuesToBrush(tab->brushes[idx].current, false);
        return;
    }
    if (m_group < 0) return;
    const auto& group = m_groups[static_cast<std::size_t>(m_group)];
    applyValuesToBrush(group.entries[static_cast<std::size_t>(group.current)].current, group.erase);
}

void SubToolLibrary::capture()
{
    if (isBrushTool()) {
        auto* tab = currentTabPtr();
        const int idx = currentBrushIndexInTab(tab);
        if (!tab || idx < 0) return;
        tab->brushes[idx].current = captureFromBrush(m_brush && m_brush->isRealisticEngine());
        return;
    }
    if (m_group < 0) return;
    auto& group = m_groups[static_cast<std::size_t>(m_group)];
    group.entries[static_cast<std::size_t>(group.current)].current =
        captureFromBrush(m_brush->traditionalMedia());
}

bool SubToolLibrary::persist()
{
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
    auto appendItem = [&](const QString& id, int index, const QString& name,
                          const QString& category, bool builtIn,
                          const Values& cur, const Values& def) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("index"), index);
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("category"), category);
        item.insert(QStringLiteral("builtIn"), builtIn);
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
        item.insert(QStringLiteral("modified"), !(cur == def));
        list.push_back(item);
    };

    if (isBrushTool()) {
        const auto* tab = currentTabPtr();
        if (!tab) return list;
        for (int index = 0; index < tab->brushes.size(); ++index) {
            const auto& entry = tab->brushes[index];
            if (!m_categoryFilter.isEmpty() && entry.category != m_categoryFilter) continue;
            if (!m_searchText.isEmpty() && !entry.name.contains(m_searchText, Qt::CaseInsensitive)
                && !entry.category.contains(m_searchText, Qt::CaseInsensitive)) continue;
            appendItem(entry.id, index, entry.name, entry.category, entry.builtIn, entry.current, entry.defaults);
        }
        return list;
    }

    if (m_group < 0) return list;
    const auto& entries = m_groups[static_cast<std::size_t>(m_group)].entries;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        if (!m_categoryFilter.isEmpty() && entry.category != m_categoryFilter) continue;
        if (!m_searchText.isEmpty() && !entry.name.contains(m_searchText, Qt::CaseInsensitive)
            && !entry.category.contains(m_searchText, Qt::CaseInsensitive)) continue;
        appendItem(QString(), static_cast<int>(index), entry.name, entry.category, true,
                   entry.current, entry.defaults);
    }
    return list;
}

int SubToolLibrary::currentIndex() const
{
    if (isBrushTool()) return currentBrushIndexInTab(currentTabPtr());
    return m_group < 0 ? -1 : m_groups[static_cast<std::size_t>(m_group)].current;
}

QString SubToolLibrary::currentBrushId() const
{
    if (!isBrushTool()) return {};
    const auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    return (tab && idx >= 0) ? tab->brushes[idx].id : QString();
}

QString SubToolLibrary::currentName() const
{
    if (isBrushTool()) {
        const auto* tab = currentTabPtr();
        const int idx = currentBrushIndexInTab(tab);
        return (tab && idx >= 0) ? tab->brushes[idx].name : QString();
    }
    if (m_group < 0) return {};
    const auto& group = m_groups[static_cast<std::size_t>(m_group)];
    return group.entries[static_cast<std::size_t>(group.current)].name;
}

bool SubToolLibrary::currentBrushBuiltIn() const
{
    if (!isBrushTool()) return true;
    const auto* tab = currentTabPtr();
    const int idx = currentBrushIndexInTab(tab);
    return (tab && idx >= 0) ? tab->brushes[idx].builtIn : false;
}

bool SubToolLibrary::currentRequiresRealistic() const
{
    if (isBrushTool()) return activeEngine() == QStringLiteral("realistic");
    if (m_group < 0) return false;
    const auto& group = m_groups[static_cast<std::size_t>(m_group)];
    return group.entries[static_cast<std::size_t>(group.current)].defaults.traditionalMedia;
}

bool SubToolLibrary::isModified() const
{
    if (isBrushTool()) {
        const auto* tab = currentTabPtr();
        const int idx = currentBrushIndexInTab(tab);
        if (!tab || idx < 0) return false;
        const auto& e = tab->brushes[idx];
        return !(e.current == e.defaults);
    }
    if (m_group < 0) return false;
    const auto& group = m_groups[static_cast<std::size_t>(m_group)];
    const auto& entry = group.entries[static_cast<std::size_t>(group.current)];
    return !(entry.current == entry.defaults);
}

} // namespace komapaint
