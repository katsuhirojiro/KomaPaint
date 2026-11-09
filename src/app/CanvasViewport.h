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
class CanvasViewport : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
public:
    explicit CanvasViewport(QQuickItem* parent = nullptr);
    QObject* document() const;
    void setDocument(QObject* document);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    Q_INVOKABLE void zoomAt(qreal x, qreal y, qreal factor);
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    Q_INVOKABLE QPointF viewportToDocument(qreal x, qreal y) const;
    Q_INVOKABLE QPointF documentToViewport(qreal x, qreal y) const;
    Q_INVOKABLE void fit();
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
    void zoomChanged();
    void zoomRequested(qreal zoom);
    void strokeError(const QString& message);
protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
private:
    struct CacheEntry { QImage image; std::size_t bytes; std::uint64_t used; };
    void reconnectLayers();
    void invalidateAll();
    void changedTile(qint64 x, qint64 y);
    void schedule(const QRectF& region);
    std::vector<CompositeLayer> layers() const;
    QImage composedTile(TilePoint tile, const std::vector<CompositeLayer>& layers, std::uint32_t background);
    QPointer<CanvasDocument> m_document;
    QPointer<Layer> m_strokeLayer;
    std::unique_ptr<BrushStroke> m_stroke;
    CompositeInvalidation m_invalidation;
    std::map<TilePoint, CacheEntry, TilePointLess> m_cache;
    std::size_t m_cacheBytes = 0;
    static constexpr std::size_t CacheLimit = 32U * 1024U * 1024U;
    std::uint64_t m_clock = 0;
    std::size_t m_compositeCount = 0;
    qreal m_zoom = 100;
    QPointF m_origin;
    bool m_originInitialized = false;
    bool m_scheduled = false;
    QRectF m_pending;
    mutable QMutex m_frameMutex;
    QImage m_frame;
};
} // namespace komapaint
