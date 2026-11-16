#include "PaintMixerStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

namespace komapaint {
namespace {

const QString kFormatName = QStringLiteral("komapaint-mixer-library");

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

QJsonObject batchToJson(const material::PaintBatch& batch)
{
    QJsonArray pigments;
    for (const auto& c : batch.pigments) {
        QJsonObject obj;
        obj.insert(QStringLiteral("pigmentId"), QString::fromStdString(c.pigmentId));
        if (c.pigmentId.empty())
            obj.insert(QStringLiteral("residualArgb"), QString::number(c.residualArgb, 16));
        obj.insert(QStringLiteral("mass"), c.mass);
        pigments.append(obj);
    }
    QJsonObject out;
    out.insert(QStringLiteral("mediumId"), QString::fromStdString(batch.mediumId));
    out.insert(QStringLiteral("pigments"), pigments);
    out.insert(QStringLiteral("water"), batch.water);
    out.insert(QStringLiteral("binder"), batch.binder);
    out.insert(QStringLiteral("filler"), batch.filler);
    out.insert(QStringLiteral("drying"), batch.drying);
    out.insert(QStringLiteral("settling"), batch.settling);
    return out;
}

bool batchFromJson(const QJsonObject& obj,
                   const material::PigmentLibrary& pigments,
                   material::PaintBatch* out)
{
    if (!out) return false;
    const QString mediumStr = obj.value(QStringLiteral("mediumId")).toString();
    const std::string mediumId = mediumStr.toStdString();
    if (mediumId != "gouache" && mediumId != "watercolour") return false;
    if (!material::findMediumProfile(mediumId)) return false;

    const auto pigmentsVal = obj.value(QStringLiteral("pigments"));
    if (!pigmentsVal.isArray()) return false;

    material::PaintBatch batch;
    batch.mediumId = mediumId;
    for (const auto& val : pigmentsVal.toArray()) {
        if (!val.isObject()) return false;
        const auto cObj = val.toObject();
        const std::string pigmentId = cObj.value(QStringLiteral("pigmentId")).toString().toStdString();
        const double mass = cObj.value(QStringLiteral("mass")).toDouble(-1.0);
        if (!std::isfinite(mass) || mass <= 0.0) return false;
        if (pigmentId.empty()) {
            bool hexOk = false;
            const auto argb = static_cast<std::uint32_t>(
                cObj.value(QStringLiteral("residualArgb")).toString().toUInt(&hexOk, 16));
            if (!hexOk || (argb >> 24U) != 0xffU || !batch.addResidual(argb, mass))
                return false;
        } else {
            if (!pigments.find(pigmentId) || !batch.addPigment(pigmentId, mass))
                return false;
        }
    }

    const double water = obj.value(QStringLiteral("water")).toDouble(-1.0);
    const double binder = obj.value(QStringLiteral("binder")).toDouble(-1.0);
    const double filler = obj.value(QStringLiteral("filler")).toDouble(-1.0);
    const double drying = obj.value(QStringLiteral("drying")).toDouble(0.0);
    const double settling = obj.value(QStringLiteral("settling")).toDouble(0.0);
    if (!std::isfinite(water) || water < 0.0
        || !std::isfinite(binder) || binder < 0.0
        || !std::isfinite(filler) || filler < 0.0
        || !std::isfinite(drying) || drying < 0.0 || drying > 1.0
        || !std::isfinite(settling) || settling < 0.0 || settling > 1.0) {
        return false;
    }
    batch.water = water;
    batch.binder = binder;
    batch.filler = filler;
    batch.drying = drying;
    batch.settling = settling;
    const double total = batch.totalMass();
    batch.wetness = total > 0.0 ? std::clamp(batch.water / total, 0.0, 1.0) : 0.0;
    batch.consistency = total > 0.0
        ? std::clamp((batch.pigmentMass() + batch.binder + batch.filler) / total, 0.0, 1.0) : 0.0;
    if (!batch.valid()) return false;
    *out = std::move(batch);
    return true;
}

} // namespace

PaintMixerStore::LoadResult PaintMixerStore::load(const QString& path,
                                                  const material::PigmentLibrary& pigments,
                                                  MixLibraryData* data,
                                                  QString* error,
                                                  int* skippedInvalidCount)
{
    if (skippedInvalidCount) *skippedInvalidCount = 0;
    if (path.isEmpty() || !QFile::exists(path)) return LoadResult::Missing;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("The saved mixtures file could not be read."));
        return LoadResult::Invalid;
    }
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !json.isObject()) {
        fail(error, QStringLiteral("The saved mixtures file is incomplete or damaged."));
        return LoadResult::Invalid;
    }
    auto root = json.object();
    const auto stored = root.take(QStringLiteral("sha256")).toString().toLatin1();
    if (stored.size() != 64 || stored != digestOf(root)) {
        fail(error, QStringLiteral("The saved mixtures file failed its checksum."));
        return LoadResult::Invalid;
    }
    if (root.value(QStringLiteral("format")).toString() != kFormatName) {
        fail(error, QStringLiteral("The file is not a KomaPaint mixture library."));
        return LoadResult::Invalid;
    }
    const int version = root.value(QStringLiteral("version")).toInt();
    if (version < 1 || version > FormatVersion) {
        fail(error, QStringLiteral("The saved mixtures file was written by a newer KomaPaint."));
        return LoadResult::Invalid;
    }

    const auto mixturesVal = root.value(QStringLiteral("savedMixtures"));
    if (!mixturesVal.isArray()) {
        fail(error, QStringLiteral("The saved mixtures file has an invalid structure."));
        return LoadResult::Invalid;
    }

    MixLibraryData loaded;
    const QString medium = root.value(QStringLiteral("activeMediumId")).toString(QStringLiteral("gouache"));
    loaded.activeMediumId = (medium == QStringLiteral("watercolour"))
        ? QStringLiteral("watercolour") : QStringLiteral("gouache");
    loaded.activeDepositId = root.value(QStringLiteral("activeDepositId")).toString();

    int skipped = 0;
    for (const auto& val : mixturesVal.toArray()) {
        if (!val.isObject()) { ++skipped; continue; }
        const auto item = val.toObject();
        SavedMixture entry;
        entry.id = item.value(QStringLiteral("id")).toString();
        entry.name = item.value(QStringLiteral("name")).toString().trimmed();
        entry.notes = item.value(QStringLiteral("notes")).toString();
        entry.createdAt = item.value(QStringLiteral("createdAt")).toString();
        entry.updatedAt = item.value(QStringLiteral("updatedAt")).toString();
        if (entry.id.isEmpty() || entry.name.isEmpty()
            || !batchFromJson(item.value(QStringLiteral("batch")).toObject(), pigments, &entry.batch)) {
            ++skipped;
            continue;
        }
        loaded.savedMixtures.push_back(std::move(entry));
    }

    const auto depositsVal = root.value(QStringLiteral("workspaceDeposits"));
    if (depositsVal.isArray()) {
        for (const auto& val : depositsVal.toArray()) {
            if (!val.isObject()) { ++skipped; continue; }
            const auto item = val.toObject();
            material::MixerDeposit d;
            d.id = item.value(QStringLiteral("id")).toString().toStdString();
            d.name = item.value(QStringLiteral("name")).toString().toStdString();
            d.x = item.value(QStringLiteral("x")).toDouble(48.0);
            d.y = item.value(QStringLiteral("y")).toDouble(48.0);
            d.radius = item.value(QStringLiteral("radius")).toDouble(22.0);
            if (d.id.empty() || !std::isfinite(d.x) || !std::isfinite(d.y)
                || !std::isfinite(d.radius) || d.radius <= 0.0
                || !batchFromJson(item.value(QStringLiteral("batch")).toObject(), pigments, &d.batch)) {
                ++skipped;
                continue;
            }
            loaded.workspaceDeposits.push_back(std::move(d));
        }
    }

    if (skippedInvalidCount) *skippedInvalidCount = skipped;
    if (data) *data = std::move(loaded);
    return LoadResult::Loaded;
}

bool PaintMixerStore::save(const QString& path,
                           const MixLibraryData& data,
                           QString* error)
{
    if (path.isEmpty()) return fail(error, QStringLiteral("No mixtures file location is set."));
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(error, QStringLiteral("The mixtures folder could not be created."));

    QJsonArray savedArray;
    for (const auto& entry : data.savedMixtures) {
        if (entry.id.isEmpty() || entry.name.trimmed().isEmpty() || !entry.batch.valid()) continue;
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), entry.id);
        obj.insert(QStringLiteral("name"), entry.name.trimmed());
        obj.insert(QStringLiteral("notes"), entry.notes);
        obj.insert(QStringLiteral("createdAt"), entry.createdAt);
        obj.insert(QStringLiteral("updatedAt"), entry.updatedAt);
        obj.insert(QStringLiteral("batch"), batchToJson(entry.batch));
        savedArray.append(obj);
    }

    QJsonArray depositsArray;
    for (const auto& d : data.workspaceDeposits) {
        if (d.id.empty() || !d.batch.valid()) continue;
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), QString::fromStdString(d.id));
        obj.insert(QStringLiteral("name"), QString::fromStdString(d.name));
        obj.insert(QStringLiteral("x"), d.x);
        obj.insert(QStringLiteral("y"), d.y);
        obj.insert(QStringLiteral("radius"), d.radius);
        obj.insert(QStringLiteral("batch"), batchToJson(d.batch));
        depositsArray.append(obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), kFormatName);
    root.insert(QStringLiteral("version"), FormatVersion);
    root.insert(QStringLiteral("activeMediumId"),
                data.activeMediumId == QStringLiteral("watercolour")
                    ? QStringLiteral("watercolour") : QStringLiteral("gouache"));
    root.insert(QStringLiteral("activeDepositId"), data.activeDepositId);
    root.insert(QStringLiteral("workspaceDeposits"), depositsArray);
    root.insert(QStringLiteral("savedMixtures"), savedArray);
    root.insert(QStringLiteral("sha256"), QString::fromLatin1(digestOf(root)));

    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush())
        return fail(error, QStringLiteral("The saved mixtures file could not be written."));
#ifdef Q_OS_WIN
    _commit(file.handle());
#else
    ::fsync(file.handle());
#endif
    if (!file.commit())
        return fail(error, QStringLiteral("The saved mixtures file could not be finalized."));
    return true;
}

} // namespace komapaint
