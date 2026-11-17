#include "BrushLibraryStore.h"
#include "BrushSettings.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

#include <cmath>

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

namespace komapaint {
namespace {

const QString kFormatName = QStringLiteral("komapaint-brush-library");

QByteArray digestOf(const QJsonObject& root)
{
    return QCryptographicHash::hash(QJsonDocument(root).toJson(QJsonDocument::Compact),
                                    QCryptographicHash::Sha256).toHex();
}

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

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

QJsonObject entryToJson(const BrushEntryData& entry)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), entry.id);
    obj.insert(QStringLiteral("name"), entry.name);
    obj.insert(QStringLiteral("category"), entry.category);
    obj.insert(QStringLiteral("builtIn"), entry.builtIn);
    obj.insert(QStringLiteral("defaults"), BrushLibraryStore::valuesToJson(entry.defaults));
    obj.insert(QStringLiteral("current"), BrushLibraryStore::valuesToJson(entry.current));
    return obj;
}

bool entryFromJson(const QJsonObject& obj, BrushEntryData* out)
{
    if (!out) return false;
    const QString id = obj.value(QStringLiteral("id")).toString().trimmed();
    const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
    const QString category = obj.value(QStringLiteral("category")).toString().trimmed();
    if (id.isEmpty() || name.isEmpty()) return false;
    const auto defVal = obj.value(QStringLiteral("defaults"));
    const auto curVal = obj.value(QStringLiteral("current"));
    if (!defVal.isObject() || !curVal.isObject()) return false;
    BrushPresetValues defaults, current;
    if (!BrushLibraryStore::parseValues(defVal.toObject(), &defaults)) return false;
    if (!BrushLibraryStore::parseValues(curVal.toObject(), &current)) return false;
    out->id = id;
    out->name = name;
    out->category = category.isEmpty() ? QStringLiteral("Custom") : category;
    out->builtIn = obj.value(QStringLiteral("builtIn")).toBool(false);
    out->defaults = std::move(defaults);
    out->current = std::move(current);
    return true;
}

QJsonObject collectionToJson(const EngineCollectionData& col)
{
    QJsonArray tabs;
    for (const auto& tab : col.tabs) {
        QJsonObject tObj;
        tObj.insert(QStringLiteral("id"), tab.id);
        tObj.insert(QStringLiteral("name"), tab.name);
        tObj.insert(QStringLiteral("currentBrushId"), tab.currentBrushId);
        QJsonArray brushes;
        for (const auto& b : tab.brushes) brushes.append(entryToJson(b));
        tObj.insert(QStringLiteral("brushes"), brushes);
        tabs.append(tObj);
    }
    QJsonObject out;
    out.insert(QStringLiteral("defaultTabId"), col.defaultTabId);
    out.insert(QStringLiteral("tabs"), tabs);
    return out;
}

bool collectionFromJson(const QJsonObject& obj, int forcedTraditionalMedia,
                        EngineCollectionData* out)
{
    if (!out) return false;
    const auto tabsVal = obj.value(QStringLiteral("tabs"));
    if (!tabsVal.isArray()) return false;
    EngineCollectionData col;
    col.defaultTabId = obj.value(QStringLiteral("defaultTabId")).toString().trimmed();
    for (const auto& tVal : tabsVal.toArray()) {
        if (!tVal.isObject()) return false;
        const auto tObj = tVal.toObject();
        BrushTabData tab;
        tab.id = tObj.value(QStringLiteral("id")).toString().trimmed();
        tab.name = tObj.value(QStringLiteral("name")).toString().trimmed();
        tab.currentBrushId = tObj.value(QStringLiteral("currentBrushId")).toString().trimmed();
        if (tab.id.isEmpty() || tab.name.isEmpty()) return false;
        const auto bVal = tObj.value(QStringLiteral("brushes"));
        if (!bVal.isArray()) return false;
        for (const auto& itemVal : bVal.toArray()) {
            if (!itemVal.isObject()) return false;
            BrushEntryData entry;
            if (!entryFromJson(itemVal.toObject(), &entry)) return false;
            if (forcedTraditionalMedia >= 0) {
                entry.defaults.traditionalMedia = (forcedTraditionalMedia != 0);
                entry.current.traditionalMedia = (forcedTraditionalMedia != 0);
            }
            tab.brushes.append(std::move(entry));
        }
        if (tab.currentBrushId.isEmpty() && !tab.brushes.isEmpty())
            tab.currentBrushId = tab.brushes.first().id;
        col.tabs.append(std::move(tab));
    }
    if (col.tabs.isEmpty()) return false;
    bool hasDefault = false;
    for (const auto& t : col.tabs) {
        if (t.id == col.defaultTabId) { hasDefault = true; break; }
    }
    if (!hasDefault) col.defaultTabId = col.tabs.first().id;
    col.currentTabId = col.defaultTabId;
    *out = std::move(col);
    return true;
}

bool bundledCollectionFromJson(const QJsonObject& obj, int forcedTraditionalMedia,
                               EngineCollectionData* out, QStringList* builtInIds,
                               const QString& defaultExecutableTool = QString())
{
    if (!out) return false;
    const auto tabsVal = obj.value(QStringLiteral("tabs"));
    if (!tabsVal.isArray()) return false;
    EngineCollectionData col;
    col.defaultTabId = obj.value(QStringLiteral("defaultTabId")).toString().trimmed();
    for (const auto& tVal : tabsVal.toArray()) {
        if (!tVal.isObject()) return false;
        const auto tObj = tVal.toObject();
        BrushTabData tab;
        tab.id = tObj.value(QStringLiteral("id")).toString().trimmed();
        tab.name = tObj.value(QStringLiteral("name")).toString().trimmed();
        if (tab.id.isEmpty() || tab.name.isEmpty()) return false;
        const auto presetsVal = tObj.value(QStringLiteral("presets"));
        if (!presetsVal.isArray()) return false;
        for (const auto& pVal : presetsVal.toArray()) {
            if (!pVal.isObject()) return false;
            const auto pObj = pVal.toObject();
            const QString id = pObj.value(QStringLiteral("id")).toString().trimmed();
            const QString name = pObj.value(QStringLiteral("name")).toString().trimmed();
            const QString category = pObj.value(QStringLiteral("category")).toString().trimmed();
            if (name.isEmpty()) return false;
            BrushPresetValues values;
            if (!BrushLibraryStore::parseValues(pObj, &values)) return false;
            if (forcedTraditionalMedia >= 0) values.traditionalMedia = (forcedTraditionalMedia != 0);
            if (values.executableTool.isEmpty() && !defaultExecutableTool.isEmpty()) {
                values.executableTool = defaultExecutableTool;
            }
            BrushEntryData entry;
            entry.id = id.isEmpty()
                ? QStringLiteral("builtin-%1-%2").arg(tab.id, QString::number(tab.brushes.size()))
                : id;
            entry.name = name;
            entry.category = category.isEmpty() ? tab.name : category;
            entry.builtIn = true;
            entry.defaults = values;
            entry.current = values;
            if (builtInIds && !builtInIds->contains(entry.id))
                builtInIds->append(entry.id);
            tab.brushes.append(std::move(entry));
        }
        if (!tab.brushes.isEmpty()) tab.currentBrushId = tab.brushes.first().id;
        col.tabs.append(std::move(tab));
    }
    if (col.tabs.isEmpty()) return false;
    bool hasDefault = false;
    for (const auto& t : col.tabs) {
        if (t.id == col.defaultTabId) { hasDefault = true; break; }
    }
    if (!hasDefault) col.defaultTabId = col.tabs.first().id;
    col.currentTabId = col.defaultTabId;
    *out = std::move(col);
    return true;
}

void mergeNewBuiltIns(const EngineCollectionData& bundled,
                      const QStringList& knownBuiltInIds,
                      EngineCollectionData* target,
                      QStringList* updatedKnownIds)
{
    if (!target || target->tabs.isEmpty()) return;
    for (const auto& bTab : bundled.tabs) {
        for (const auto& bEntry : bTab.brushes) {
            if (knownBuiltInIds.contains(bEntry.id)) continue;
            bool alreadyPresent = false;
            for (const auto& t : target->tabs) {
                for (const auto& e : t.brushes) {
                    if (e.id == bEntry.id) { alreadyPresent = true; break; }
                }
                if (alreadyPresent) break;
            }
            if (!alreadyPresent) {
                int destIdx = -1;
                for (int i = 0; i < target->tabs.size(); ++i) {
                    if (target->tabs[i].id == bTab.id) { destIdx = i; break; }
                }
                if (destIdx < 0) {
                    for (int i = 0; i < target->tabs.size(); ++i) {
                        if (target->tabs[i].id == target->defaultTabId) { destIdx = i; break; }
                    }
                }
                if (destIdx < 0) destIdx = 0;
                target->tabs[destIdx].brushes.append(bEntry);
            }
            if (updatedKnownIds && !updatedKnownIds->contains(bEntry.id))
                updatedKnownIds->append(bEntry.id);
        }
    }
}

} // namespace

bool BrushLibraryStore::parseValues(const QJsonObject& object, BrushPresetValues* out)
{
    if (!out) return false;
    BrushPresetValues v;
    const QJsonValue execVal = object.value(QStringLiteral("executableTool"));
    if (!execVal.isUndefined()) {
        if (!execVal.isString()) return false;
        v.executableTool = execVal.toString().trimmed();
    }
    const bool brushRequired = v.executableTool.isEmpty() ||
        v.executableTool == QStringLiteral("pen") ||
        v.executableTool == QStringLiteral("pencil") ||
        v.executableTool == QStringLiteral("brush") ||
        v.executableTool == QStringLiteral("airbrush") ||
        v.executableTool == QStringLiteral("eraser") ||
        v.executableTool == QStringLiteral("boundary_eraser") ||
        v.executableTool == QStringLiteral("smudge");

    if (brushRequired) {
        if (!readInteger(object, "size", BrushSettings::MinSize, BrushSettings::MaxSize, &v.size)
            || !readInteger(object, "opacity", 0, 100, &v.opacity)
            || !readInteger(object, "flow", 0, 100, &v.flow)
            || !readInteger(object, "spacing", BrushSettings::MinSpacing, BrushSettings::MaxSpacing, &v.spacing)
            || !readInteger(object, "hardness", 0, 100, &v.hardness)
            || !readInteger(object, "stabilization", 0, 100, &v.stabilization)
            || !object.value(QStringLiteral("textured")).isBool()) {
            return false;
        }
        v.textured = object.value(QStringLiteral("textured")).toBool();
    } else {
        if (!readOptionalInteger(object, "size", BrushSettings::MinSize, BrushSettings::MaxSize, &v.size)
            || !readOptionalInteger(object, "opacity", 0, 100, &v.opacity)
            || !readOptionalInteger(object, "flow", 0, 100, &v.flow)
            || !readOptionalInteger(object, "spacing", BrushSettings::MinSpacing, BrushSettings::MaxSpacing, &v.spacing)
            || !readOptionalInteger(object, "hardness", 0, 100, &v.hardness)
            || !readOptionalInteger(object, "stabilization", 0, 100, &v.stabilization)
            || !readOptionalBool(object, "textured", &v.textured)) {
            return false;
        }
    }

    const QJsonValue tipVal = object.value(QStringLiteral("tipStyle"));
    if (tipVal.isString()) {
        v.tipStyle = BrushSettings::normalizeTipStyle(tipVal.toString());
    } else {
        v.tipStyle = v.textured ? QStringLiteral("textured") : QStringLiteral("round");
    }
    v.textured = v.textured || (v.tipStyle != QStringLiteral("round"));

    if (!readOptionalInteger(object, "pressureSize", 0, 100, &v.pressureSize)
        || !readOptionalInteger(object, "pressureOpacity", 0, 100, &v.pressureOpacity)
        || !readOptionalInteger(object, "pressureFlow", 0, 100, &v.pressureFlow)
        || !readOptionalInteger(object, "pressureGamma", 40, 200, &v.pressureGamma)
        || !readOptionalInteger(object, "minSizeRatio", 5, 100, &v.minSizeRatio)
        || !readOptionalInteger(object, "tiltSize", 0, 100, &v.tiltSize)
        || !readOptionalBool(object, "tiltAngle", &v.tiltAngle)
        || !readOptionalBool(object, "stylusRotation", &v.stylusRotation)
        || !readOptionalInteger(object, "blendStrength", 0, 100, &v.blendStrength)
        || !readOptionalInteger(object, "colorMix", 0, 100, &v.colorMix)
        || !readOptionalBool(object, "erase", &v.erase)
        || !readOptionalBool(object, "traditionalMedia", &v.traditionalMedia)
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
        || !readOptionalInteger(object, "pressureToContact", 0, 100, &v.pressureToContact)) {
        return false;
    }
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
    const QJsonValue mixVal = object.value(QStringLiteral("materialMixture"));
    if (mixVal.isObject()) {
        v.materialMixture = mixVal.toObject().toVariantMap();
    }
    const QJsonValue paramsVal = object.value(QStringLiteral("toolParams"));
    if (paramsVal.isObject()) {
        v.toolParams = paramsVal.toObject().toVariantMap();
    }
    *out = std::move(v);
    return true;
}

QJsonObject BrushLibraryStore::valuesToJson(const BrushPresetValues& v)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("size"), v.size);
    obj.insert(QStringLiteral("opacity"), v.opacity);
    obj.insert(QStringLiteral("flow"), v.flow);
    obj.insert(QStringLiteral("spacing"), v.spacing);
    obj.insert(QStringLiteral("hardness"), v.hardness);
    obj.insert(QStringLiteral("stabilization"), v.stabilization);
    obj.insert(QStringLiteral("textured"), v.textured);
    obj.insert(QStringLiteral("tipStyle"), v.tipStyle);
    obj.insert(QStringLiteral("pressureSize"), v.pressureSize);
    obj.insert(QStringLiteral("pressureOpacity"), v.pressureOpacity);
    obj.insert(QStringLiteral("pressureFlow"), v.pressureFlow);
    obj.insert(QStringLiteral("pressureGamma"), v.pressureGamma);
    obj.insert(QStringLiteral("minSizeRatio"), v.minSizeRatio);
    obj.insert(QStringLiteral("tiltSize"), v.tiltSize);
    obj.insert(QStringLiteral("tiltAngle"), v.tiltAngle);
    obj.insert(QStringLiteral("stylusRotation"), v.stylusRotation);
    obj.insert(QStringLiteral("blendStrength"), v.blendStrength);
    obj.insert(QStringLiteral("colorMix"), v.colorMix);
    obj.insert(QStringLiteral("erase"), v.erase);
    obj.insert(QStringLiteral("traditionalMedia"), v.traditionalMedia);
    obj.insert(QStringLiteral("persistentReservoir"), v.persistentReservoir);
    obj.insert(QStringLiteral("mediumId"), v.mediumId);
    obj.insert(QStringLiteral("materialPresetId"), v.materialPresetId);
    obj.insert(QStringLiteral("paintLoad"), v.paintLoad);
    obj.insert(QStringLiteral("pigmentConcentration"), v.pigmentConcentration);
    obj.insert(QStringLiteral("waterAmount"), v.waterAmount);
    obj.insert(QStringLiteral("paintBody"), v.paintBody);
    obj.insert(QStringLiteral("pickup"), v.pickup);
    obj.insert(QStringLiteral("aspectRatio"), v.aspectRatio);
    obj.insert(QStringLiteral("tipAngle"), v.tipAngle);
    obj.insert(QStringLiteral("followDirection"), v.followDirection);
    obj.insert(QStringLiteral("depletion"), v.depletion);
    obj.insert(QStringLiteral("edgeAccumulation"), v.edgeAccumulation);
    obj.insert(QStringLiteral("toothResponse"), v.toothResponse);
    obj.insert(QStringLiteral("granulation"), v.granulation);
    obj.insert(QStringLiteral("dryingSpeed"), v.dryingSpeed);
    obj.insert(QStringLiteral("pressureToAmount"), v.pressureToAmount);
    obj.insert(QStringLiteral("pressureToContact"), v.pressureToContact);
    if (!v.materialMixture.isEmpty())
        obj.insert(QStringLiteral("materialMixture"), QJsonObject::fromVariantMap(v.materialMixture));
    if (!v.executableTool.isEmpty())
        obj.insert(QStringLiteral("executableTool"), v.executableTool);
    if (!v.toolParams.isEmpty())
        obj.insert(QStringLiteral("toolParams"), QJsonObject::fromVariantMap(v.toolParams));
    return obj;
}

bool BrushLibraryStore::loadBundledCollections(const QJsonObject& root,
                                               BrushLibraryDocument* out,
                                               QString* error)
{
    if (!out) return fail(error, QStringLiteral("Invalid output brush library."));
    const QJsonObject collections = root.value(QStringLiteral("brushCollections")).toObject();
    if (collections.isEmpty())
        return fail(error, QStringLiteral("Missing brushCollections in bundled preset catalog."));

    BrushLibraryDocument doc;
    if (!bundledCollectionFromJson(collections.value(QStringLiteral("digital")).toObject(),
                                   0, &doc.digital, &doc.knownBuiltInIds, QStringLiteral("brush"))) {
        return fail(error, QStringLiteral("Invalid digital brush collection in catalog."));
    }
    if (!bundledCollectionFromJson(collections.value(QStringLiteral("realistic")).toObject(),
                                   1, &doc.realistic, &doc.knownBuiltInIds, QStringLiteral("brush"))) {
        return fail(error, QStringLiteral("Invalid realistic brush collection in catalog."));
    }

    // Seed universal toolCollections from "groups" (for pen/pencil/airbrush/eraser)
    // and then override/extend with explicit "toolCollections" definitions.
    const QJsonArray groups = root.value(QStringLiteral("groups")).toArray();
    for (const QJsonValue& groupVal : groups) {
        const QJsonObject gObj = groupVal.toObject();
        const QString tool = gObj.value(QStringLiteral("tool")).toString().trimmed();
        if (tool.isEmpty() || tool == QStringLiteral("brush")) continue;
        const bool erase = gObj.value(QStringLiteral("erase")).toBool(false);
        const QJsonArray presets = gObj.value(QStringLiteral("presets")).toArray();
        EngineCollectionData col;
        BrushTabData tab;
        tab.id = QStringLiteral("%1-default").arg(tool);
        tab.name = QStringLiteral("Default");
        for (const QJsonValue& pVal : presets) {
            if (!pVal.isObject()) continue;
            const auto pObj = pVal.toObject();
            BrushPresetValues values;
            if (!parseValues(pObj, &values)) continue;
            values.erase = erase;
            if (values.executableTool.isEmpty()) values.executableTool = tool;
            BrushEntryData entry;
            const QString id = pObj.value(QStringLiteral("id")).toString().trimmed();
            entry.id = id.isEmpty() ? QStringLiteral("builtin-%1-%2").arg(tool, QString::number(tab.brushes.size())) : id;
            entry.name = pObj.value(QStringLiteral("name")).toString().trimmed();
            entry.category = pObj.value(QStringLiteral("category")).toString().trimmed();
            if (entry.category.isEmpty()) entry.category = tab.name;
            entry.builtIn = true;
            entry.defaults = values;
            entry.current = values;
            if (!doc.knownBuiltInIds.contains(entry.id)) doc.knownBuiltInIds.append(entry.id);
            tab.brushes.append(std::move(entry));
        }
        if (!tab.brushes.isEmpty()) tab.currentBrushId = tab.brushes.first().id;
        col.defaultTabId = tab.id;
        col.currentTabId = tab.id;
        col.tabs.append(std::move(tab));
        doc.toolCollections.insert(tool, std::move(col));
    }

    const QJsonObject toolCols = root.value(QStringLiteral("toolCollections")).toObject();
    for (auto it = toolCols.constBegin(); it != toolCols.constEnd(); ++it) {
        if (!it.value().isObject()) continue;
        EngineCollectionData col;
        if (!bundledCollectionFromJson(it.value().toObject(), -1, &col, &doc.knownBuiltInIds, it.key())) {
            return fail(error, QStringLiteral("Invalid tool collection '%1' in catalog.").arg(it.key()));
        }
        doc.toolCollections.insert(it.key(), std::move(col));
    }

    *out = std::move(doc);
    return true;
}

bool BrushLibraryStore::load(const QString& filePath,
                             const BrushLibraryDocument& bundled,
                             BrushLibraryDocument* out,
                             QString* error)
{
    if (!out) return fail(error, QStringLiteral("Invalid output brush library."));
    QFile file(filePath);
    if (!file.exists()) {
        *out = bundled;
        return true;
    }
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Could not open brush library: %1").arg(file.errorString()));

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(error, QStringLiteral("Brush library JSON is corrupt."));

    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("format")).toString() == kFormatName
        ? root.value(QStringLiteral("version")).toInt(-1) : -1;
    if (version != 1 && version != 2) {
        return fail(error, QStringLiteral("Unsupported brush library format or version."));
    }
    const QByteArray expectedSha = root.value(QStringLiteral("sha256")).toString().toLatin1();
    QJsonObject payload = root;
    payload.remove(QStringLiteral("sha256"));
    if (expectedSha.isEmpty() || digestOf(payload) != expectedSha)
        return fail(error, QStringLiteral("Brush library checksum mismatch."));

    BrushLibraryDocument loaded;
    for (const auto& idVal : root.value(QStringLiteral("knownBuiltInIds")).toArray()) {
        const QString id = idVal.toString().trimmed();
        if (!id.isEmpty()) loaded.knownBuiltInIds.append(id);
    }
    if (!collectionFromJson(root.value(QStringLiteral("digital")).toObject(), 0, &loaded.digital)
        || !collectionFromJson(root.value(QStringLiteral("realistic")).toObject(), 1, &loaded.realistic)) {
        return fail(error, QStringLiteral("Brush library contains invalid engine collections."));
    }

    const QJsonObject savedToolCols = root.value(QStringLiteral("toolCollections")).toObject();
    for (auto it = savedToolCols.constBegin(); it != savedToolCols.constEnd(); ++it) {
        if (!it.value().isObject()) continue;
        EngineCollectionData col;
        if (!collectionFromJson(it.value().toObject(), -1, &col)) {
            return fail(error, QStringLiteral("Brush library contains invalid tool collection '%1'.").arg(it.key()));
        }
        loaded.toolCollections.insert(it.key(), std::move(col));
    }

    mergeNewBuiltIns(bundled.digital, loaded.knownBuiltInIds, &loaded.digital, &loaded.knownBuiltInIds);
    mergeNewBuiltIns(bundled.realistic, loaded.knownBuiltInIds, &loaded.realistic, &loaded.knownBuiltInIds);
    for (auto it = bundled.toolCollections.constBegin(); it != bundled.toolCollections.constEnd(); ++it) {
        if (!loaded.toolCollections.contains(it.key())) {
            loaded.toolCollections.insert(it.key(), it.value());
            for (const auto& bTab : it.value().tabs) {
                for (const auto& bEntry : bTab.brushes) {
                    if (bEntry.builtIn && !loaded.knownBuiltInIds.contains(bEntry.id))
                        loaded.knownBuiltInIds.append(bEntry.id);
                }
            }
        } else {
            mergeNewBuiltIns(it.value(), loaded.knownBuiltInIds,
                             &loaded.toolCollections[it.key()], &loaded.knownBuiltInIds);
        }
    }
    *out = std::move(loaded);
    return true;
}

bool BrushLibraryStore::save(const QString& filePath,
                             const BrushLibraryDocument& doc,
                             QString* error)
{
    const QFileInfo info(filePath);
    QDir dir(info.absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return fail(error, QStringLiteral("Could not create brush library directory."));

    QJsonArray knownIds;
    for (const auto& id : doc.knownBuiltInIds) knownIds.append(id);

    QJsonObject toolColsJson;
    for (auto it = doc.toolCollections.constBegin(); it != doc.toolCollections.constEnd(); ++it) {
        toolColsJson.insert(it.key(), collectionToJson(it.value()));
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), kFormatName);
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("knownBuiltInIds"), knownIds);
    root.insert(QStringLiteral("digital"), collectionToJson(doc.digital));
    root.insert(QStringLiteral("realistic"), collectionToJson(doc.realistic));
    root.insert(QStringLiteral("toolCollections"), toolColsJson);
    root.insert(QStringLiteral("sha256"), QString::fromLatin1(digestOf(root)));

    QSaveFile saveFile(filePath);
    if (!saveFile.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("Could not open brush library for writing."));
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (saveFile.write(json) != json.size() || !saveFile.flush())
        return fail(error, QStringLiteral("Could not write brush library."));
#ifdef Q_OS_WIN
    _commit(saveFile.handle());
#else
    ::fsync(saveFile.handle());
#endif
    if (!saveFile.commit())
        return fail(error, QStringLiteral("Could not finalize brush library save."));
    return true;
}

} // namespace komapaint
