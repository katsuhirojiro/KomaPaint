#include "WorkspaceState.h"

#include <QColor>
#include <algorithm>
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

#include "FileAssociation.h"
#include "../core/Settings.h"
#include "../files/DocumentFile.h"

#include <QFileInfo>
#include <QUrl>

namespace komapaint {

WorkspaceState::WorkspaceState(QObject* parent) : QObject(parent)
{
    m_brush = new BrushSettings(this);
    m_document = new CanvasDocument(this);
    m_timeline = new TimelineState(this);
    m_colors   = new ColorPalette(this);
    m_history  = new UndoStack(this);
    m_creationService = new ProjectCreationService(this);
    m_tabs.push_back({m_document, m_history, {}, QUuid::createUuid()});
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
    m_tabs.push_back({document, history, path, QUuid::createUuid()});
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
        m_tabs.push_back({blank, history, {}, QUuid::createUuid()});
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
    QString error;
    if (!DocumentFile::save(*m_document, path, &error)) { emit fileErrorOccurred(error); return false; }
    const bool changed = m_currentFile != path;
    m_currentFile = path;
    m_tabs[static_cast<std::size_t>(m_activeTab)].path = path;
    if (m_document->title().isEmpty() || m_document->title() == QStringLiteral("Untitled"))
        m_document->setTitle(QFileInfo(path).completeBaseName());
    m_document->setModified(false);
    if (changed) emit currentFileChanged();
    return true;
}

bool WorkspaceState::openFromPath(const QString& pathOrUrl)
{
    if (m_history->strokeActive()) { emit fileErrorOccurred(QStringLiteral("Finish or cancel the current stroke before opening a file.")); return false; }
    const QString path = localPathFromQml(pathOrUrl);
    if (path.isEmpty()) { emit fileErrorOccurred(QStringLiteral("No readable filename was given.")); return false; }
    QString error;
    CanvasDocument* next = DocumentFile::open(path, &error);
    if (!next) { emit fileErrorOccurred(error); return false; }
    addDocument(next, path);
    return true;
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
