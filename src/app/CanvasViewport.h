#pragma once
#include "../canvas/CpuTileCompositor.h"
#include "../drawing/BrushEngine.h"
#include <memory>
#include <QQuickPaintedItem>
#include <QImage>
#include <QMutex>
#include <QPointer>
#include <QRectF>
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
public:
    explicit CanvasViewport(QQuickItem* parent = nullptr);
    ~CanvasViewport() override;
    QObject* document() const;
    void setDocument(QObject* document);
    QObject* history() const;
    void setHistory(QObject* history);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
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
    void paint(QPainter* painter) override;
    // Test hooks: explicit synchronous refresh and a copy-on-write snapshot.
    void refreshNow();
    QImage frameSnapshot() const;
    std::size_t compositeCount() const { return m_compositeCount; }
signals:
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
    CompositeInvalidation m_invalidation;
    std::map<TilePoint, CacheEntry, TilePointLess> m_cache;
    std::size_t m_cacheBytes = 0;
    std::shared_ptr<DocumentMemoryBudget> m_cacheBudget;
    static constexpr std::size_t CacheLimit = 32U * 1024U * 1024U;
    std::uint64_t m_clock = 0;
    std::size_t m_compositeCount = 0;
    qreal m_zoom = 100;
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
