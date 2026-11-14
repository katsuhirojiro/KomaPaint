#include "SessionStore.h"

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

const QString kFormatName = QStringLiteral("komapaint-session");

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

}  // namespace

SessionStore::LoadResult SessionStore::load(const QString& path, SessionData* data, QString* error)
{
    if (path.isEmpty() || !QFile::exists(path)) return LoadResult::Missing;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("The session file could not be read."));
        return LoadResult::Invalid;
    }
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !json.isObject()) {
        fail(error, QStringLiteral("The session file is incomplete or damaged."));
        return LoadResult::Invalid;
    }
    auto root = json.object();
    const auto stored = root.take(QStringLiteral("sha256")).toString().toLatin1();
    if (stored.size() != 64 || stored != digestOf(root)) {
        fail(error, QStringLiteral("The session file failed its checksum."));
        return LoadResult::Invalid;
    }
    if (root.value(QStringLiteral("format")).toString() != kFormatName) {
        fail(error, QStringLiteral("The session file is not a KomaPaint session."));
        return LoadResult::Invalid;
    }
    const int version = root.value(QStringLiteral("version")).toInt();
    if (version < 1 || version > FormatVersion) {
        fail(error, QStringLiteral("The session file was written by a newer KomaPaint."));
        return LoadResult::Invalid;
    }
    const auto documents = root.value(QStringLiteral("documents"));
    if (!documents.isArray()) {
        fail(error, QStringLiteral("The session file has no project list."));
        return LoadResult::Invalid;
    }
    SessionData loaded;
    loaded.activeId = root.value(QStringLiteral("active")).toString();
    for (const auto& value : documents.toArray()) {
        // One malformed entry only loses that tab; it never hides the others.
        if (!value.isObject()) continue;
        const auto item = value.toObject();
        SessionDocument doc;
        doc.id = item.value(QStringLiteral("id")).toString();
        doc.path = item.value(QStringLiteral("path")).toString();
        if (doc.id.isEmpty() || doc.path.isEmpty()) continue;
        doc.title = item.value(QStringLiteral("title")).toString();
        doc.tool = item.value(QStringLiteral("tool")).toString();
        const double zoom = item.value(QStringLiteral("zoom")).toDouble(100.0);
        const double cx = item.value(QStringLiteral("centerX")).toDouble();
        const double cy = item.value(QStringLiteral("centerY")).toDouble();
        doc.hasView = item.value(QStringLiteral("hasView")).toBool()
            && std::isfinite(zoom) && zoom > 0 && std::isfinite(cx) && std::isfinite(cy);
        doc.zoom = doc.hasView ? zoom : 100.0;
        doc.center = doc.hasView ? QPointF(cx, cy) : QPointF();
        loaded.documents.push_back(doc);
    }
    loaded.ui = root.value(QStringLiteral("ui")).toObject().toVariantMap();
    if (data) *data = std::move(loaded);
    return LoadResult::Loaded;
}

bool SessionStore::save(const QString& path, const SessionData& data, QString* error)
{
    if (path.isEmpty()) return fail(error, QStringLiteral("No session file location is set."));
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(error, QStringLiteral("The session folder could not be created."));
    QJsonArray documents;
    for (const auto& doc : data.documents) {
        QJsonObject item;
        item.insert(QStringLiteral("id"), doc.id);
        item.insert(QStringLiteral("path"), doc.path);
        item.insert(QStringLiteral("title"), doc.title);
        item.insert(QStringLiteral("tool"), doc.tool);
        item.insert(QStringLiteral("zoom"), doc.zoom);
        item.insert(QStringLiteral("hasView"), doc.hasView);
        item.insert(QStringLiteral("centerX"), doc.center.x());
        item.insert(QStringLiteral("centerY"), doc.center.y());
        documents.append(item);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), kFormatName);
    root.insert(QStringLiteral("version"), FormatVersion);
    root.insert(QStringLiteral("active"), data.activeId);
    root.insert(QStringLiteral("documents"), documents);
    root.insert(QStringLiteral("ui"), QJsonObject::fromVariantMap(data.ui));
    root.insert(QStringLiteral("sha256"), QString::fromLatin1(digestOf(root)));
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush())
        return fail(error, QStringLiteral("The session file could not be written."));
#ifdef Q_OS_WIN
    const bool synced = _commit(file.handle()) == 0;
#else
    const bool synced = ::fsync(file.handle()) == 0;
#endif
    if (!synced) return fail(error, QStringLiteral("The session file could not be synchronized to disk."));
    if (!file.commit()) return fail(error, QStringLiteral("The session file could not be replaced."));
    return true;
}

}  // namespace komapaint
