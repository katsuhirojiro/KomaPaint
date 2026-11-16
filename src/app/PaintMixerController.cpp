#include "PaintMixerController.h"

#include "BrushSettings.h"
#include "../color/ColorPalette.h"

#include <QDateTime>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace komapaint {
namespace {

QString formatArgbHex(std::uint32_t argb)
{
    return QStringLiteral("#%1")
        .arg(argb & 0x00ffffffU, 6, 16, QLatin1Char('0')).toUpper();
}

} // namespace

PaintMixerController::PaintMixerController(BrushSettings* brush, ColorPalette* colors, QObject* parent)
    : QObject(parent),
      m_brush(brush),
      m_colors(colors)
{
    if (m_brush) {
        connect(m_brush, &BrushSettings::materialMixtureChanged,
                this, &PaintMixerController::brushStateChanged);
        connect(m_brush, &BrushSettings::changed,
                this, &PaintMixerController::brushStateChanged);
    }
}

QString PaintMixerController::activeMediumId() const
{
    return QString::fromStdString(m_model.activeMediumId());
}

void PaintMixerController::setActiveMediumId(const QString& mediumId)
{
    const std::string target = mediumId.trimmed() == QStringLiteral("watercolour")
        ? std::string("watercolour") : std::string("gouache");
    if (m_model.activeMediumId() == target) return;
    if (m_model.setMedium(target)) {
        setNotice(target == "watercolour"
            ? QStringLiteral("Switched mixing medium to Watercolour.")
            : QStringLiteral("Switched mixing medium to Gouache."));
        notifyMutation();
    }
}

QString PaintMixerController::argbToHex(std::uint32_t argb) const
{
    return formatArgbHex(argb);
}

QString PaintMixerController::summarizeBatch(const material::PaintBatch& batch) const
{
    if (batch.pigments.empty()) return QStringLiteral("Water / clear medium");
    QStringList parts;
    const double totalPigment = std::max(1e-9, batch.pigmentMass());
    for (const auto& c : batch.pigments) {
        const int pct = qRound((c.mass / totalPigment) * 100.0);
        if (c.pigmentId.empty()) {
            parts.append(QStringLiteral("Digital %1 (%2%)")
                .arg(formatArgbHex(c.residualArgb))
                .arg(pct));
        } else {
            parts.append(QStringLiteral("%1 (%2%)")
                .arg(QString::fromStdString(c.pigmentId))
                .arg(pct));
        }
    }
    return parts.join(QStringLiteral(" + "));
}

QVariantList PaintMixerController::pigmentLibrary() const
{
    QVariantList list;
    const std::string medium = m_model.activeMediumId();
    for (const auto& p : m_model.pigments().pigments()) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), QString::fromStdString(p.id));
        item.insert(QStringLiteral("name"), QString::fromStdString(p.name));
        item.insert(QStringLiteral("colorFamily"), QString::fromStdString(p.colorFamily));
        const std::uint32_t swatch = material::pigmentPreviewArgb(m_model.engine(), p, medium);
        item.insert(QStringLiteral("swatchHex"), formatArgbHex(swatch));
        QStringList mediaList;
        bool gouacheCompat = false;
        bool watercolourCompat = false;
        for (const auto& m : p.media) {
            const QString qm = QString::fromStdString(m);
            mediaList.append(qm);
            if (m == "gouache") gouacheCompat = true;
            if (m == "watercolour") watercolourCompat = true;
        }
        item.insert(QStringLiteral("media"), mediaList);
        item.insert(QStringLiteral("gouacheCompatible"), gouacheCompat);
        item.insert(QStringLiteral("watercolourCompatible"), watercolourCompat);
        item.insert(QStringLiteral("compatibleWithActive"),
                    medium == "watercolour" ? watercolourCompat : gouacheCompat);
        item.insert(QStringLiteral("transparency"), p.transparency);
        item.insert(QStringLiteral("tintStrength"), p.tintStrength);
        item.insert(QStringLiteral("granulation"), p.granulation);
        item.insert(QStringLiteral("staining"), p.staining);
        item.insert(QStringLiteral("liftability"), p.liftability);
        item.insert(QStringLiteral("settling"), p.settling);
        item.insert(QStringLiteral("particleSizeMicrons"), p.particleSizeMicrons);
        item.insert(QStringLiteral("physicalDensity"), p.physicalDensity);
        item.insert(QStringLiteral("verifiedMeasurement"), p.verifiedMeasurement);
        item.insert(QStringLiteral("provenance"), QString::fromStdString(p.provenance));
        list.append(item);
    }
    return list;
}

QVariantList PaintMixerController::deposits() const
{
    QVariantList list;
    for (const auto& d : m_model.deposits()) {
        const auto summary = m_model.opticalSummary(d);
        QVariantMap item;
        item.insert(QStringLiteral("id"), QString::fromStdString(d.id));
        item.insert(QStringLiteral("name"), QString::fromStdString(d.name));
        item.insert(QStringLiteral("x"), d.x);
        item.insert(QStringLiteral("y"), d.y);
        item.insert(QStringLiteral("radius"), d.radius);
        item.insert(QStringLiteral("mediumId"), QString::fromStdString(d.batch.mediumId));
        item.insert(QStringLiteral("porcelainHex"), formatArgbHex(summary.porcelainArgb));
        item.insert(QStringLiteral("darkGroundHex"), formatArgbHex(summary.darkGroundArgb));
        item.insert(QStringLiteral("straightHex"), formatArgbHex(summary.straightArgb));
        item.insert(QStringLiteral("alpha"), summary.alpha);
        item.insert(QStringLiteral("containsDigitalImport"), d.containsDigitalImport());
        item.insert(QStringLiteral("hasPigment"), !d.batch.pigments.empty());
        item.insert(QStringLiteral("summary"), summarizeBatch(d.batch));
        list.append(item);
    }
    return list;
}

QString PaintMixerController::activeDepositId() const
{
    return QString::fromStdString(m_model.activeDepositId());
}

QVariantMap PaintMixerController::activeDeposit() const
{
    const auto* d = m_model.activeDeposit();
    if (!d) return QVariantMap{};
    const auto summary = m_model.opticalSummary(*d);

    QVariantList components;
    const double totalPigment = std::max(1e-9, d->batch.pigmentMass());
    for (std::size_t i = 0; i < d->batch.pigments.size(); ++i) {
        const auto& c = d->batch.pigments[i];
        QVariantMap cm;
        cm.insert(QStringLiteral("index"), static_cast<int>(i));
        const bool isDigital = c.pigmentId.empty();
        cm.insert(QStringLiteral("isDigital"), isDigital);
        cm.insert(QStringLiteral("pigmentId"), QString::fromStdString(c.pigmentId));
        cm.insert(QStringLiteral("mass"), c.mass);
        cm.insert(QStringLiteral("sharePercent"), (c.mass / totalPigment) * 100.0);
        if (isDigital) {
            const QString hex = formatArgbHex(c.residualArgb);
            cm.insert(QStringLiteral("label"), QStringLiteral("Digital / Imported %1").arg(hex));
            cm.insert(QStringLiteral("swatchHex"), hex);
            cm.insert(QStringLiteral("provenance"),
                      QStringLiteral("Imported digital RGB colour (not a real-world pigment)"));
        } else if (const auto* pig = m_model.pigments().find(c.pigmentId)) {
            const auto swatch = material::pigmentPreviewArgb(m_model.engine(), *pig, d->batch.mediumId);
            cm.insert(QStringLiteral("label"),
                      QStringLiteral("%1 · %2").arg(QString::fromStdString(pig->id),
                                                    QString::fromStdString(pig->name)));
            cm.insert(QStringLiteral("swatchHex"), formatArgbHex(swatch));
            cm.insert(QStringLiteral("provenance"), QString::fromStdString(pig->provenance));
        }
        components.append(cm);
    }

    QVariantMap out;
    out.insert(QStringLiteral("id"), QString::fromStdString(d->id));
    out.insert(QStringLiteral("name"), QString::fromStdString(d->name));
    out.insert(QStringLiteral("mediumId"), QString::fromStdString(d->batch.mediumId));
    out.insert(QStringLiteral("hasComponents"), !d->batch.pigments.empty());
    out.insert(QStringLiteral("containsDigitalImport"), d->containsDigitalImport());
    out.insert(QStringLiteral("porcelainHex"), formatArgbHex(summary.porcelainArgb));
    out.insert(QStringLiteral("darkGroundHex"), formatArgbHex(summary.darkGroundArgb));
    out.insert(QStringLiteral("straightHex"), formatArgbHex(summary.straightArgb));
    out.insert(QStringLiteral("alpha"), summary.alpha);
    out.insert(QStringLiteral("granulation"), summary.granulation);
    out.insert(QStringLiteral("pigmentShare"), summary.pigmentShare);
    out.insert(QStringLiteral("waterShare"), summary.waterShare);
    out.insert(QStringLiteral("binderShare"), summary.binderShare);
    out.insert(QStringLiteral("fillerShare"), summary.fillerShare);
    out.insert(QStringLiteral("water"), d->batch.water);
    out.insert(QStringLiteral("binder"), d->batch.binder);
    out.insert(QStringLiteral("filler"), d->batch.filler);
    out.insert(QStringLiteral("pigmentMass"), d->batch.pigmentMass());
    out.insert(QStringLiteral("totalMass"), d->batch.totalMass());
    out.insert(QStringLiteral("components"), components);
    return out;
}

QVariantList PaintMixerController::savedMixtures() const
{
    QVariantList list;
    for (const auto& entry : m_savedMixtures) {
        material::MixerDeposit temp;
        temp.batch = entry.batch;
        const auto summary = m_model.opticalSummary(temp);
        QVariantMap item;
        item.insert(QStringLiteral("id"), entry.id);
        item.insert(QStringLiteral("name"), entry.name);
        item.insert(QStringLiteral("notes"), entry.notes);
        item.insert(QStringLiteral("mediumId"), QString::fromStdString(entry.batch.mediumId));
        item.insert(QStringLiteral("porcelainHex"), formatArgbHex(summary.porcelainArgb));
        item.insert(QStringLiteral("darkGroundHex"), formatArgbHex(summary.darkGroundArgb));
        item.insert(QStringLiteral("alpha"), summary.alpha);
        item.insert(QStringLiteral("containsDigitalImport"), temp.containsDigitalImport());
        item.insert(QStringLiteral("summary"), summarizeBatch(entry.batch));
        item.insert(QStringLiteral("updatedAt"), entry.updatedAt);
        list.append(item);
    }
    return list;
}

bool PaintMixerController::brushLoadedFromMixer() const
{
    return m_brush && m_brush->traditionalMedia() && !m_brush->materialMixture().isEmpty();
}

bool PaintMixerController::initializePersistence(const QString& filePath)
{
    m_persistencePath = filePath;
    if (filePath.isEmpty()) return false;

    MixLibraryData loaded;
    QString error;
    int skipped = 0;
    const auto status = PaintMixerStore::load(filePath, m_model.pigments(), &loaded, &error, &skipped);
    if (status == PaintMixerStore::LoadResult::Missing) {
        return true;
    }
    if (status == PaintMixerStore::LoadResult::Invalid) {
        setError(error);
        return false;
    }
    m_savedMixtures = std::move(loaded.savedMixtures);
    if (!loaded.workspaceDeposits.empty()) {
        m_model.restoreWorkspaceState(loaded.activeMediumId.toStdString(),
                                      std::move(loaded.workspaceDeposits),
                                      loaded.activeDepositId.toStdString());
    } else if (!loaded.activeMediumId.isEmpty()) {
        m_model.restoreWorkspaceState(loaded.activeMediumId.toStdString(), {}, {});
    }
    if (skipped > 0) {
        setNotice(QStringLiteral("Loaded mixture library (%1 invalid entry/entries skipped).").arg(skipped));
    }
    ++m_surfaceRevision;
    emit stateChanged();
    emit savedMixturesChanged();
    return true;
}

bool PaintMixerController::persistLibrary()
{
    if (m_persistencePath.isEmpty()) return true;
    MixLibraryData data;
    data.activeMediumId = activeMediumId();
    data.activeDepositId = activeDepositId();
    data.workspaceDeposits = m_model.deposits();
    data.savedMixtures = m_savedMixtures;
    QString error;
    if (!PaintMixerStore::save(m_persistencePath, data, &error)) {
        setError(error);
        return false;
    }
    return true;
}

void PaintMixerController::notifyMutation(bool persistWorkspace)
{
    ++m_surfaceRevision;
    emit stateChanged();
    if (persistWorkspace) persistLibrary();
}

void PaintMixerController::setNotice(const QString& notice)
{
    if (m_statusNotice == notice) return;
    m_statusNotice = notice;
    emit noticeChanged();
}

void PaintMixerController::setError(const QString& error)
{
    if (m_lastError == error) return;
    m_lastError = error;
    emit errorChanged();
}

void PaintMixerController::selectDeposit(const QString& id)
{
    if (m_model.selectDeposit(id.toStdString())) {
        emit stateChanged();
        persistLibrary();
    }
}

QString PaintMixerController::depositIdAt(double x, double y) const
{
    const auto found = m_model.depositAt(x, y);
    return found ? QString::fromStdString(*found) : QString{};
}

QString PaintMixerController::createEmptyDeposit(const QString& name, double x, double y)
{
    const std::string id = m_model.createEmptyDeposit(name.trimmed().toStdString(), x, y);
    if (id.empty()) return {};
    setNotice(QStringLiteral("Created new mixing well."));
    notifyMutation();
    return QString::fromStdString(id);
}

bool PaintMixerController::addPigmentToActive(const QString& pigmentId, double quantity)
{
    if (!m_model.addPigmentToActive(pigmentId.toStdString(), quantity, true))
        return false;
    setNotice(QStringLiteral("Added %1 to active deposit.").arg(pigmentId));
    notifyMutation();
    return true;
}

QString PaintMixerController::addPigmentAsNewDeposit(const QString& pigmentId, double quantity,
                                                     double x, double y)
{
    const std::string id = m_model.addPigmentDeposit(pigmentId.toStdString(), quantity, x, y);
    if (id.empty()) return {};
    setNotice(QStringLiteral("Placed %1 deposit on mixing palette.").arg(pigmentId));
    notifyMutation();
    return QString::fromStdString(id);
}

QVariantMap PaintMixerController::addFromColorWheel(const QColor& color,
                                                    bool allowPigmentMatch,
                                                    bool createNewDeposit)
{
    QVariantMap response;
    if (!color.isValid()) {
        response.insert(QStringLiteral("ok"), false);
        return response;
    }
    const QColor opaque = color.toRgb();
    const std::uint32_t argb = 0xff000000U
        | (static_cast<std::uint32_t>(opaque.red()) << 16U)
        | (static_cast<std::uint32_t>(opaque.green()) << 8U)
        | static_cast<std::uint32_t>(opaque.blue());
    const auto res = m_model.addColorFromWheel(argb, allowPigmentMatch, createNewDeposit, 1.0);
    response.insert(QStringLiteral("ok"), res.ok);
    response.insert(QStringLiteral("depositId"), QString::fromStdString(res.depositId));
    response.insert(QStringLiteral("matchedKnownPigment"), res.matchedKnownPigment);
    response.insert(QStringLiteral("matchedPigmentId"), QString::fromStdString(res.matchedPigmentId));
    response.insert(QStringLiteral("hex"), formatArgbHex(argb));
    if (res.ok) {
        if (res.matchedKnownPigment) {
            setNotice(QStringLiteral("Added %1 (matched pigment %2) to mixer.")
                .arg(formatArgbHex(argb), QString::fromStdString(res.matchedPigmentId)));
        } else {
            setNotice(QStringLiteral("Added %1 as Digital / Imported colour.")
                .arg(formatArgbHex(argb)));
        }
        notifyMutation();
    }
    return response;
}

bool PaintMixerController::sendActiveColorToWheel()
{
    if (!m_colors) return false;
    const auto* active = m_model.activeDeposit();
    if (!active || active->batch.pigments.empty()) return false;
    const auto summary = m_model.opticalSummary(*active);
    const QColor resultColor = QColor::fromRgba(summary.porcelainArgb);
    m_colors->setForeground(resultColor);
    setNotice(QStringLiteral("Updated drawing colour to %1.").arg(formatArgbHex(summary.porcelainArgb)));
    return true;
}

bool PaintMixerController::setComponentMass(int index, double mass)
{
    if (index < 0 || !m_model.setComponentMass(static_cast<std::size_t>(index), mass))
        return false;
    notifyMutation();
    return true;
}

bool PaintMixerController::removeComponent(int index)
{
    if (index < 0 || !m_model.removeComponent(static_cast<std::size_t>(index)))
        return false;
    notifyMutation();
    return true;
}

bool PaintMixerController::addWater(double delta)
{
    if (!m_model.addWaterToActive(delta)) return false;
    notifyMutation();
    return true;
}

bool PaintMixerController::setWater(double water)
{
    if (!m_model.setWaterOnActive(water)) return false;
    notifyMutation();
    return true;
}

bool PaintMixerController::setBinder(double binder)
{
    if (!m_model.setBinderOnActive(binder)) return false;
    notifyMutation();
    return true;
}

bool PaintMixerController::setFiller(double filler)
{
    if (!m_model.setFillerOnActive(filler)) return false;
    notifyMutation();
    return true;
}

bool PaintMixerController::addPaintBody(double factorDelta)
{
    if (!m_model.addPaintBodyToActive(factorDelta)) return false;
    notifyMutation();
    return true;
}

QString PaintMixerController::mixDeposits(const QString& sourceId, const QString& targetId,
                                          double sourceShare, bool createNew)
{
    const auto result = m_model.mixDeposits(sourceId.toStdString(), targetId.toStdString(),
                                            sourceShare, createNew);
    if (!result) return {};
    setNotice(QStringLiteral("Mixed deposits on palette."));
    notifyMutation();
    return QString::fromStdString(*result);
}

bool PaintMixerController::moveDeposit(const QString& id, double x, double y)
{
    if (!m_model.moveDeposit(id.toStdString(), x, y)) return false;
    notifyMutation();
    return true;
}

bool PaintMixerController::renameActiveDeposit(const QString& name)
{
    if (m_model.activeDepositId().empty()) return false;
    if (!m_model.renameDeposit(m_model.activeDepositId(), name.trimmed().toStdString()))
        return false;
    notifyMutation();
    return true;
}

bool PaintMixerController::resetActiveDeposit()
{
    if (m_model.activeDepositId().empty()) return false;
    if (!m_model.resetDeposit(m_model.activeDepositId())) return false;
    setNotice(QStringLiteral("Reset active deposit."));
    notifyMutation();
    return true;
}

bool PaintMixerController::removeDeposit(const QString& id)
{
    if (!m_model.removeDeposit(id.toStdString())) return false;
    notifyMutation();
    return true;
}

void PaintMixerController::clearPalette()
{
    if (m_model.deposits().empty()) return;
    m_model.clearAllDeposits();
    setNotice(QStringLiteral("Cleared mixing palette."));
    notifyMutation();
}

bool PaintMixerController::undo()
{
    if (!m_model.undo()) return false;
    setNotice(QStringLiteral("Undid mixer change."));
    notifyMutation();
    return true;
}

bool PaintMixerController::redo()
{
    if (!m_model.redo()) return false;
    setNotice(QStringLiteral("Redid mixer change."));
    notifyMutation();
    return true;
}

QVariantMap PaintMixerController::batchToBrushMap(const material::PaintBatch& batch)
{
    QVariantList components;
    for (const auto& c : batch.pigments) {
        QVariantMap item;
        item.insert(QStringLiteral("pigmentId"), QString::fromStdString(c.pigmentId));
        if (c.pigmentId.empty())
            item.insert(QStringLiteral("residualArgb"), static_cast<qulonglong>(c.residualArgb));
        item.insert(QStringLiteral("mass"), c.mass);
        components.append(item);
    }
    QVariantMap out;
    out.insert(QStringLiteral("mediumId"), QString::fromStdString(batch.mediumId));
    out.insert(QStringLiteral("pigments"), components);
    out.insert(QStringLiteral("water"), batch.water);
    out.insert(QStringLiteral("binder"), batch.binder);
    out.insert(QStringLiteral("filler"), batch.filler);
    out.insert(QStringLiteral("drying"), batch.drying);
    out.insert(QStringLiteral("settling"), batch.settling);
    return out;
}

bool PaintMixerController::loadBatchIntoBrush(const material::PaintBatch& batch, const QString& label)
{
    if (!m_brush || !batch.valid() || batch.totalMass() <= 0.0) return false;
    const QString targetMedium = batch.mediumId == "watercolour"
        ? QStringLiteral("watercolour") : QStringLiteral("gouache");
    const QVariantMap map = batchToBrushMap(batch);
    if (!m_brush->loadMaterialMixture(map)) return false;

    m_brush->setErase(false);
    m_brush->setTraditionalMedia(true);
    m_brush->setMediumId(targetMedium);
    // If current preset is a pigment-free water/lifting tool, switch to a
    // paint-bearing round brush in the target medium while keeping user tip
    // choices like Dry Gouache or Watercolour Mop intact when compatible.
    const QString preset = m_brush->materialPresetId();
    if (preset == QStringLiteral("clean-water") || preset == QStringLiteral("paint-lifting")) {
        m_brush->setMaterialPresetId(targetMedium == QStringLiteral("watercolour")
            ? QStringLiteral("watercolour-round") : QStringLiteral("gouache-round"));
    }

    const QString reloadMode = m_brush->persistentReservoir()
        ? QStringLiteral("Persistent reservoir ON")
        : QStringLiteral("Auto-reload ON");
    setNotice(QStringLiteral("Loaded \"%1\" into brush (%2 · %3).")
        .arg(label, targetMedium == QStringLiteral("watercolour")
             ? QStringLiteral("Watercolour") : QStringLiteral("Gouache"),
             reloadMode));
    emit brushStateChanged();
    return true;
}

bool PaintMixerController::loadActiveIntoBrush()
{
    const auto* active = m_model.activeDeposit();
    if (!active) {
        setError(QStringLiteral("Select or create a paint deposit before loading into the brush."));
        return false;
    }
    return loadBatchIntoBrush(active->batch, QString::fromStdString(active->name));
}

bool PaintMixerController::loadSavedIntoBrush(const QString& savedId)
{
    const auto it = std::find_if(m_savedMixtures.begin(), m_savedMixtures.end(),
                                 [&savedId](const SavedMixture& m) { return m.id == savedId; });
    if (it == m_savedMixtures.end()) return false;
    return loadBatchIntoBrush(it->batch, it->name);
}

QString PaintMixerController::saveActiveMixture(const QString& name, const QString& notes)
{
    const auto* active = m_model.activeDeposit();
    const QString trimmed = name.trimmed();
    if (!active) {
        setError(QStringLiteral("No active paint deposit to save."));
        return {};
    }
    const QString finalName = trimmed.isEmpty() ? QString::fromStdString(active->name) : trimmed;
    if (finalName.isEmpty()) {
        setError(QStringLiteral("Enter a name for the saved mixture."));
        return {};
    }
    SavedMixture entry;
    entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.name = finalName;
    entry.notes = notes.trimmed();
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    entry.createdAt = now;
    entry.updatedAt = now;
    entry.batch = active->batch;
    m_savedMixtures.insert(m_savedMixtures.begin(), entry);
    emit savedMixturesChanged();
    persistLibrary();
    setNotice(QStringLiteral("Saved mixture \"%1\".").arg(finalName));
    return entry.id;
}

bool PaintMixerController::openSavedMixture(const QString& savedId)
{
    const auto it = std::find_if(m_savedMixtures.begin(), m_savedMixtures.end(),
                                 [&savedId](const SavedMixture& m) { return m.id == savedId; });
    if (it == m_savedMixtures.end()) return false;
    const std::string depositId = m_model.createDeposit(it->name.toStdString(), it->batch);
    if (depositId.empty()) return false;
    setNotice(QStringLiteral("Opened saved mixture \"%1\" on palette.").arg(it->name));
    notifyMutation();
    return true;
}

QString PaintMixerController::duplicateSavedMixture(const QString& savedId)
{
    const auto it = std::find_if(m_savedMixtures.begin(), m_savedMixtures.end(),
                                 [&savedId](const SavedMixture& m) { return m.id == savedId; });
    if (it == m_savedMixtures.end()) return {};
    SavedMixture copy = *it;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = QStringLiteral("%1 (Copy)").arg(it->name);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    copy.createdAt = now;
    copy.updatedAt = now;
    const auto insertAt = std::next(it);
    const QString createdId = copy.id;
    m_savedMixtures.insert(insertAt, std::move(copy));
    emit savedMixturesChanged();
    persistLibrary();
    setNotice(QStringLiteral("Duplicated mixture \"%1\".").arg(it->name));
    return createdId;
}

bool PaintMixerController::renameSavedMixture(const QString& savedId, const QString& newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) return false;
    const auto it = std::find_if(m_savedMixtures.begin(), m_savedMixtures.end(),
                                 [&savedId](const SavedMixture& m) { return m.id == savedId; });
    if (it == m_savedMixtures.end()) return false;
    it->name = trimmed;
    it->updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    emit savedMixturesChanged();
    persistLibrary();
    return true;
}

bool PaintMixerController::deleteSavedMixture(const QString& savedId)
{
    const auto it = std::find_if(m_savedMixtures.begin(), m_savedMixtures.end(),
                                 [&savedId](const SavedMixture& m) { return m.id == savedId; });
    if (it == m_savedMixtures.end()) return false;
    const QString deletedName = it->name;
    m_savedMixtures.erase(it);
    emit savedMixturesChanged();
    persistLibrary();
    setNotice(QStringLiteral("Deleted saved mixture \"%1\".").arg(deletedName));
    return true;
}

QImage PaintMixerController::renderSurfaceImage() const
{
    std::vector<std::uint32_t> pixels;
    if (!m_model.renderSurface(pixels)) return {};
    QImage img(material::PaintMixerModel::SurfaceWidth,
               material::PaintMixerModel::SurfaceHeight,
               QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < material::PaintMixerModel::SurfaceHeight; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(img.scanLine(y));
        const std::size_t offset = static_cast<std::size_t>(y) * material::PaintMixerModel::SurfaceWidth;
        std::copy(pixels.begin() + offset,
                  pixels.begin() + offset + material::PaintMixerModel::SurfaceWidth,
                  row);
    }
    return img;
}

} // namespace komapaint
