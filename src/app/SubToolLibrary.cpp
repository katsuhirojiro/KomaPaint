#include "SubToolLibrary.h"
#include "BrushSettings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QResource>
#include <QVariantMap>

#include <cmath>

// This catalog is compiled into the application library, not maintained as a
// second set of presentation-only presets in QML or C++.
static void initializeBrushPresetResources()
{
    Q_INIT_RESOURCE(brush_presets);
}

namespace komapaint {
namespace {

bool readInteger(const QJsonObject& object, const char* key, int minimum, int maximum, int* result)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < minimum || number > maximum)
        return false;
    *result = static_cast<int>(number);
    return true;
}

bool readOptionalInteger(const QJsonObject& object, const char* key, int minimum, int maximum, int* result)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined()) return true;
    return readInteger(object, key, minimum, maximum, result);
}

bool readOptionalBool(const QJsonObject& object, const char* key, bool* result)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined()) return true;
    if (!value.isBool()) return false;
    *result = value.toBool();
    return true;
}

bool parseValues(const QJsonObject& object, SubToolLibrary::Values* values)
{
    if (!values) return false;
    auto& v = *values;
    if (!readInteger(object, "size", BrushSettings::MinSize, BrushSettings::MaxSize, &v.size)
        || !readInteger(object, "opacity", 0, 100, &v.opacity)
        || !readInteger(object, "flow", 0, 100, &v.flow)
        || !readInteger(object, "spacing", BrushSettings::MinSpacing, BrushSettings::MaxSpacing, &v.spacing)
        || !readInteger(object, "hardness", 0, 100, &v.hardness)
        || !readInteger(object, "stabilization", 0, 100, &v.stabilization)
        || !object.value(QStringLiteral("textured")).isBool()) return false;
    v.textured = object.value(QStringLiteral("textured")).toBool();
    if (!readOptionalBool(object, "traditionalMedia", &v.traditionalMedia)
        || !readOptionalBool(object, "persistentReservoir", &v.persistentReservoir)
        || !readOptionalInteger(object, "paintLoad", 10, 100, &v.paintLoad)
        || !readOptionalInteger(object, "pigmentConcentration", 10, 100, &v.pigmentConcentration)
        || !readOptionalInteger(object, "waterAmount", 0, 100, &v.waterAmount)
        || !readOptionalInteger(object, "paintBody", 0, 100, &v.paintBody)
        || !readOptionalInteger(object, "pickup", 0, 100, &v.pickup)
        || !readOptionalInteger(object, "aspectRatio", 100, 400, &v.aspectRatio)
        || !readOptionalInteger(object, "tipAngle", -180, 180, &v.tipAngle)
        || !readOptionalBool(object, "followDirection", &v.followDirection)
        || !readOptionalInteger(object, "depletion", 0, 200, &v.depletion)
        || !readOptionalInteger(object, "edgeAccumulation", 0, 100, &v.edgeAccumulation)
        || !readOptionalInteger(object, "toothResponse", 0, 100, &v.toothResponse)
        || !readOptionalInteger(object, "granulation", 0, 100, &v.granulation)
        || !readOptionalInteger(object, "dryingSpeed", 25, 200, &v.dryingSpeed)
        || !readOptionalInteger(object, "pressureToAmount", 0, 100, &v.pressureToAmount)
        || !readOptionalInteger(object, "pressureToContact", 0, 100, &v.pressureToContact))
        return false;
    const QJsonValue presetId = object.value(QStringLiteral("materialPresetId"));
    if (!presetId.isUndefined()) {
        if (!presetId.isString() || presetId.toString().trimmed().isEmpty()) return false;
        v.materialPresetId = presetId.toString().trimmed();
    }
    const QJsonValue mediumId = object.value(QStringLiteral("mediumId"));
    if (!mediumId.isUndefined()) {
        if (!mediumId.isString()) return false;
        const QString m = mediumId.toString().trimmed();
        if (m != QStringLiteral("gouache") && m != QStringLiteral("watercolour")) return false;
        v.mediumId = m;
    }
    return true;
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
                        || !parseValues(preset, &values)) {
                        m_loadError = QStringLiteral("The bundled brush preset catalog contains an invalid preset.");
                        m_groups.clear();
                        break;
                    }
                    group.entries.push_back({name, category, values, values});
                }
                if (!m_loadError.isEmpty()) break;
                m_groups.push_back(std::move(group));
            }
        }
    }
    if (!m_loadError.isEmpty()) qWarning("[KomaPaint] %s", qUtf8Printable(m_loadError));

    connect(m_brush, &BrushSettings::changed, this, [this] {
        if (m_applying || m_group < 0) return;
        capture();
        emit subToolsChanged();
    });
}

int SubToolLibrary::groupIndex(const QString& tool) const
{
    for (std::size_t i = 0; i < m_groups.size(); ++i)
        if (m_groups[i].tool == tool) return static_cast<int>(i);
    return -1;
}

bool SubToolLibrary::paints(const QString& tool) const { return groupIndex(tool) >= 0; }

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
    // A tool that does not paint leaves the current engine settings alone.
    if (m_group >= 0) apply();
    emit toolChanged();
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
    if (m_group < 0) return result;
    for (const auto& entry : m_groups[static_cast<std::size_t>(m_group)].entries)
        if (!result.contains(entry.category)) result.append(entry.category);
    return result;
}

void SubToolLibrary::select(int index)
{
    if (m_group < 0) return;
    auto& group = m_groups[static_cast<std::size_t>(m_group)];
    if (index < 0 || index >= static_cast<int>(group.entries.size()) || index == group.current) return;
    group.current = index;
    apply();
    emit subToolsChanged();
    emit currentChanged();
}

void SubToolLibrary::resetCurrent()
{
    if (m_group < 0) return;
    auto& group = m_groups[static_cast<std::size_t>(m_group)];
    auto& entry = group.entries[static_cast<std::size_t>(group.current)];
    if (entry.current == entry.defaults) return;
    entry.current = entry.defaults;
    apply();
    emit subToolsChanged();
}

void SubToolLibrary::apply()
{
    if (m_group < 0) return;
    const auto& group = m_groups[static_cast<std::size_t>(m_group)];
    const Values values = group.entries[static_cast<std::size_t>(group.current)].current;
    m_applying = true;
    m_brush->setSize(values.size);
    m_brush->setOpacity(values.opacity);
    m_brush->setFlow(values.flow);
    m_brush->setSpacing(values.spacing);
    m_brush->setHardness(values.hardness);
    m_brush->setStabilization(values.stabilization);
    m_brush->setTextured(values.textured);
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
    m_brush->setErase(group.erase);
    m_applying = false;
}

void SubToolLibrary::capture()
{
    if (m_group < 0) return;
    auto& group = m_groups[static_cast<std::size_t>(m_group)];
    auto& values = group.entries[static_cast<std::size_t>(group.current)].current;
    values.size = m_brush->size();
    values.opacity = m_brush->opacity();
    values.flow = m_brush->flow();
    values.spacing = m_brush->spacing();
    values.hardness = m_brush->hardness();
    values.stabilization = m_brush->stabilization();
    values.textured = m_brush->textured();
    values.traditionalMedia = m_brush->traditionalMedia();
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
}

QVariantList SubToolLibrary::subTools() const
{
    QVariantList list;
    if (m_group < 0) return list;
    const auto& entries = m_groups[static_cast<std::size_t>(m_group)].entries;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        if (!m_categoryFilter.isEmpty() && entry.category != m_categoryFilter) continue;
        if (!m_searchText.isEmpty() && !entry.name.contains(m_searchText, Qt::CaseInsensitive)
            && !entry.category.contains(m_searchText, Qt::CaseInsensitive)) continue;
        QVariantMap item;
        item.insert(QStringLiteral("index"), static_cast<int>(index));
        item.insert(QStringLiteral("name"), entry.name);
        item.insert(QStringLiteral("category"), entry.category);
        // These values are the exact settings applied to BrushSettings and
        // consumed by the rasterizer, not values invented by the panel.
        item.insert(QStringLiteral("size"), entry.current.size);
        item.insert(QStringLiteral("opacity"), entry.current.opacity);
        item.insert(QStringLiteral("flow"), entry.current.flow);
        item.insert(QStringLiteral("spacing"), entry.current.spacing);
        item.insert(QStringLiteral("textured"), entry.current.textured);
        item.insert(QStringLiteral("hardness"), entry.current.hardness);
        item.insert(QStringLiteral("stabilization"), entry.current.stabilization);
        item.insert(QStringLiteral("modified"), !(entry.current == entry.defaults));
        list.push_back(item);
    }
    return list;
}

int SubToolLibrary::currentIndex() const
{
    return m_group < 0 ? -1 : m_groups[static_cast<std::size_t>(m_group)].current;
}

QString SubToolLibrary::currentName() const
{
    if (m_group < 0) return {};
    const auto& group = m_groups[static_cast<std::size_t>(m_group)];
    return group.entries[static_cast<std::size_t>(group.current)].name;
}

bool SubToolLibrary::isModified() const
{
    if (m_group < 0) return false;
    const auto& group = m_groups[static_cast<std::size_t>(m_group)];
    const auto& entry = group.entries[static_cast<std::size_t>(group.current)];
    return !(entry.current == entry.defaults);
}

} // namespace komapaint
