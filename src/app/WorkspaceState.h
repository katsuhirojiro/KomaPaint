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

    Q_INVOKABLE bool newDocument(int width, int height, bool transparent, const QColor& background);
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

private:
    BrushSettings* m_brush;
    CanvasDocument* m_document;
    TimelineState*  m_timeline;
    ColorPalette*   m_colors;
    UndoStack*      m_history;
    QString         m_activeTool = QStringLiteral("brush");
    qreal           m_zoom       = 100.0;
};

}  // namespace komapaint
