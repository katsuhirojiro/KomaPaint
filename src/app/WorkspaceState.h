#pragma once

#include "../animation/TimelineState.h"
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
    Q_PROPERTY(CanvasDocument* document  READ document  CONSTANT)
    Q_PROPERTY(LayerModel*     layers    READ layers    CONSTANT)
    Q_PROPERTY(TimelineState*  timeline  READ timeline  CONSTANT)
    Q_PROPERTY(ColorPalette*   colors    READ colors    CONSTANT)
    Q_PROPERTY(UndoStack*      history   READ history   CONSTANT)
    Q_PROPERTY(QString         activeTool READ activeTool WRITE setActiveTool NOTIFY activeToolChanged)
    Q_PROPERTY(qreal           zoom      READ zoom      WRITE setZoom      NOTIFY zoomChanged)

public:
    explicit WorkspaceState(QObject* parent = nullptr);

    CanvasDocument* document() const { return m_document; }
    LayerModel*     layers()   const { return m_layers; }
    TimelineState*  timeline() const { return m_timeline; }
    ColorPalette*   colors()   const { return m_colors; }
    UndoStack*      history()  const { return m_history; }

    QString activeTool() const { return m_activeTool; }
    qreal   zoom()       const { return m_zoom; }

    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void zoomFit();
    Q_INVOKABLE void resetZoom();

public slots:
    void setActiveTool(const QString& tool);
    void setZoom(qreal z);

signals:
    void activeToolChanged(const QString& tool);
    void zoomChanged(qreal zoom);

private:
    CanvasDocument* m_document;
    LayerModel*     m_layers;
    TimelineState*  m_timeline;
    ColorPalette*   m_colors;
    UndoStack*      m_history;
    QString         m_activeTool = QStringLiteral("brush");
    qreal           m_zoom       = 100.0;
};

}  // namespace komapaint
