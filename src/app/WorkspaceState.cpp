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

#include <QHash>
#include <QRegularExpression>
#include <QStorageInfo>
#include <cmath>
#include <new>

namespace komapaint {
namespace {

const QString kIndexName = QStringLiteral("workspace.json");
// Index schema. Version 1 kept one tool/zoom for the whole window; version 2
// records title, snapshot time and view state per project. Both are read.
constexpr int kIndexVersion = 2;

QByteArray fileDigest(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result().toHex();
}

// A snapshot name is always "<tab uuid>-<snapshot uuid>.koma": a bare file
// name, so an index can never point outside the recovery folder.
bool strictSnapshotName(const QString& name)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"
        "-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.koma$"));
    return pattern.match(name).hasMatch();
}

bool bareSnapshotName(const QString& name)
{
    return !name.isEmpty() && QFileInfo(name).fileName() == name && name.endsWith(QStringLiteral(".koma"));
}

struct IndexData {
    QList<QJsonObject> entries;
    QString activeId;
    int version = 0;
};

// Reads and fully validates the workspace index (including its own checksum).
// A valid index is the only authority for which snapshots may be reused or
// removed; an invalid one is never used as a deletion list.
bool readWorkspaceIndex(const QDir& dir, IndexData* out)
{
    QFile file(dir.filePath(kIndexName));
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !json.isObject()) return false;
    auto root = json.object();
    const auto storedHash = root.take(QStringLiteral("sha256")).toString().toLatin1();
    if (storedHash.size() != 64) return false;
    if (storedHash != QCryptographicHash::hash(QJsonDocument(root).toJson(QJsonDocument::Compact),
                                               QCryptographicHash::Sha256).toHex()) return false;
    const int version = root.value(QStringLiteral("version")).toInt();
    if (version != 1 && version != kIndexVersion) return false;
    const auto projects = root.value(QStringLiteral("projects"));
    if (!projects.isArray()) return false;
    IndexData data;
    data.version = version;
    data.activeId = root.value(QStringLiteral("active")).toString();
    for (const auto& value : projects.toArray()) {
        auto item = value.toObject();
        if (version == 1) {
            // Version 1 stored one tool and zoom for the window; give every
            // project that state so nothing is lost when it is recovered.
            if (!item.contains(QStringLiteral("tool")))
                item.insert(QStringLiteral("tool"), root.value(QStringLiteral("activeTool")));
            if (!item.contains(QStringLiteral("zoom")))
                item.insert(QStringLiteral("zoom"), root.value(QStringLiteral("zoom")));
        }
        data.entries.append(item);
    }
    if (out) *out = std::move(data);
    return true;
}

QString cleanPath(const QString& path)
{
    if (path.isEmpty()) return {};
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();  // empty when the file is missing
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool samePath(const QString& a, const QString& b)
{
    if (a.isEmpty() || b.isEmpty()) return false;
#ifdef Q_OS_WIN
    return cleanPath(a).compare(cleanPath(b), Qt::CaseInsensitive) == 0;
#else
    return cleanPath(a) == cleanPath(b);
#endif
}

QString readableBytes(quint64 bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
    if (bytes >= 1024ULL * 1024ULL)
        return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 0);
}

}  // namespace


WorkspaceState::WorkspaceState(QObject* parent) : QObject(parent)
{
    connect(&m_recoveryTimer, &QTimer::timeout, this, [this] {
        if (m_history->strokeActive()) return;
        QString error;
        if (persistInternal({}, {}, &error)) { m_lastTimerRecoveryError.clear(); return; }
        // A background snapshot that keeps failing for the same reason (for
        // example a full disk) is reported once, not every few minutes.
        if (error.isEmpty() || error == m_lastTimerRecoveryError) return;
        m_lastTimerRecoveryError = error;
        emit fileErrorOccurred(error);
    });
    m_brush = new BrushSettings(this);
    m_document = new CanvasDocument(this);
    m_timeline = new TimelineState(this);
    m_colors   = new ColorPalette(this);
    m_history  = new UndoStack(this);
    m_creationService = new ProjectCreationService(this);
    m_tabs.push_back(makeTab(m_document, m_history, {}));
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

WorkspaceState::Tab WorkspaceState::makeTab(CanvasDocument* document, UndoStack* history, const QString& path) const
{
    Tab tab;
    tab.document = document;
    tab.history = history;
    tab.path = path;
    tab.id = QUuid::createUuid();
    tab.savedHash = path.isEmpty() ? QByteArray{} : fileDigest(path);
    const auto* adapter = DocumentRecoveryRegistry::instance().adapterForDocument(*document);
    tab.typeId = adapter ? adapter->typeId() : document->documentType();
    // A new project starts with the tool the user is holding; from then on
    // the tool belongs to the project.
    tab.tool = m_activeTool;
    return tab;
}

int WorkspaceState::tabIndexForPath(const QString& path) const
{
    for (std::size_t i = 0; i < m_tabs.size(); ++i)
        if (samePath(m_tabs[i].path, path)) return static_cast<int>(i);
    return -1;
}

int WorkspaceState::tabIndexForId(const QUuid& id) const
{
    if (id.isNull()) return -1;
    for (std::size_t i = 0; i < m_tabs.size(); ++i)
        if (m_tabs[i].id == id) return static_cast<int>(i);
    return -1;
}

bool WorkspaceState::isPristinePlaceholder(const Tab& tab) const
{
    return m_initialPlaceholder && m_tabs.size() == 1 && !tab.document->isModified()
        && tab.path.isEmpty() && !tab.history->canUndo() && !tab.history->canRedo();
}

// A project needs a recovery snapshot when it holds work that is in no saved
// file: it was never saved, or it changed since its last save. A saved,
// unchanged project is already safe in its own .koma file.
bool WorkspaceState::tabNeedsSnapshot(const Tab& tab) const
{
    if (isPristinePlaceholder(tab)) return false;
    return tab.path.isEmpty() || tab.document->isModified();
}

QVariantList WorkspaceState::projectTabs() const
{
    QVariantList result;
    for (const auto& tab : m_tabs) {
        QVariantMap item;
        item.insert(QStringLiteral("title"), tab.document->title());
        item.insert(QStringLiteral("modified"), tab.document->isModified());
        item.insert(QStringLiteral("id"), tab.id.toString(QUuid::WithoutBraces));
        item.insert(QStringLiteral("path"), tab.path);
        result.append(item);
    }
    return result;
}

// Points the workspace-facing properties at the active tab. Nothing is copied
// between tabs: the properties are views onto the tab that is now active.
void WorkspaceState::applyActiveTab()
{
    const auto& tab = m_tabs[static_cast<std::size_t>(m_activeTab)];
    m_document = tab.document;
    m_history = tab.history;
    m_currentFile = tab.path;
    const bool toolChanged = m_activeTool != tab.tool;
    const bool zoomChanged_ = !qFuzzyCompare(m_zoom, tab.zoom);
    m_activeTool = tab.tool;
    m_zoom = tab.zoom;
    emit documentChanged();
    emit currentFileChanged();
    emit tabsChanged();
    if (toolChanged) emit activeToolChanged(m_activeTool);
    if (zoomChanged_) emit zoomChanged(m_zoom);
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
    applyActiveTab();
    return true;
}

void WorkspaceState::addDocument(CanvasDocument* document, const QString& path,
                                 const std::function<void(Tab&)>& prepare)
{
    document->setParent(this);
    auto* history = new UndoStack(this);
    connectLayerDeletion(document, history);
    connect(document, &CanvasDocument::titleChanged, this, &WorkspaceState::tabsChanged);
    connect(document, &CanvasDocument::modifiedChanged, this, &WorkspaceState::tabsChanged);
    Tab tab = makeTab(document, history, path);
    if (prepare) prepare(tab);
    if (m_tabs.size() == 1 && isPristinePlaceholder(m_tabs.front())) {
        auto old = m_tabs.front();
        m_tabs.clear();
        old.history->deleteLater(); old.document->deleteLater();
        m_activeTab = -1;
    }
    m_initialPlaceholder = false;
    m_tabs.push_back(tab);
    activateTab(static_cast<int>(m_tabs.size()) - 1);
    saveSession();
}

bool WorkspaceState::closeTab(int index, bool discard)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()) || m_history->strokeActive()) return false;
    const auto tab = m_tabs[static_cast<std::size_t>(index)];
    if (tab.history->strokeActive() || (tab.document->isModified() && !discard)) return false;
    m_tabs.erase(m_tabs.begin() + index);
    tab.history->deleteLater(); tab.document->deleteLater();
    bool activated = false;
    if (m_tabs.empty()) {
        auto* blank = new CanvasDocument(this);
        auto* history = new UndoStack(this);
        m_tabs.push_back(makeTab(blank, history, {}));
        connectLayerDeletion(blank, history);
        connect(blank, &CanvasDocument::titleChanged, this, &WorkspaceState::tabsChanged);
        connect(blank, &CanvasDocument::modifiedChanged, this, &WorkspaceState::tabsChanged);
        m_initialPlaceholder = true;
        m_activeTab = -1;
        activated = activateTab(0);
    } else {
        int next = m_activeTab;
        if (index < next) --next;
        else if (index == next) next = std::min(index, static_cast<int>(m_tabs.size()) - 1);
        m_activeTab = -1;
        activated = activateTab(next);
    }
    // The project is closed: it was saved, or the user explicitly chose not
    // to keep its changes. Its recovery snapshot is now obsolete. A failure
    // here only leaves an extra recovery entry behind, never loses work.
    QString ignored;
    dropRecoveryEntriesForTabs({tab.id.toString(QUuid::WithoutBraces)}, &ignored);
    saveSession();
    return activated;
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
    // Two tabs must never be bound to one file: saving one would silently
    // replace what the other believes is on disk.
    const int other = tabIndexForPath(path);
    if (other >= 0 && other != m_activeTab) {
        emit fileErrorOccurred(QStringLiteral("\"%1\" is open in another tab. Choose a different name, or close that tab first.")
                                   .arg(QFileInfo(path).fileName()));
        return false;
    }
    QString error;
    if (!DocumentFile::save(*m_document, path, &error)) { emit fileErrorOccurred(error); return false; }
    const bool changed = m_currentFile != path;
    m_currentFile = path;
    auto& tab = m_tabs[static_cast<std::size_t>(m_activeTab)];
    tab.path = path;
    tab.savedHash = fileDigest(path);
    if (m_document->title().isEmpty() || m_document->title() == QStringLiteral("Untitled"))
        m_document->setTitle(QFileInfo(path).completeBaseName());
    m_document->setModified(false);
    if (changed) emit currentFileChanged();
    emit tabsChanged();
    // The save succeeded, so this project's recovery snapshot is obsolete.
    // It was kept until this point on purpose.
    QString ignored;
    dropRecoveryEntriesForTabs({tab.id.toString(QUuid::WithoutBraces)}, &ignored);
    saveSession();
    return true;
}

bool WorkspaceState::openFromPath(const QString& pathOrUrl)
{
    if (m_history->strokeActive()) { emit fileErrorOccurred(QStringLiteral("Finish or cancel the current stroke before opening a file.")); return false; }
    const QString requested = localPathFromQml(pathOrUrl);
    if (requested.isEmpty()) { emit fileErrorOccurred(QStringLiteral("No readable filename was given.")); return false; }
    const QString path = QFileInfo(requested).absoluteFilePath();
    // A file that is already open is shown, not opened a second time.
    const int existing = tabIndexForPath(path);
    if (existing >= 0) return activateTab(existing);
    QString error;
    CanvasDocument* next = DocumentFile::open(path, &error);
    if (!next) { emit fileErrorOccurred(error); return false; }
    addDocument(next, path);
    return true;
}

// ------------------------------------------------------------ view state

void WorkspaceState::storeActiveView(qreal zoom, qreal centerX, qreal centerY)
{
    if (m_activeTab < 0 || m_activeTab >= static_cast<int>(m_tabs.size())) return;
    if (!std::isfinite(zoom) || !std::isfinite(centerX) || !std::isfinite(centerY)) return;
    auto& tab = m_tabs[static_cast<std::size_t>(m_activeTab)];
    tab.viewCenter = QPointF(centerX, centerY);
    tab.hasView = true;
    setZoom(zoom);
}

bool WorkspaceState::activeViewStored() const
{
    return m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())
        && m_tabs[static_cast<std::size_t>(m_activeTab)].hasView;
}

QPointF WorkspaceState::activeViewCenter() const
{
    return activeViewStored() ? m_tabs[static_cast<std::size_t>(m_activeTab)].viewCenter : QPointF();
}

void WorkspaceState::setActiveTool(const QString& tool)
{
    if (m_activeTool == tool) return;
    m_activeTool = tool;
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()))
        m_tabs[static_cast<std::size_t>(m_activeTab)].tool = tool;
    emit activeToolChanged(m_activeTool);
}

void WorkspaceState::setZoom(qreal z)
{
    if (!std::isfinite(z)) return;
    z = qBound(6.25, z, 6400.0);
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()))
        m_tabs[static_cast<std::size_t>(m_activeTab)].zoom = z;
    if (qFuzzyCompare(z, m_zoom)) return;
    m_zoom = z;
    emit zoomChanged(z);
}

// --------------------------------------------------------------- recovery

bool WorkspaceState::initializeRecovery(const QString& directory)
{
    if (directory.isEmpty() || !QDir().mkpath(directory)) return false;
    auto lock = std::make_unique<QLockFile>(QDir(directory).filePath(QStringLiteral("workspace.lock")));
    if (!lock->tryLock(0)) return false;
    m_recoveryDirectory = directory;
    m_recoveryLock = std::move(lock);
    loadPendingRecovery();
    emit recoveryAvailableChanged();
    emit recoveryEntriesChanged();
    return true;
}

void WorkspaceState::loadPendingRecovery()
{
    m_pendingRecovery.clear();
    m_recoveryActiveId.clear();
    const QDir dir(m_recoveryDirectory);
    // A temporary file left by an interrupted write was never committed and
    // is never valid data. QSaveFile names them "<target>.XXXXXX".
    for (const auto& info : dir.entryInfoList({QStringLiteral("*.koma.*"), kIndexName + QStringLiteral(".*")}, QDir::Files))
        QFile::remove(info.absoluteFilePath());

    auto originalStateFor = [](const QString& path, const QByteArray& origin) {
        if (path.isEmpty()) return QString();
        if (!QFile::exists(path)) return QStringLiteral("missing");
        return origin.size() == 64 && fileDigest(path) == origin ? QStringLiteral("unchanged") : QStringLiteral("changed");
    };

    IndexData index;
    if (readWorkspaceIndex(dir, &index)) {
        // A valid index is the newest committed recovery state. Snapshots it
        // does not reference are leftovers from an interrupted pass or from a
        // completed index replacement; they are safe to remove now.
        QSet<QString> referenced;
        for (const auto& item : index.entries) referenced.insert(item.value(QStringLiteral("snapshot")).toString());
        for (const auto& info : dir.entryInfoList({QStringLiteral("*.koma")}, QDir::Files))
            if (!referenced.contains(info.fileName())) QFile::remove(info.absoluteFilePath());
        m_recoveryActiveId = index.activeId;
        int counter = 0;
        for (const auto& item : index.entries) {
            RecoveryEntry entry;
            entry.json = item;
            entry.key = item.value(QStringLiteral("snapshot")).toString();
            if (entry.key.isEmpty()) entry.key = QStringLiteral("entry-%1").arg(counter);
            ++counter;
            entry.tabId = QUuid(item.value(QStringLiteral("id")).toString());
            entry.typeId = item.value(QStringLiteral("type")).toString();
            entry.path = item.value(QStringLiteral("path")).toString();
            entry.title = item.value(QStringLiteral("title")).toString();
            if (entry.title.isEmpty())
                entry.title = entry.path.isEmpty() ? QStringLiteral("Unsaved project") : QFileInfo(entry.path).completeBaseName();
            const QFileInfo snapshot(dir.filePath(entry.key));
            entry.snapshotTime = QDateTime::fromString(item.value(QStringLiteral("savedAt")).toString(), Qt::ISODate);
            if (!entry.snapshotTime.isValid() && snapshot.exists()) entry.snapshotTime = snapshot.lastModified();
            entry.originalState = originalStateFor(entry.path, item.value(QStringLiteral("originSha256")).toString().toLatin1());
            if (bareSnapshotName(entry.key) && snapshot.exists()) entry.status = QStringLiteral("ready");
            else { entry.status = QStringLiteral("damaged"); entry.message = QStringLiteral("The recovery snapshot file is missing."); }
            m_pendingRecovery.push_back(entry);
        }
        return;
    }

    // No usable index (damaged, or the very first index was never written).
    // Nothing is deleted. Every complete snapshot file in the folder is
    // offered on its own, so a damaged index cannot hide recoverable work.
    // The original file's location is unknown here, so these always recover
    // as separate, unsaved projects.
    const auto files = dir.entryInfoList({QStringLiteral("*.koma")}, QDir::Files, QDir::Time | QDir::Reversed);
    for (const auto& info : files) {
        const QString name = info.fileName();
        if (!strictSnapshotName(name)) continue;
        RecoveryEntry entry;
        entry.key = name;
        entry.tabId = QUuid(name.left(36));
        entry.snapshotTime = info.lastModified();
        entry.status = QStringLiteral("damaged");
        entry.message = QStringLiteral("This recovery snapshot is incomplete or damaged.");
        entry.title = QStringLiteral("Unsaved project");
        for (const auto& typeId : DocumentRecoveryRegistry::instance().typeIds()) {
            const auto* adapter = DocumentRecoveryRegistry::instance().adapterForType(typeId);
            QString error;
            std::unique_ptr<CanvasDocument> document;
            try { document.reset(adapter->open(info.absoluteFilePath(), &error)); }
            catch (const std::bad_alloc&) { document.reset(); }
            if (!document || !adapter->supports(*document)) continue;
            entry.typeId = typeId;
            if (!document->title().isEmpty()) entry.title = document->title();
            entry.status = QStringLiteral("ready");
            entry.message = QStringLiteral("The recovery index was damaged, so the original file location is unknown.");
            break;
        }
        QJsonObject item;
        item.insert(QStringLiteral("id"), entry.tabId.toString(QUuid::WithoutBraces));
        item.insert(QStringLiteral("type"), entry.typeId);
        item.insert(QStringLiteral("snapshot"), name);
        item.insert(QStringLiteral("sha256"), QString::fromLatin1(fileDigest(info.absoluteFilePath())));
        item.insert(QStringLiteral("path"), QString());
        item.insert(QStringLiteral("originSha256"), QString());
        item.insert(QStringLiteral("modified"), true);
        item.insert(QStringLiteral("title"), entry.title);
        item.insert(QStringLiteral("savedAt"), entry.snapshotTime.toUTC().toString(Qt::ISODate));
        entry.json = item;
        m_pendingRecovery.push_back(entry);
    }
}

QVariantList WorkspaceState::recoveryEntries() const
{
    QVariantList result;
    for (const auto& entry : m_pendingRecovery) {
        QVariantMap item;
        item.insert(QStringLiteral("key"), entry.key);
        item.insert(QStringLiteral("title"), entry.title);
        item.insert(QStringLiteral("saved"), !entry.path.isEmpty());
        item.insert(QStringLiteral("originalPath"), QDir::toNativeSeparators(entry.path));
        item.insert(QStringLiteral("fileName"), entry.path.isEmpty() ? QString() : QFileInfo(entry.path).fileName());
        item.insert(QStringLiteral("originalState"), entry.originalState);
        item.insert(QStringLiteral("snapshotTime"), entry.snapshotTime.isValid()
                        ? entry.snapshotTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm")) : QString());
        item.insert(QStringLiteral("status"), entry.status);
        item.insert(QStringLiteral("message"), entry.message);
        result.append(item);
    }
    return result;
}

void WorkspaceState::setRecoveryInterval(int minutes)
{
    m_recoveryTimer.stop();
    if (!m_recoveryLock) return;
    if (minutes == 5 || minutes == 10 || minutes == 15 || minutes == 30 || minutes == 60)
        m_recoveryTimer.start(minutes * 60000);
}

qint64 WorkspaceState::recoveryStorageAvailable() const
{
    if (m_storageProbe) return m_storageProbe();
    const QStorageInfo storage(m_recoveryDirectory);
    return storage.isValid() && storage.isReady() ? storage.bytesAvailable() : -1;
}

bool WorkspaceState::commitRecoveryIndex(const QList<QJsonObject>& entries, const QString& activeId, QString* error)
{
    auto fail = [error](const QString& message) { if (error) *error = message; return false; };
    QJsonArray projects;
    for (const auto& item : entries) projects.append(item);
    QJsonObject manifest;
    manifest.insert(QStringLiteral("version"), kIndexVersion);
    manifest.insert(QStringLiteral("active"), activeId);
    manifest.insert(QStringLiteral("projects"), projects);
    const auto manifestHash = QCryptographicHash::hash(QJsonDocument(manifest).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
    manifest.insert(QStringLiteral("sha256"), QString::fromLatin1(manifestHash));
    const auto bytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    QSaveFile file(QDir(m_recoveryDirectory).filePath(kIndexName));
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
    return true;
}

bool WorkspaceState::persistWorkspace(const QStringList& discardIds, const QStringList& excludeIds)
{
    QString error;
    if (persistInternal(discardIds, excludeIds, &error)) return true;
    if (!error.isEmpty()) emit fileErrorOccurred(error);
    return false;
}

bool WorkspaceState::persistInternal(const QStringList& discardIds, const QStringList& excludeIds, QString* error)
{
    auto setError = [error](const QString& message) { if (error) *error = message; };
    if (!m_recoveryLock) {
        setError(QStringLiteral("The workspace recovery location is unavailable. The previous recovery state has not been changed."));
        return false;
    }
    if (m_history->strokeActive()) {
        setError(QStringLiteral("Finish or cancel the current stroke before saving recovery snapshots."));
        return false;
    }
    const QDir dir(m_recoveryDirectory);
    IndexData old;
    const bool hadValidIndex = readWorkspaceIndex(dir, &old);
    // Entries still waiting for a recovery decision are carried forward
    // untouched; the entries of live tabs are the ones this pass replaces.
    QSet<QString> pendingKeys;
    for (const auto& entry : m_pendingRecovery) pendingKeys.insert(entry.key);
    QHash<QString, QJsonObject> previousByTab;
    if (hadValidIndex) {
        for (const auto& item : old.entries) {
            if (pendingKeys.contains(item.value(QStringLiteral("snapshot")).toString())) continue;
            previousByTab.insert(item.value(QStringLiteral("id")).toString(), item);
        }
    }

    QStringList created;
    QStringList failures;
    QList<QJsonObject> entries;
    QString activeId;
    for (std::size_t i = 0; i < m_tabs.size(); ++i) {
        const auto& tab = m_tabs[i];
        const auto id = tab.id.toString(QUuid::WithoutBraces);
        // Explicitly given up by the user (Don't Save) or leaving the
        // workspace: no entry, and its old snapshot is removed after commit.
        if (discardIds.contains(id) || excludeIds.contains(id)) continue;
        if (!tabNeedsSnapshot(tab)) continue;
        const QString title = tab.document->title();
        // Each project is independent: when this one cannot be written, its
        // previous valid snapshot stays in the index and the others go on.
        auto keepPrevious = [&](const QString& reason) {
            failures.append(QStringLiteral("\"%1\": %2").arg(title, reason));
            const auto found = previousByTab.constFind(id);
            if (found == previousByTab.constEnd()) return;
            const auto name = found->value(QStringLiteral("snapshot")).toString();
            if (!bareSnapshotName(name) || !QFile::exists(dir.filePath(name))) return;
            entries.append(*found);
            if (static_cast<int>(i) == m_activeTab) activeId = id;
        };
        const auto* adapter = DocumentRecoveryRegistry::instance().adapterForType(tab.typeId);
        if (!adapter) {
            keepPrevious(QStringLiteral("the document type \"%1\" has no recovery serializer in this build.").arg(tab.typeId));
            continue;
        }
        // Do not start a snapshot that cannot fit: filling the disk would
        // also take away the room the user needs for a real Save.
        const quint64 needed = adapter->estimatedSnapshotBytes(*tab.document);
        const qint64 available = recoveryStorageAvailable();
        if (needed > 0 && available >= 0 && needed > static_cast<quint64>(available)) {
            keepPrevious(QStringLiteral("not enough free disk space for a recovery snapshot (about %1 needed, %2 free).")
                             .arg(readableBytes(needed), readableBytes(static_cast<quint64>(available))));
            continue;
        }
        if (m_recoveryFailureForTests == 1) {
            keepPrevious(QStringLiteral("recovery snapshot write failed (injected)."));
            continue;
        }
        const auto name = id + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".koma");
        const auto path = dir.filePath(name);
        QString detail;
        bool written = false;
        QByteArray digest;
        try {
            written = adapter->save(*tab.document, path, &detail);
            if (written) {
                // A snapshot only counts once it has been read back whole.
                digest = fileDigest(path);
                std::unique_ptr<CanvasDocument> verified(adapter->open(path, &detail));
                written = !digest.isEmpty() && verified && verified->documentType() == tab.typeId
                    && adapter->supports(*verified);
                if (!written && detail.isEmpty()) detail = QStringLiteral("the snapshot could not be validated.");
            }
        } catch (const std::bad_alloc&) {
            written = false;
            detail = QStringLiteral("not enough memory to write the recovery snapshot.");
        }
        if (!written) {
            QFile::remove(path);
            keepPrevious(QStringLiteral("could not write a recovery snapshot: ") + detail);
            continue;
        }
        created.append(path);
        QJsonObject item;
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("type"), tab.typeId);
        item.insert(QStringLiteral("snapshot"), name);
        item.insert(QStringLiteral("sha256"), QString::fromLatin1(digest));
        item.insert(QStringLiteral("path"), tab.path);
        item.insert(QStringLiteral("originSha256"), QString::fromLatin1(tab.savedHash));
        item.insert(QStringLiteral("modified"), tab.document->isModified());
        item.insert(QStringLiteral("title"), title);
        item.insert(QStringLiteral("savedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        item.insert(QStringLiteral("tool"), tab.tool);
        item.insert(QStringLiteral("zoom"), tab.zoom);
        item.insert(QStringLiteral("hasView"), tab.hasView);
        item.insert(QStringLiteral("centerX"), tab.viewCenter.x());
        item.insert(QStringLiteral("centerY"), tab.viewCenter.y());
        entries.append(item);
        if (static_cast<int>(i) == m_activeTab) activeId = id;
    }
    for (const auto& entry : m_pendingRecovery) entries.append(entry.json);

    const bool unchanged = hadValidIndex && created.isEmpty() && entries == old.entries && activeId == old.activeId;
    if (!unchanged) {
        QString commitError;
        if (!commitRecoveryIndex(entries, activeId, &commitError)) {
            for (const auto& path : created) QFile::remove(path);
            setError(commitError + QStringLiteral(" The previous recovery state was retained."));
            return false;
        }
        // Remove only files named by the previous committed index, only after
        // the new index is committed, and only when the new index no longer
        // refers to them. A damaged previous index is never a deletion list.
        if (hadValidIndex) {
            QSet<QString> kept;
            for (const auto& item : entries) kept.insert(item.value(QStringLiteral("snapshot")).toString());
            for (const auto& item : old.entries) {
                const auto name = item.value(QStringLiteral("snapshot")).toString();
                if (bareSnapshotName(name) && !kept.contains(name)) QFile::remove(dir.filePath(name));
            }
        }
    }
    saveSession();
    if (!failures.isEmpty()) {
        setError(QStringLiteral("Recovery snapshots could not be updated for:\n%1\nEarlier recovery data for these projects was kept. Your project files were not changed.")
                     .arg(failures.join(QLatin1Char('\n'))));
        return false;
    }
    return true;
}

bool WorkspaceState::dropRecoveryEntriesForTabs(const QStringList& tabIds, QString* error)
{
    if (!m_recoveryLock || tabIds.isEmpty()) return true;
    const QDir dir(m_recoveryDirectory);
    IndexData old;
    if (!readWorkspaceIndex(dir, &old)) return true;  // nothing this build may delete
    QSet<QString> pendingKeys;
    for (const auto& entry : m_pendingRecovery) pendingKeys.insert(entry.key);
    QList<QJsonObject> kept;
    QStringList obsolete;
    for (const auto& item : old.entries) {
        const auto name = item.value(QStringLiteral("snapshot")).toString();
        if (!pendingKeys.contains(name) && tabIds.contains(item.value(QStringLiteral("id")).toString()))
            obsolete.append(name);
        else
            kept.append(item);
    }
    if (obsolete.isEmpty()) return true;
    const QString activeId = tabIds.contains(old.activeId) ? QString() : old.activeId;
    if (!commitRecoveryIndex(kept, activeId, error)) return false;
    for (const auto& name : obsolete)
        if (bareSnapshotName(name)) QFile::remove(dir.filePath(name));
    return true;
}

bool WorkspaceState::recoverEntryInternal(const QString& key, QString* error, QString* notice)
{
    auto setError = [error](const QString& message) { if (error) *error = message; };
    if (!m_recoveryLock) { setError(QStringLiteral("The workspace recovery location is unavailable.")); return false; }
    if (m_history->strokeActive()) { setError(QStringLiteral("Finish or cancel the current stroke before recovering a project.")); return false; }
    auto found = std::find_if(m_pendingRecovery.begin(), m_pendingRecovery.end(),
                              [&key](const RecoveryEntry& entry) { return entry.key == key; });
    if (found == m_pendingRecovery.end()) { setError(QStringLiteral("That recovery entry no longer exists.")); return false; }
    const QString label = found->title;
    // A failed entry is marked, reported and kept. It never blocks the rest.
    auto damaged = [&](const QString& reason) {
        found->status = QStringLiteral("damaged");
        found->message = reason;
        emit recoveryEntriesChanged();
        setError(QStringLiteral("\"%1\" could not be recovered: %2 Its recovery data was kept.").arg(label, reason));
        return false;
    };
    const QJsonObject item = found->json;
    const QUuid id(item.value(QStringLiteral("id")).toString());
    const auto name = item.value(QStringLiteral("snapshot")).toString();
    const auto digest = item.value(QStringLiteral("sha256")).toString().toLatin1();
    const auto typeId = item.value(QStringLiteral("type")).toString();
    const auto* adapter = DocumentRecoveryRegistry::instance().adapterForType(typeId);
    if (!adapter)
        return damaged(QStringLiteral("it uses a document type this version of KomaPaint does not know."));
    if (id.isNull() || !bareSnapshotName(name) || !name.startsWith(id.toString(QUuid::WithoutBraces) + QLatin1Char('-'))
        || digest.size() != 64)
        return damaged(QStringLiteral("its recovery record has an invalid identity."));
    const QDir dir(m_recoveryDirectory);
    const QString snapshotPath = dir.filePath(name);
    if (!QFile::exists(snapshotPath)) return damaged(QStringLiteral("the recovery snapshot file is missing."));
    if (fileDigest(snapshotPath) != digest)
        return damaged(QStringLiteral("the recovery snapshot failed its checksum (it is incomplete or was changed)."));
    QString detail;
    std::unique_ptr<CanvasDocument> document;
    try { document.reset(adapter->open(snapshotPath, &detail)); }
    catch (const std::bad_alloc&) {
        // Not damage: the data is intact, there is just no room right now.
        setError(QStringLiteral("There is not enough memory to recover \"%1\" right now. Its recovery data was kept; close other projects and try again.").arg(label));
        return false;
    }
    if (!document) return damaged(QStringLiteral("the recovery snapshot could not be read (%1).").arg(detail));
    if (!adapter->supports(*document) || document->documentType() != typeId)
        return damaged(QStringLiteral("the recovery snapshot does not match its recorded document type."));

    // Decide whether the recovered work may stay attached to its file. The
    // original is never written here; attaching only means a later, explicit
    // Save by the user goes to that file.
    QString path = item.value(QStringLiteral("path")).toString();
    const auto origin = item.value(QStringLiteral("originSha256")).toString().toLatin1();
    bool modified = item.value(QStringLiteral("modified")).toBool(true);
    QString detachedBecause;
    if (!path.isEmpty()) {
        if (!QFile::exists(path)) detachedBecause = QStringLiteral("its original file \"%1\" is missing").arg(QFileInfo(path).fileName());
        else if (origin.size() != 64 || fileDigest(path) != origin)
            detachedBecause = QStringLiteral("its original file \"%1\" has changed since the snapshot was taken").arg(QFileInfo(path).fileName());
        else if (tabIndexForPath(path) >= 0)
            detachedBecause = QStringLiteral("its original file \"%1\" is already open in another tab").arg(QFileInfo(path).fileName());
    }
    if (!detachedBecause.isEmpty()) {
        path.clear();
        if (!document->title().endsWith(QStringLiteral("(recovered)")))
            document->setTitle(document->title() + QStringLiteral(" (recovered)"));
        if (notice)
            *notice = QStringLiteral("\"%1\" was recovered as a separate project because %2. Use Save As to keep it; the original file was not changed.")
                          .arg(label, detachedBecause);
    }
    // Work that has no file of its own exists nowhere else: it must ask to
    // be saved before it can be closed.
    if (path.isEmpty()) modified = true;
    document->setModified(modified);

    const bool idFree = tabIndexForId(id) < 0;
    const bool batch = m_sessionBatch;
    m_sessionBatch = true;
    addDocument(document.release(), path, [&](Tab& tab) {
        if (idFree) tab.id = id;
        tab.savedHash = path.isEmpty() ? QByteArray{} : origin;
        const auto tool = item.value(QStringLiteral("tool")).toString();
        if (!tool.isEmpty()) tab.tool = tool;
        const double zoom = item.value(QStringLiteral("zoom")).toDouble();
        if (std::isfinite(zoom) && zoom >= 6.25 && zoom <= 6400.0) tab.zoom = zoom;
        const double cx = item.value(QStringLiteral("centerX")).toDouble();
        const double cy = item.value(QStringLiteral("centerY")).toDouble();
        if (item.value(QStringLiteral("hasView")).toBool() && std::isfinite(cx) && std::isfinite(cy)) {
            tab.hasView = true;
            tab.viewCenter = QPointF(cx, cy);
        }
    });
    m_sessionBatch = batch;

    // The snapshot file stays on disk and in the index until the next
    // snapshot pass or Save replaces it, so a crash right now loses nothing.
    m_pendingRecovery.erase(std::find_if(m_pendingRecovery.begin(), m_pendingRecovery.end(),
                                         [&key](const RecoveryEntry& entry) { return entry.key == key; }));
    emit recoveryEntriesChanged();
    if (m_pendingRecovery.empty()) emit recoveryAvailableChanged();
    emit tabsChanged();
    return true;
}

bool WorkspaceState::recoverEntry(const QString& key)
{
    QString error;
    QString notice;
    const bool ok = recoverEntryInternal(key, &error, &notice);
    if (ok) saveSession();
    if (!ok && !error.isEmpty()) emit fileErrorOccurred(error);
    else if (!notice.isEmpty()) emit fileErrorOccurred(notice);
    return ok;
}

bool WorkspaceState::restoreWorkspace()
{
    if (!m_recoveryLock || m_pendingRecovery.empty() || m_history->strokeActive()) return false;
    QStringList keys;
    for (const auto& entry : m_pendingRecovery) keys.append(entry.key);
    const QUuid active(m_recoveryActiveId);
    QStringList problems;
    QStringList notices;
    int recovered = 0;
    for (const auto& key : keys) {
        QString error;
        QString notice;
        if (recoverEntryInternal(key, &error, &notice)) {
            ++recovered;
            if (!notice.isEmpty()) notices.append(notice);
        } else if (!error.isEmpty()) {
            problems.append(error);
        }
    }
    if (recovered > 0) {
        const int index = tabIndexForId(active);
        if (index >= 0) activateTab(index);
        saveSession();
    }
    const QStringList messages = problems + notices;
    if (!messages.isEmpty()) emit fileErrorOccurred(messages.join(QStringLiteral("\n\n")));
    return problems.isEmpty();
}

bool WorkspaceState::discardEntry(const QString& key)
{
    if (!m_recoveryLock) return false;
    const auto found = std::find_if(m_pendingRecovery.begin(), m_pendingRecovery.end(),
                                    [&key](const RecoveryEntry& entry) { return entry.key == key; });
    if (found == m_pendingRecovery.end()) return false;
    const QDir dir(m_recoveryDirectory);
    // The index stops referring to the snapshot before the file is removed,
    // so a failure in between can only leave an unreferenced file, which the
    // next start cleans up - never an entry pointing at nothing.
    QList<QJsonObject> kept;
    QString activeId;
    IndexData old;
    if (readWorkspaceIndex(dir, &old)) {
        for (const auto& item : old.entries)
            if (item.value(QStringLiteral("snapshot")).toString() != key) kept.append(item);
        activeId = old.activeId;
    } else {
        for (const auto& entry : m_pendingRecovery)
            if (entry.key != key) kept.append(entry.json);
    }
    QString error;
    if (!commitRecoveryIndex(kept, activeId, &error)) {
        emit fileErrorOccurred(QStringLiteral("The recovery entry could not be discarded (%1). It has been kept.").arg(error));
        return false;
    }
    if (bareSnapshotName(key)) QFile::remove(dir.filePath(key));
    m_pendingRecovery.erase(found);
    emit recoveryEntriesChanged();
    if (m_pendingRecovery.empty()) emit recoveryAvailableChanged();
    return true;
}

bool WorkspaceState::discardRecovery()
{
    if (!m_recoveryLock || m_pendingRecovery.empty()) return false;
    const QDir dir(m_recoveryDirectory);
    QSet<QString> pendingKeys;
    for (const auto& entry : m_pendingRecovery) pendingKeys.insert(entry.key);
    QList<QJsonObject> kept;
    QString activeId;
    IndexData old;
    if (readWorkspaceIndex(dir, &old)) {
        for (const auto& item : old.entries)
            if (!pendingKeys.contains(item.value(QStringLiteral("snapshot")).toString())) kept.append(item);
        activeId = old.activeId;
    }
    QString error;
    if (!commitRecoveryIndex(kept, activeId, &error)) {
        emit fileErrorOccurred(QStringLiteral("The recovery data could not be discarded (%1). It has been kept.").arg(error));
        return false;
    }
    for (const auto& key : pendingKeys)
        if (bareSnapshotName(key)) QFile::remove(dir.filePath(key));
    m_pendingRecovery.clear();
    emit recoveryEntriesChanged();
    emit recoveryAvailableChanged();
    return true;
}

// ---------------------------------------------------------------- session

bool WorkspaceState::initializeSession(const QString& filePath)
{
    if (filePath.isEmpty()) return false;
    m_sessionPath = QFileInfo(filePath).absoluteFilePath();
    m_session = {};
    QString error;
    const auto result = SessionStore::load(m_sessionPath, &m_session, &error);
    m_sessionLoaded = result == SessionStore::LoadResult::Loaded;
    if (result == SessionStore::LoadResult::Invalid)
        qWarning("[KomaPaint] The previous session could not be read (%s); starting with an empty session.", qUtf8Printable(error));
    m_sessionRestorePending = true;
    return true;
}

bool WorkspaceState::restoreSession()
{
    if (m_sessionPath.isEmpty() || !m_sessionRestorePending) return false;
    if (m_history->strokeActive()) return false;
    m_sessionRestorePending = false;
    if (!m_sessionLoaded) { saveSession(); return true; }

    // Projects with a recovery entry waiting are not reopened from disk
    // here: the user decides in the recovery dialog whether the recovered
    // version or nothing is opened.
    QSet<QString> waiting;
    for (const auto& entry : m_pendingRecovery) waiting.insert(entry.tabId.toString(QUuid::WithoutBraces));
    // A file given on the command line was opened before this runs and
    // stays the active project.
    const QUuid keepActive = isPristinePlaceholder(m_tabs.front()) ? QUuid() : m_tabs[static_cast<std::size_t>(m_activeTab)].id;

    QStringList problems;
    m_sessionBatch = true;
    for (const auto& stored : m_session.documents) {
        if (waiting.contains(stored.id)) continue;
        if (tabIndexForPath(stored.path) >= 0) continue;
        const QString label = stored.title.isEmpty() ? QFileInfo(stored.path).fileName() : stored.title;
        QString error;
        CanvasDocument* next = nullptr;
        try { next = DocumentFile::open(stored.path, &error); }
        catch (const std::bad_alloc&) { next = nullptr; error = QStringLiteral("there is not enough memory to open it right now."); }
        if (!next) {
            // One project failing never stops the others.
            problems.append(QStringLiteral("\"%1\" (%2): %3").arg(label, QDir::toNativeSeparators(stored.path), error));
            continue;
        }
        const QUuid id(stored.id);
        const bool idFree = !id.isNull() && tabIndexForId(id) < 0;
        addDocument(next, stored.path, [&](Tab& tab) {
            if (idFree) tab.id = id;
            if (!stored.tool.isEmpty()) tab.tool = stored.tool;
            if (stored.hasView && stored.zoom >= 6.25 && stored.zoom <= 6400.0) {
                tab.zoom = stored.zoom;
                tab.hasView = true;
                tab.viewCenter = stored.center;
            }
        });
    }
    m_sessionBatch = false;
    int activate = tabIndexForId(keepActive);
    if (activate < 0) activate = tabIndexForId(QUuid(m_session.activeId));
    if (activate >= 0) activateTab(activate);
    saveSession();
    if (!problems.isEmpty()) {
        emit fileErrorOccurred(QStringLiteral("Some projects from your last session could not be reopened:\n%1\n\nNothing was deleted. Any recovery data for these projects is still kept.")
                                   .arg(problems.join(QLatin1Char('\n'))));
    }
    return problems.isEmpty();
}

bool WorkspaceState::saveSession()
{
    if (m_sessionPath.isEmpty() || m_sessionRestorePending || m_sessionBatch) return false;
    SessionData data;
    data.ui = m_session.ui;
    // Only projects that have a file can be reopened by path. Unsaved work
    // is the business of recovery, never of the session.
    for (const auto& tab : m_tabs) {
        if (tab.path.isEmpty()) continue;
        SessionDocument doc;
        doc.id = tab.id.toString(QUuid::WithoutBraces);
        doc.path = tab.path;
        doc.title = tab.document->title();
        doc.tool = tab.tool;
        doc.zoom = tab.zoom;
        doc.hasView = tab.hasView;
        doc.center = tab.viewCenter;
        data.documents.push_back(doc);
    }
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()))
        data.activeId = m_tabs[static_cast<std::size_t>(m_activeTab)].id.toString(QUuid::WithoutBraces);
    QString error;
    if (!SessionStore::save(m_sessionPath, data, &error)) {
        qWarning("[KomaPaint] The session could not be saved: %s", qUtf8Printable(error));
        return false;
    }
    m_session = std::move(data);
    m_sessionLoaded = true;
    return true;
}

QVariant WorkspaceState::sessionValue(const QString& key, const QVariant& fallback) const
{
    const auto found = m_session.ui.constFind(key);
    return found == m_session.ui.constEnd() ? fallback : found.value();
}

void WorkspaceState::setSessionValue(const QString& key, const QVariant& value)
{
    if (key.isEmpty()) return;
    const auto found = m_session.ui.constFind(key);
    if (found != m_session.ui.constEnd() && found.value() == value) return;
    m_session.ui.insert(key, value);
    saveSession();
}

void WorkspaceState::finishShutdown(const QStringList& discardIds)
{
    QStringList discard = discardIds;
    // A new project that was never saved and never changed holds no work;
    // it is not offered for recovery after a normal exit.
    for (const auto& tab : m_tabs)
        if (tab.path.isEmpty() && !tab.document->isModified())
            discard.append(tab.id.toString(QUuid::WithoutBraces));
    QString error;
    if (m_recoveryLock && !m_history->strokeActive() && !persistInternal(discard, {}, &error) && !error.isEmpty())
        qWarning("[KomaPaint] Recovery data could not be finalized at shutdown: %s", qUtf8Printable(error));
    saveSession();
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
