#include "WorkspaceState.h"

#include <QColor>

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
    connectLayerDeletion(m_document);
}

void WorkspaceState::connectLayerDeletion(const CanvasDocument* document)
{
    connect(document->layers(), &QAbstractItemModel::rowsAboutToBeRemoved, this, [this] {
        if (m_history->strokeActive()) m_history->cancelStroke();
        if (m_history->canUndo() || m_history->canRedo()) { m_history->clear(); emit m_history->errorOccurred(QStringLiteral("Artwork history was cleared because a layer was deleted.")); }
    });
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
    auto* previous = m_document;
    m_history->clear(); // history references tiles of the document being replaced
    m_document = next;
    connectLayerDeletion(m_document);
    m_currentFile.clear();
    emit currentFileChanged();
    emit documentChanged();
    previous->deleteLater();
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
    replaceDocument(next);
    return true;
}

bool WorkspaceState::newProject(const QString& name, int width, int height, bool transparent,
                                const QString& backgroundColor, qreal dpi)
{
    if (m_history->strokeActive()) return false;
    if (name.trimmed().isEmpty()) return false;
    QColor bgColor(backgroundColor);
    if (!CanvasDocument::validSize(QSize(width, height)) || !bgColor.isValid()
        || bgColor.alpha() != 255 || dpi <= 0) return false;
    auto* next = new CanvasDocument(this);
    next->setTitle(name.trimmed());
    next->setSize(QSize(width, height));
    next->setDpi(dpi);
    next->setBackgroundColor(bgColor);
    next->setTransparentBackground(transparent);
    replaceDocument(next);
    return true;
}

bool WorkspaceState::saveToPath(const QString& pathOrUrl)
{
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
    m_document->setModified(false);
    const bool changed = m_currentFile != path;
    m_currentFile = path;
    if (m_document->title().isEmpty() || m_document->title() == QStringLiteral("Untitled"))
        m_document->setTitle(QFileInfo(path).completeBaseName());
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
    auto* previous = m_document;
    m_history->clear(); // history references tiles of the document being replaced
    m_document = next;
    connectLayerDeletion(m_document);
    const bool changed = m_currentFile != path;
    m_currentFile = path;
    // The title travels with the file; do not clobber a user-chosen title.
    emit documentChanged();
    if (changed) emit currentFileChanged();
    previous->deleteLater();
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

}  // namespace komapaint
