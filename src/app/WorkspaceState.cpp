#include "WorkspaceState.h"

#include <QColor>
#include <algorithm>
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QDir>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QSet>
#include <memory>
#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

#include "FileAssociation.h"
#include "../core/Settings.h"
#include "../files/DocumentFile.h"

#include <QFileInfo>
#include <QUrl>

namespace komapaint {
namespace {
QByteArray fileDigest(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result().toHex();
}

// Reads and fully validates the workspace index (including its own checksum).
// A valid index is the only authority for which snapshots may be reused or
// removed; an invalid one is always retained untouched.
bool readWorkspaceIndex(const QDir& dir, QJsonArray* entries)
{
    QFile file(dir.filePath(QStringLiteral("workspace.json")));
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !json.isObject()) return false;
    auto root = json.object();
    const auto storedHash = root.take(QStringLiteral("sha256")).toString().toLatin1();
    if (storedHash.size() != 64) return false;
    if (storedHash != QCryptographicHash::hash(QJsonDocument(root).toJson(QJsonDocument::Compact),
                                               QCryptographicHash::Sha256).toHex()) return false;
    if (root.value(QStringLiteral("version")).toInt() != 1) return false;
    const auto projects = root.value(QStringLiteral("projects"));
    if (!projects.isArray()) return false;
    if (entries) *entries = projects.toArray();
    return true;
}
}


WorkspaceState::WorkspaceState(QObject* parent) : QObject(parent)
{
    connect(&m_recoveryTimer, &QTimer::timeout, this, [this] {
        if (!m_history->strokeActive()) persistWorkspace();
    });
    m_brush = new BrushSettings(this);
    m_document = new CanvasDocument(this);
    m_timeline = new TimelineState(this);
    m_colors   = new ColorPalette(this);
    m_history  = new UndoStack(this);
    m_creationService = new ProjectCreationService(this);
    m_tabs.push_back({m_document, m_history, {}, QUuid::createUuid(), {},
                      QString::fromLatin1(CanvasDocument::IllustrationTypeId)});
    connectLayerDeletion(m_document, m_history);
    connect(m_document, &CanvasDocument::titleChanged, this, &WorkspaceState::tabsChanged);
    connect(m_document, &CanvasDocument::modifiedChanged, this, &WorkspaceState::tabsChanged);
}

void WorkspaceState::connectLayerDeletion(const CanvasDocument* document, UndoStack* history)
{
    connect(document->layers(), &QAbstractItemModel::rowsAboutToBeRemoved, history, [history] {
        if (history->strokeActive()) history->cancelStroke();
        if (history->canUndo() || history->canRedo()) { history->clear(); emit history->errorOccurred(QStringLiteral("Artwork history was cleared because a layer was deleted.")); }
    });
}

QVariantList WorkspaceState::projectTabs() const
{
    QVariantList result;
    for (const auto& tab : m_tabs) {
        QVariantMap item;
        item.insert(QStringLiteral("title"), tab.document->title());
        item.insert(QStringLiteral("modified"), tab.document->isModified());
        item.insert(QStringLiteral("id"), tab.id.toString(QUuid::WithoutBraces));
        result.append(item);
    }
    return result;
}

bool WorkspaceState::activateTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return false;
    if (index == m_activeTab) return true;
    if (m_history->strokeActive()) {
        emit fileErrorOccurred(QStringLiteral("Finish or cancel the current stroke before switching projects."));
        return false;
    }
    m_activeTab = index;
    const auto& tab = m_tabs[static_cast<std::size_t>(index)];
    m_document = tab.document; m_history = tab.history; m_currentFile = tab.path;
    emit documentChanged(); emit currentFileChanged(); emit tabsChanged();
    return true;
}

void WorkspaceState::addDocument(CanvasDocument* document, const QString& path)
{
    document->setParent(this);
    auto* history = new UndoStack(this);
    connectLayerDeletion(document, history);
    connect(document, &CanvasDocument::titleChanged, this, &WorkspaceState::tabsChanged);
    connect(document, &CanvasDocument::modifiedChanged, this, &WorkspaceState::tabsChanged);
    if (m_initialPlaceholder && m_tabs.size() == 1 && !m_document->isModified()
        && m_currentFile.isEmpty() && !m_history->canUndo() && !m_history->canRedo()) {
        auto old = m_tabs.front();
        m_tabs.clear();
        old.history->deleteLater(); old.document->deleteLater();
        m_activeTab = -1;
    }
    m_initialPlaceholder = false;
    const auto* adapter = DocumentRecoveryRegistry::instance().adapterForDocument(*document);
    m_tabs.push_back({document, history, path, QUuid::createUuid(),
                      path.isEmpty() ? QByteArray{} : fileDigest(path),
                      adapter ? adapter->typeId() : document->documentType()});
    activateTab(static_cast<int>(m_tabs.size()) - 1);
}

bool WorkspaceState::closeTab(int index, bool discard)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()) || m_history->strokeActive()) return false;
    const auto tab = m_tabs[static_cast<std::size_t>(index)];
    if (tab.history->strokeActive() || (tab.document->isModified() && !discard)) return false;
    m_tabs.erase(m_tabs.begin() + index);
    tab.history->deleteLater(); tab.document->deleteLater();
    if (m_tabs.empty()) {
        auto* blank = new CanvasDocument(this);
        auto* history = new UndoStack(this);
        m_tabs.push_back({blank, history, {}, QUuid::createUuid(), {},
                          QString::fromLatin1(CanvasDocument::IllustrationTypeId)});
        connectLayerDeletion(blank, history);
        connect(blank, &CanvasDocument::titleChanged, this, &WorkspaceState::tabsChanged);
        connect(blank, &CanvasDocument::modifiedChanged, this, &WorkspaceState::tabsChanged);
        m_initialPlaceholder = true;
        m_activeTab = -1;
        return activateTab(0);
    }
    int next = m_activeTab;
    if (index < next) --next;
    else if (index == next) next = std::min(index, static_cast<int>(m_tabs.size()) - 1);
    m_activeTab = -1;
    return activateTab(next);
}

static QString localPathFromQml(const QString& input)
{
    if (input.isEmpty()) return {};
    // Windows drive-letter ("C:/...") and UNC ("\\\\server\\share") paths are
    // plain filesystem paths, not URLs — QUrl would parse "c:" as a scheme.
    if (input.size() >= 3 && input[1] == u':' && input[0].isLetter() && (input[2] == u'/' || input[2] == u'\\'))
        return input;
    if (input.startsWith(QStringLiteral("\\\\"))) return input;
    const QUrl url(input);
    if (url.isLocalFile()) return url.toLocalFile();
    if (url.scheme().isEmpty()) return input; // relative path or bare filename
    return {}; // remote or unsupported scheme
}

void WorkspaceState::associateWithKomaFiles()
{
#if defined(Q_OS_WIN)
    const QString exePath = FileAssociation::currentExecutablePath();
    if (exePath.isEmpty()) {
        emit fileAssociationCompleted(QStringLiteral("Could not determine where KomaPaint is installed."), false);
        return;
    }
    QString error;
    if (FileAssociation::registerAssociation(exePath, &error)) {
        emit fileAssociationCompleted(QStringLiteral("Double-clicking a .koma file will now open it in this KomaPaint."), true);
        return;
    }
    emit fileAssociationCompleted(error, false);
#else
    emit fileAssociationCompleted(QStringLiteral("The .koma file association is only supported on Windows."), false);
#endif
}

void WorkspaceState::replaceDocument(CanvasDocument* next)
{
    addDocument(next);
}

bool WorkspaceState::newDocument(int width, int height, bool transparent, const QColor& background)
{
    if (m_history->strokeActive()) return false;
    if (!CanvasDocument::validSize(QSize(width, height)) || !background.isValid()
        || background.alpha() != 255) return false;
    auto* next = new CanvasDocument(this);
    next->setSize(QSize(width, height));
    next->setBackgroundColor(background);
    next->setTransparentBackground(transparent);
    next->setModified(false);
    replaceDocument(next);
    return true;
}

bool WorkspaceState::newProject(const QString& name, int width, int height, bool transparent,
                                const QString& backgroundColor, qreal dpi)
{
    qDebug() << "[NewProject] WorkspaceState::newProject() called:"
             << "name=" << name << "width=" << width << "height=" << height
             << "transparent=" << transparent << "bgColor=" << backgroundColor << "dpi=" << dpi;

    if (m_history->strokeActive()) {
        m_creationError = QStringLiteral("Finish or cancel the current stroke before creating a project.");
        emit creationErrorChanged();
        return false;
    }

    // Delegates validation and construction to the creation service so the
    // current project is never left in a partially-constructed state.
    auto* doc = m_creationService->createDocument(name, width, height, transparent,
                                                  backgroundColor, dpi);
    if (!doc) {
        m_creationError = m_creationService->creationError();
        qDebug() << "[NewProject] REJECTED:" << m_creationError;
        emit creationErrorChanged();
        return false;
    }

    qDebug() << "[NewProject] CanvasDocument created:" << doc
             << "size=" << doc->width() << "x" << doc->height()
             << "dpi=" << doc->dpi() << "transparent=" << doc->hasTransparentBackground();

    m_creationError = QString();
    emit creationErrorChanged();

    doc->setParent(this);
    replaceDocument(doc);
    qDebug() << "[NewProject] replaceDocument done, returning true";
    return true;
}

bool WorkspaceState::saveToPath(const QString& pathOrUrl)
{
    if (m_history->strokeActive()) { emit fileErrorOccurred(QStringLiteral("Finish or cancel the current stroke before saving.")); return false; }
    QString path;
    if (pathOrUrl.isEmpty()) {
        // Bare save: reuse the current file if there is one.
        path = m_currentFile;
        if (path.isEmpty()) { emit fileErrorOccurred(QStringLiteral("No filename was given.")); return false; }
    } else {
        path = localPathFromQml(pathOrUrl);
        if (path.isEmpty()) { emit fileErrorOccurred(QStringLiteral("\"%1\" is not a local file path.").arg(pathOrUrl)); return false; }
    }
    // Store an absolute path so recovery can re-associate the tab after a
    // restart from a different working directory.
    path = QFileInfo(path).absoluteFilePath();
    QString error;
    if (!DocumentFile::save(*m_document, path, &error)) { emit fileErrorOccurred(error); return false; }
    const bool changed = m_currentFile != path;
    m_currentFile = path;
    m_tabs[static_cast<std::size_t>(m_activeTab)].path = path;
    m_tabs[static_cast<std::size_t>(m_activeTab)].savedHash = fileDigest(path);
    if (m_document->title().isEmpty() || m_document->title() == QStringLiteral("Untitled"))
        m_document->setTitle(QFileInfo(path).completeBaseName());
    m_document->setModified(false);
    if (changed) emit currentFileChanged();
    return true;
}

bool WorkspaceState::openFromPath(const QString& pathOrUrl)
{
    if (m_history->strokeActive()) { emit fileErrorOccurred(QStringLiteral("Finish or cancel the current stroke before opening a file.")); return false; }
    const QString requested = localPathFromQml(pathOrUrl);
    if (requested.isEmpty()) { emit fileErrorOccurred(QStringLiteral("No readable filename was given.")); return false; }
    const QString path = QFileInfo(requested).absoluteFilePath();
    QString error;
    CanvasDocument* next = DocumentFile::open(path, &error);
    if (!next) { emit fileErrorOccurred(error); return false; }
    addDocument(next, path);
    return true;
}

bool WorkspaceState::initializeRecovery(const QString& directory)
{
    if (directory.isEmpty() || !QDir().mkpath(directory)) return false;
    auto lock = std::make_unique<QLockFile>(QDir(directory).filePath(QStringLiteral("workspace.lock")));
    if (!lock->tryLock(0)) return false;
    m_recoveryDirectory = directory;
    m_recoveryLock = std::move(lock);
    m_recoveryAvailable = QFile::exists(QDir(directory).filePath(QStringLiteral("workspace.json")));
    const QDir recoveryDir(directory);
    QJsonArray entries;
    if (readWorkspaceIndex(recoveryDir, &entries)) {
        // A valid index is the newest recovery state. Snapshots it does not
        // reference are leftovers from an interrupted snapshot write or a
        // completed index replacement; they are safe to remove now. If the
        // index is invalid nothing is deleted, so its data stays recoverable.
        QSet<QString> referenced;
        for (const auto& value : entries)
            referenced.insert(value.toObject().value(QStringLiteral("snapshot")).toString());
        for (const auto& info : recoveryDir.entryInfoList({QStringLiteral("*.koma")}, QDir::Files)) {
            if (!referenced.contains(info.fileName())) QFile::remove(info.absoluteFilePath());
        }
    }
    emit recoveryAvailableChanged();
    return true;
}

void WorkspaceState::setRecoveryInterval(int minutes)
{
    m_recoveryTimer.stop();
    if (minutes == 5 || minutes == 10 || minutes == 15 || minutes == 30 || minutes == 60)
        m_recoveryTimer.start(minutes * 60000);
}

bool WorkspaceState::persistWorkspace(const QStringList& discardIds, const QStringList& excludeIds)
{
    if (!m_recoveryLock) {
        emit fileErrorOccurred(QStringLiteral("The workspace recovery location is unavailable. The previous recovery state has not been changed."));
        return false;
    }
    if (m_recoveryAvailable) {
        emit fileErrorOccurred(QStringLiteral("An earlier recovery decision is still pending. Restore or discard it before new recovery snapshots are written; the previous recovery state has not been changed."));
        return false;
    }
    if (m_history->strokeActive()) {
        emit fileErrorOccurred(QStringLiteral("Finish or cancel the current stroke before saving recovery snapshots."));
        return false;
    }
    QDir dir(m_recoveryDirectory);
    const QString manifestPath = dir.filePath(QStringLiteral("workspace.json"));
    QJsonArray oldEntries;
    const bool hadValidIndex = readWorkspaceIndex(dir, &oldEntries);
    QStringList created;
    auto fail = [&](const QString& message) {
        for (const auto& path : created) QFile::remove(path);
        emit fileErrorOccurred(message + QStringLiteral(" The previous recovery state was retained."));
        return false;
    };
    QJsonArray entries;
    QString activeId;
    for (std::size_t i = 0; i < m_tabs.size(); ++i) {
        const auto& tab = m_tabs[i];
        const auto id = tab.id.toString(QUuid::WithoutBraces);
        if (excludeIds.contains(id)) continue;
        const bool discard = discardIds.contains(id);
        if (discard && tab.path.isEmpty()) continue;
        const auto* adapter = DocumentRecoveryRegistry::instance().adapterForType(tab.typeId);
        if (!adapter)
            return fail(QStringLiteral("The document type \"%1\" has no recovery serializer in this build.").arg(tab.typeId));
        std::unique_ptr<CanvasDocument> saved;
        const CanvasDocument* source = tab.document;
        QString error;
        if (discard) {
            saved.reset(adapter->open(tab.path, &error));
            if (!saved) return fail(QStringLiteral("Could not reopen the saved project to discard its changes: ") + error);
            if (!adapter->supports(*saved) || saved->documentType() != tab.typeId)
                return fail(QStringLiteral("The saved project has a different document type than its recovery tab."));
            source = saved.get();
        }
        const auto name = id + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".koma");
        const auto path = dir.filePath(name);
        if (m_recoveryFailureForTests == 1) return fail(QStringLiteral("Recovery snapshot write failed (injected)."));
        created.append(path);
        if (!adapter->save(*source, path, &error)) return fail(QStringLiteral("Could not write a recovery snapshot: ") + error);
        const auto digest = fileDigest(path);
        std::unique_ptr<CanvasDocument> verified(adapter->open(path, &error));
        if (digest.isEmpty() || !verified || verified->documentType() != tab.typeId || !adapter->supports(*verified))
            return fail(QStringLiteral("Could not validate a recovery snapshot: ") + error);
        QJsonObject item;
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("type"), tab.typeId);
        item.insert(QStringLiteral("snapshot"), name);
        item.insert(QStringLiteral("sha256"), QString::fromLatin1(digest));
        item.insert(QStringLiteral("path"), tab.path);
        const auto origin = discard ? fileDigest(tab.path) : tab.savedHash;
        item.insert(QStringLiteral("originSha256"), QString::fromLatin1(origin));
        item.insert(QStringLiteral("modified"), discard ? false : tab.document->isModified());
        entries.append(item);
        if (static_cast<int>(i) == m_activeTab) activeId = id;
    }
    if (activeId.isEmpty() && !entries.isEmpty()) activeId = entries.first().toObject().value(QStringLiteral("id")).toString();
    QJsonObject manifest;
    manifest.insert(QStringLiteral("version"), 1);
    manifest.insert(QStringLiteral("active"), activeId);
    manifest.insert(QStringLiteral("activeTool"), m_activeTool);
    manifest.insert(QStringLiteral("zoom"), m_zoom);
    manifest.insert(QStringLiteral("projects"), entries);
    const auto manifestHash = QCryptographicHash::hash(QJsonDocument(manifest).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
    manifest.insert(QStringLiteral("sha256"), QString::fromLatin1(manifestHash));
    const auto bytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    QSaveFile file(manifestPath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush())
        return fail(QStringLiteral("Could not write the workspace recovery index."));
#ifdef Q_OS_WIN
    if (_commit(file.handle()) != 0) return fail(QStringLiteral("Could not synchronize the workspace recovery index."));
#else
    if (::fsync(file.handle()) != 0) return fail(QStringLiteral("Could not synchronize the workspace recovery index."));
#endif
    if (m_recoveryFailureForTests == 2) return fail(QStringLiteral("Recovery index replacement failed (injected)."));
    if (!file.commit()) return fail(QStringLiteral("Could not replace the workspace recovery index."));
    // Remove only files explicitly referenced by the previous committed index,
    // and only after the new one is committed. Never follow paths out of the
    // directory. A corrupt previous index is never used as a deletion list.
    if (hadValidIndex) {
        for (const auto& value : oldEntries) {
            const auto name = value.toObject().value(QStringLiteral("snapshot")).toString();
            if (!name.isEmpty() && QFileInfo(name).fileName() == name && name.endsWith(QStringLiteral(".koma")))
                QFile::remove(dir.filePath(name));
        }
    }
    return true;
}

bool WorkspaceState::restoreWorkspace()
{
    if (!m_recoveryLock || !m_recoveryAvailable || m_history->strokeActive()) return false;
    QDir dir(m_recoveryDirectory);
    QFile file(dir.filePath(QStringLiteral("workspace.json")));
    QJsonParseError parse;
    if (!file.open(QIODevice::ReadOnly)) { emit fileErrorOccurred(QStringLiteral("Cannot read the workspace recovery index.")); return false; }
    const auto json = QJsonDocument::fromJson(file.readAll(), &parse);
    auto root = json.object();
    const auto indexHash = root.take(QStringLiteral("sha256")).toString().toLatin1();
    const bool hashValid = indexHash.size() == 64 && indexHash == QCryptographicHash::hash(QJsonDocument(root).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
    const auto entriesValue = root.value(QStringLiteral("projects"));
    if (parse.error != QJsonParseError::NoError || !json.isObject() || root.value(QStringLiteral("version")).toInt() != 1 || !entriesValue.isArray() || !hashValid) {
        emit fileErrorOccurred(QStringLiteral("The workspace recovery index is invalid. It has been retained.")); return false;
    }
    struct Loaded { std::unique_ptr<CanvasDocument> doc; QString path; QUuid id; QByteArray origin; bool modified; };
    std::vector<Loaded> loaded;
    QSet<QUuid> ids;
    bool detached = false;
    QString error;
    for (const auto& value : entriesValue.toArray()) {
        const auto item = value.toObject();
        const QUuid id(item.value(QStringLiteral("id")).toString());
        const auto name = item.value(QStringLiteral("snapshot")).toString();
        const auto digest = item.value(QStringLiteral("sha256")).toString().toLatin1();
        const auto typeId = item.value(QStringLiteral("type")).toString();
        const auto* adapter = DocumentRecoveryRegistry::instance().adapterForType(typeId);
        if (!value.isObject() || id.isNull() || ids.contains(id) || !adapter
            || QFileInfo(name).fileName() != name || !name.startsWith(id.toString(QUuid::WithoutBraces) + QLatin1Char('-'))
            || !name.endsWith(QStringLiteral(".koma")) || digest.size() != 64 || fileDigest(dir.filePath(name)) != digest) {
            emit fileErrorOccurred(QStringLiteral("A recovery project has an invalid or unsupported type, identity or checksum. No projects were restored; recovery data was retained.")); return false;
        }
        ids.insert(id);
        auto doc = std::unique_ptr<CanvasDocument>(adapter->open(dir.filePath(name), &error));
        if (!doc) { emit fileErrorOccurred(QStringLiteral("A recovery project could not be opened: ") + error); return false; }
        if (!adapter->supports(*doc) || doc->documentType() != typeId) {
            emit fileErrorOccurred(QStringLiteral("A recovery project does not match its recorded document type. No projects were restored; recovery data was retained.")); return false;
        }
        auto path = item.value(QStringLiteral("path")).toString();
        const auto origin = item.value(QStringLiteral("originSha256")).toString().toLatin1();
        bool modified = item.value(QStringLiteral("modified")).toBool();
        if (!path.isEmpty() && (origin.size() != 64 || fileDigest(path) != origin)) {
            path.clear(); modified = true; detached = true;
        }
        loaded.push_back({std::move(doc), path, id, origin, modified});
    }
    const auto activeTool = root.value(QStringLiteral("activeTool")).toString();
    const auto zoom = root.value(QStringLiteral("zoom")).toDouble();
    const QUuid active(root.value(QStringLiteral("active")).toString());
    if ((!loaded.empty() && !ids.contains(active)) || (loaded.empty() && !active.isNull())) {
        emit fileErrorOccurred(QStringLiteral("The workspace recovery index has an invalid active project. Recovery data was retained.")); return false;
    }
    // No live document changes until every snapshot has passed validation.
    for (auto& item : loaded) {
        item.doc->setModified(item.modified);
        auto* document = item.doc.release();
        addDocument(document, item.path);
        auto& tab = m_tabs.back();
        tab.id = item.id; tab.savedHash = item.origin;
    }
    for (std::size_t i = 0; i < m_tabs.size(); ++i) if (m_tabs[i].id == active) { activateTab(static_cast<int>(i)); break; }
    if (!activeTool.isEmpty()) setActiveTool(activeTool);
    if (zoom >= 6.25 && zoom <= 6400.0) setZoom(zoom);
    m_recoveryAvailable = false;
    emit recoveryAvailableChanged(); emit tabsChanged();
    if (detached) emit fileErrorOccurred(QStringLiteral("A recovered project's original file has changed or is missing. The recovered artwork was opened without a filename; use Save As to avoid overwriting that file."));
    return true;
}

bool WorkspaceState::discardRecovery()
{
    if (!m_recoveryLock || !m_recoveryAvailable) return false;
    QDir dir(m_recoveryDirectory);
    QStringList snapshots;
    QFile index(dir.filePath(QStringLiteral("workspace.json")));
    if (index.open(QIODevice::ReadOnly)) {
        for (const auto& value : QJsonDocument::fromJson(index.readAll()).object().value(QStringLiteral("projects")).toArray()) {
            const auto name = value.toObject().value(QStringLiteral("snapshot")).toString();
            if (!name.isEmpty() && QFileInfo(name).fileName() == name && name.endsWith(QStringLiteral(".koma")))
                snapshots.append(name);
        }
    }
    index.close();
    if (!QFile::remove(dir.filePath(QStringLiteral("workspace.json")))) {
        emit fileErrorOccurred(QStringLiteral("Could not discard the old recovery index. It has been retained.")); return false;
    }
    // Snapshot files are removed only after the index that referred to them is
    // gone, so a failed discard never leaves a dangling recovery entry.
    for (const auto& name : snapshots) QFile::remove(dir.filePath(name));
    m_recoveryAvailable = false; emit recoveryAvailableChanged(); return true;
}

void WorkspaceState::setActiveTool(const QString& tool)
{
    if (m_activeTool == tool) return;
    m_activeTool = tool;
    emit activeToolChanged(m_activeTool);
}

void WorkspaceState::setZoom(qreal z)
{
    z = qBound(6.25, z, 6400.0);
    if (qFuzzyCompare(z, m_zoom)) return;
    m_zoom = z;
    emit zoomChanged(z);
}

void WorkspaceState::zoomIn()  { setZoom(m_zoom * 1.25); }
void WorkspaceState::zoomOut() { setZoom(m_zoom / 1.25); }
void WorkspaceState::resetZoom() { setZoom(100.0); }
void WorkspaceState::zoomFit()   { setZoom(100.0); /* will be screen-fit in later phase */ }

QString WorkspaceState::diagnosticLogPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return dir + QStringLiteral("/KomaPaint/NewProjectDiagnostic.log");
}

void WorkspaceState::writeDiagnosticLog(const QString& message)
{
    const QString path = diagnosticLogPath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
    QTextStream out(&file);
    out << timestamp << QStringLiteral("  ") << message << Qt::endl;
    file.close();
}

void WorkspaceState::clearDiagnosticLog()
{
    QFile file(diagnosticLogPath());
    if (file.exists()) file.remove();
}

}  // namespace komapaint
