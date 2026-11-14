#pragma once
#include "../canvas/CpuTileCompositor.h"
#include "../drawing/BrushEngine.h"
#include "../drawing/StrokeStabilizer.h"
#include <memory>
#include <QQuickPaintedItem>
#include <QColor>
#include <QImage>
#include <QMutex>
#include <QPointer>
#include <QRectF>
#include <QVariant>
#include <map>

namespace komapaint {
class CanvasDocument;
class Layer;
class UndoStack;
class CanvasViewport : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QObject* history READ history WRITE setHistory NOTIFY historyChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    // Document point shown at the centre of the viewport. Together with zoom
    // this is the view state each project keeps for itself.
    Q_PROPERTY(QPointF viewCenter READ viewCenter NOTIFY viewChanged)
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
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
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
    // Test hooks: explicit synchronous refresh and a copy-on-write snapshot.
    void refreshNow();
    QImage frameSnapshot() const;
    std::size_t compositeCount() const { return m_compositeCount; }
signals:
    void canvasColoursChanged();
    void documentChanged();
    void historyChanged();
    void zoomChanged();
    void zoomRequested(qreal zoom);
    // Zoom or position changed (pan, zoom, fit, resize).
    void viewChanged();
    void strokeError(const QString& message);
protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
private:
    struct CacheEntry { QImage image; std::size_t bytes; std::uint64_t used; };
    void reconnectLayers();
    void clearCache() noexcept;
    void eraseCacheTile(TilePoint tile) noexcept;
    void invalidateAll();
    void changedTile(qint64 x, qint64 y);
    void reportStrokeFailure(SparseTileStore::WriteResult reason, bool rolledBack, std::size_t tileBytes, std::size_t tileBudget);
    void schedule(const QRectF& region);
    std::vector<CompositeLayer> layers() const;
    QImage composedTile(TilePoint tile, const std::vector<CompositeLayer>& layers, std::uint32_t background);
    QPointer<CanvasDocument> m_document;
    QPointer<UndoStack> m_history;
    QPointer<Layer> m_strokeLayer;
    std::unique_ptr<BrushStroke> m_stroke;
    StrokeStabilizer m_stabilizer;
    CompositeInvalidation m_invalidation;
    std::map<TilePoint, CacheEntry, TilePointLess> m_cache;
    std::size_t m_cacheBytes = 0;
    std::shared_ptr<DocumentMemoryBudget> m_cacheBudget;
    static constexpr std::size_t CacheLimit = 32U * 1024U * 1024U;
    std::uint64_t m_clock = 0;
    std::size_t m_compositeCount = 0;
    qreal m_zoom = 100;
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
};
} // namespace komapaint
