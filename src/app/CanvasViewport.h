#pragma once
#include "../canvas/CpuTileCompositor.h"
#include "../drawing/BrushEngine.h"
#include "../drawing/StrokeStabilizer.h"
#include "../input/PointerInput.h"
#include "../input/WinTabBackend.h"
#include "../material/MaterialEngine.h"
#include <array>
#include <memory>
#include <QQuickPaintedItem>
#include <QBrush>
#include <QColor>
#include <QImage>
#include <QElapsedTimer>
#include <QMutex>
#include <QPointer>
#include <QRectF>
#include <QVariant>
#include <map>
#include <span>
#include <vector>

class QHoverEvent;
class QMouseEvent;
class QTabletEvent;
class QTimer;
class QTouchEvent;

namespace komapaint {
namespace input {
class TabletInputSettings;
}
class BrushSettings;
class CanvasDocument;
class Layer;
class UndoStack;
class CanvasViewport : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QObject* history READ history WRITE setHistory NOTIFY historyChanged)
    Q_PROPERTY(QObject* inputSettings READ inputSettings WRITE setInputSettings NOTIFY inputSettingsChanged)
    Q_PROPERTY(QObject* brushSettings READ brushSettings WRITE setBrushSettings NOTIFY brushSettingsChanged)
    Q_PROPERTY(QColor brushColor READ brushColor WRITE setBrushColor NOTIFY brushColorChanged)
    Q_PROPERTY(QString activeTool READ activeTool WRITE setActiveTool NOTIFY activeToolChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(qreal viewRotation READ viewRotation WRITE setViewRotation NOTIFY viewRotationChanged)
    // Document point shown at the centre of the viewport. Together with zoom
    // this is the view state each project keeps for itself.
    Q_PROPERTY(QPointF viewCenter READ viewCenter NOTIFY viewChanged)
    // Live pointer/hover state for canvas brush cursor preview.
    Q_PROPERTY(bool cursorVisible READ cursorVisible NOTIFY cursorStateChanged)
    Q_PROPERTY(QPointF cursorPos READ cursorPos NOTIFY cursorStateChanged)
    Q_PROPERTY(qreal cursorTiltX READ cursorTiltX NOTIFY cursorStateChanged)
    Q_PROPERTY(qreal cursorTiltY READ cursorTiltY NOTIFY cursorStateChanged)
    Q_PROPERTY(qreal cursorRotationDeg READ cursorRotationDeg NOTIFY cursorStateChanged)
    Q_PROPERTY(bool cursorHasTilt READ cursorHasTilt NOTIFY cursorStateChanged)
    Q_PROPERTY(bool cursorEraserActive READ cursorEraserActive NOTIFY cursorStateChanged)
    Q_PROPERTY(bool cursorStylusInProximity READ cursorStylusInProximity NOTIFY cursorStateChanged)
    // Colours the canvas paints around and behind the artwork. They come from
    // the active theme (KomaTheme.qml) so the desk and the transparency
    // checkerboard follow a theme switch like the rest of the window.
    Q_PROPERTY(QColor deskColour READ deskColour WRITE setDeskColour NOTIFY canvasColoursChanged)
    Q_PROPERTY(QColor checkerLight READ checkerLight WRITE setCheckerLight NOTIFY canvasColoursChanged)
    Q_PROPERTY(QColor checkerDark READ checkerDark WRITE setCheckerDark NOTIFY canvasColoursChanged)
public:
    explicit CanvasViewport(QQuickItem* parent = nullptr);
    ~CanvasViewport() override;
    QObject* document() const;
    void setDocument(QObject* document);
    QObject* history() const;
    void setHistory(QObject* history);
    QObject* inputSettings() const;
    void setInputSettings(QObject* settings);
    QObject* brushSettings() const;
    void setBrushSettings(QObject* settings);
    QColor brushColor() const { return m_brushColor; }
    void setBrushColor(const QColor& color);
    QString activeTool() const { return m_activeTool; }
    void setActiveTool(const QString& tool);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    qreal viewRotation() const { return m_viewRotation; }
    void setViewRotation(qreal degrees);
    Q_INVOKABLE void resetRotation();

    bool cursorVisible() const { return m_cursorVisible; }
    QPointF cursorPos() const { return m_cursorPos; }
    qreal cursorTiltX() const { return m_cursorTiltX; }
    qreal cursorTiltY() const { return m_cursorTiltY; }
    qreal cursorRotationDeg() const { return m_cursorRotationDeg; }
    bool cursorHasTilt() const { return m_cursorHasTilt; }
    bool cursorEraserActive() const { return m_cursorEraserActive; }
    bool cursorStylusInProximity() const { return m_cursorStylusInProximity; }

    QColor deskColour() const { return m_deskColour; }
    void setDeskColour(const QColor& colour);
    QColor checkerLight() const { return m_checkerLight; }
    void setCheckerLight(const QColor& colour);
    QColor checkerDark() const { return m_checkerDark; }
    void setCheckerDark(const QColor& colour);
    Q_INVOKABLE void zoomAt(qreal x, qreal y, qreal factor);
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    Q_INVOKABLE QPointF viewportToDocument(qreal x, qreal y) const;
    Q_INVOKABLE QPointF documentToViewport(qreal x, qreal y) const;
    Q_INVOKABLE void fit();
    QPointF viewCenter() const;
    // Shows the given document point at the centre of the viewport at the
    // given zoom. Used when a project tab is shown again and after a restart.
    // If the item has no size yet the view is applied as soon as it has one.
    Q_INVOKABLE void restoreView(qreal zoom, qreal centerX, qreal centerY);
    Q_INVOKABLE bool beginStroke(qreal x, qreal y, qreal pressure, QObject* settings, const QColor& color);
    Q_INVOKABLE void continueStroke(qreal x, qreal y, qreal pressure);
    Q_INVOKABLE void endStroke(qreal x, qreal y, qreal pressure);
    Q_INVOKABLE void cancelStroke();
    // Colour of the composed artwork under a viewport position, as an opaque
    // QColor. Empty (undefined in QML) outside the document and where the
    // artwork is fully transparent, so the eyedropper never picks the desk or
    // the transparency checkerboard.
    Q_INVOKABLE QVariant colorAt(qreal x, qreal y) const;
    void paint(QPainter* painter) override;
    // Test hooks & input integration helpers.
    void refreshNow();
    QImage frameSnapshot() const;
    std::size_t compositeCount() const { return m_compositeCount; }
    input::PointerStrokeArbiter& pointerArbiter() noexcept { return m_arbiter; }
    const input::PointerStrokeArbiter& pointerArbiter() const noexcept { return m_arbiter; }
    void injectRawPointerEvent(const input::RawPointerEvent& raw);
signals:
    void canvasColoursChanged();
    void documentChanged();
    void historyChanged();
    void inputSettingsChanged();
    void brushSettingsChanged();
    void brushColorChanged();
    void activeToolChanged();
    void zoomChanged();
    void viewRotationChanged();
    void zoomRequested(qreal zoom);
    // Zoom or position changed (pan, zoom, fit, resize).
    void viewChanged();
    void cursorStateChanged();
    void strokeError(const QString& message);
    void eyedropperPicked(const QColor& color);
    void barrelActionRequested(const QString& action);
protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    bool event(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseUngrabEvent() override;
    void hoverEnterEvent(QHoverEvent* e) override;
    void hoverMoveEvent(QHoverEvent* e) override;
    void hoverLeaveEvent(QHoverEvent* e) override;
    void touchEvent(QTouchEvent* e) override;
    void touchUngrabEvent() override;
private:
    struct CacheEntry { QImage image; std::size_t bytes; std::uint64_t used; };
    struct TouchAnchor {
        bool active = false;
        int id = -1;
        QPointF pos;
    };
    void reconnectLayers();
    void clearCache() noexcept;
    void eraseCacheTile(TilePoint tile) noexcept;
    void invalidateAll();
    void changedTile(qint64 x, qint64 y);
    void refreshMaterialSimulation();
    void tickMaterialSimulation();
    void syncInputConfig();
    void syncWinTabAttachment();
    void updateCursorState(const input::PointerSample& sample, bool visible);
    void clearCursorState();
    bool activeToolIsPaintable() const;
    void processArbiterResult(const input::ProcessResult& res);
    void handleTabletEvent(QTabletEvent* e);
    void handleTouchNavigation(const QTouchEvent* e);
    void recoverOpenStroke(bool commit);
    void clearStrokeObjects();
    bool beginStrokeSample(const input::PointerSample& sample, BrushSettings* brush,
                           const QColor& color, bool eraseOverride);
    void continueStrokeSample(const input::PointerSample& sample);
    void endStrokeSample(const input::PointerSample& sample);
    void reportStrokeFailure(SparseTileStore::WriteResult reason, bool rolledBack,
                             std::size_t tileBytes, std::size_t tileBudget);
    void schedule(const QRectF& region);
    std::vector<CompositeLayer> layers() const;
    QImage composedTile(TilePoint tile, const std::vector<CompositeLayer>& layers, std::uint32_t background);

    QPointer<CanvasDocument> m_document;
    QPointer<UndoStack> m_history;
    QPointer<input::TabletInputSettings> m_inputSettings;
    QPointer<BrushSettings> m_brushSettings;
    QColor m_brushColor = QColor(Qt::black);
    QString m_activeTool = QStringLiteral("brush");
    QPointer<Layer> m_strokeLayer;
    std::unique_ptr<BrushStroke> m_stroke;
    std::unique_ptr<material::MaterialStroke> m_materialStroke;
    std::unique_ptr<material::BrushReservoir> m_materialReservoir;
    quint64 m_loadedMixtureRevision = 0;
    QTimer* m_materialTimer = nullptr;
    QTimer* m_diagnosticsTimer = nullptr;
    QElapsedTimer m_materialClock;
    qint64 m_lastMaterialTick = 0;
    StrokeStabilizer m_stabilizer;
    input::PointerStrokeArbiter m_arbiter;
    std::unique_ptr<input::WinTabBackend> m_winTab;
    std::vector<input::RawPointerEvent> m_touchScratch;
    std::array<TouchAnchor, 8> m_touchAnchors{};
    bool m_touchNavActive = false;
    bool m_barrelPanActive = false;
    bool m_barrelEyedropperActive = false;
    QPointF m_lastPanPos;
    input::PointerSample m_lastStrokeSample{};
    bool m_hasLastStrokeSample = false;

    bool m_cursorVisible = false;
    QPointF m_cursorPos;
    qreal m_cursorTiltX = 0.0;
    qreal m_cursorTiltY = 0.0;
    qreal m_cursorRotationDeg = 0.0;
    bool m_cursorHasTilt = false;
    bool m_cursorEraserActive = false;
    bool m_cursorStylusInProximity = false;

    CompositeInvalidation m_invalidation;
    std::map<TilePoint, CacheEntry, TilePointLess> m_cache;
    std::size_t m_cacheBytes = 0;
    std::shared_ptr<DocumentMemoryBudget> m_cacheBudget;
    static constexpr std::size_t CacheLimit = 32U * 1024U * 1024U;
    std::uint64_t m_clock = 0;
    std::size_t m_compositeCount = 0;
    qreal m_zoom = 100;
    qreal m_viewRotation = 0.0;
    // Canvas chrome; replaced by the theme when the window binds them.
    QColor m_deskColour = QColor(0x4a, 0x4d, 0x52);
    QColor m_checkerLight = QColor(0x76, 0x7a, 0x81);
    QColor m_checkerDark = QColor(0x5a, 0x5d, 0x63);
    QPointF m_origin;
    bool m_originInitialized = false;
    bool m_pendingView = false;   // restoreView() arrived before the item had a size
    QPointF m_pendingCenter;
    bool m_modifiedBeforeStroke = false;
    bool m_scheduled = false;
    QRectF m_pending;
    mutable QMutex m_frameMutex;
    QImage m_frame;
    QBrush m_checkerBrush;
    QColor m_cachedCheckerLight;
    QColor m_cachedCheckerDark;
    std::vector<std::vector<std::uint32_t>> m_materialScratchBuffers;
    std::vector<std::span<const std::uint32_t>> m_materialOverrides;
    std::vector<std::uint32_t> m_composeTilePixels;
};
} // namespace komapaint
