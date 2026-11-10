#pragma once

#include "../animation/TimelineState.h"
#include "BrushSettings.h"
#include "../canvas/CanvasDocument.h"
#include "../color/ColorPalette.h"
#include "../history/UndoStack.h"
#include "../layers/LayerModel.h"

#include <QObject>
#include <QString>

namespace komapaint {

// Aggregates the per-session state that the UI binds to: the current
// document, layer list, timeline, colors and undo stack. One instance per
// running KomaPaint process; exposed to QML as "Workspace".

class Settings;

class WorkspaceState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(BrushSettings* brush READ brush CONSTANT)
    Q_PROPERTY(CanvasDocument* document  READ document  NOTIFY documentChanged)
    Q_PROPERTY(LayerModel*     layers    READ layers    NOTIFY documentChanged)
    Q_PROPERTY(TimelineState*  timeline  READ timeline  CONSTANT)
    Q_PROPERTY(ColorPalette*   colors    READ colors    CONSTANT)
    Q_PROPERTY(UndoStack*      history   READ history   CONSTANT)
    Q_PROPERTY(QString         currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(QString         activeTool READ activeTool WRITE setActiveTool NOTIFY activeToolChanged)
    Q_PROPERTY(qreal           zoom      READ zoom      WRITE setZoom      NOTIFY zoomChanged)

public:
    explicit WorkspaceState(QObject* parent = nullptr);

    BrushSettings* brush() const { return m_brush; }
    CanvasDocument* document() const { return m_document; }
    LayerModel*     layers()   const { return m_document->layers(); }
    TimelineState*  timeline() const { return m_timeline; }
    ColorPalette*   colors()   const { return m_colors; }
    UndoStack*      history()  const { return m_history; }

    QString activeTool() const { return m_activeTool; }
    qreal   zoom()       const { return m_zoom; }
    QString currentFile() const { return m_currentFile; }

    Q_INVOKABLE bool newDocument(int width, int height, bool transparent, const QColor& background);
    // Creates a new illustration project from the New Project dialog: applies
    // name, canvas dimensions, DPI and background, then replaces the current
    // project cleanly (history cleared, previous document discarded). Returns
    // false and leaves the current project untouched on invalid input.
    Q_INVOKABLE bool newProject(const QString& name, int width, int height, bool transparent,
                                const QString& backgroundColor, qreal dpi);
    // Cleanly replaces the current document with another document: clears
    // history, reconnects layer-deletion handling, clears the current file
    // and emits documentChanged() / currentFileChanged(). The previous
    // document is scheduled for deletion via deleteLater().
    void replaceDocument(CanvasDocument* next);
    // Saves the current document. pathOrUrl may be a plain local path or a
    // file:// URL (as delivered by QML FileDialog). Empty path saves to the
    // current file when one is set. On failure the file on disk is untouched
    // and fileErrorOccurred carries a user-facing message.
    Q_INVOKABLE bool saveToPath(const QString& pathOrUrl);
    // Loads the document at pathOrUrl, fully validates it, then atomically
    // swaps it in as the current document. On any failure the current
    // document is left completely unchanged and fileErrorOccurred fires.
    Q_INVOKABLE bool openFromPath(const QString& pathOrUrl);
    // Per-user Windows registration so double-clicking a .koma file opens it
    // in this build. Emits fileAssociationCompleted with a user-readable
    // message; never modifies system-wide settings. Not supported off Windows.
    Q_INVOKABLE void associateWithKomaFiles();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void zoomFit();
    Q_INVOKABLE void resetZoom();

public slots:
    void setActiveTool(const QString& tool);
    void setZoom(qreal z);

signals:
    void documentChanged();
    void activeToolChanged(const QString& tool);
    void zoomChanged(qreal zoom);
    void currentFileChanged();
    void fileErrorOccurred(const QString& message);
    void fileAssociationCompleted(const QString& message, bool ok);

private:
    // Clears artwork history when a layer is deleted, because layer deletion
    // is not yet undoable. Connected to every document we own or swap in.
    void connectLayerDeletion(const CanvasDocument* document);
    BrushSettings* m_brush;
    CanvasDocument* m_document;
    TimelineState*  m_timeline;
    ColorPalette*   m_colors;
    UndoStack*      m_history;
    QString         m_activeTool = QStringLiteral("brush");
    QString         m_currentFile;
    qreal           m_zoom       = 100.0;
};

}  // namespace komapaint
