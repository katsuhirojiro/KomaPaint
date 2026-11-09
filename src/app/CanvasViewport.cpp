#include "CanvasViewport.h"
#include "BrushSettings.h"
#include "../canvas/CanvasDocument.h"
#include "../layers/Layer.h"
#include "../layers/LayerModel.h"
#include <QPainter>
#include <QMutexLocker>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace komapaint {
CanvasViewport::CanvasViewport(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    setAntialiasing(false);
}
QObject* CanvasViewport::document() const { return m_document.data(); }
void CanvasViewport::setDocument(QObject* object)
{
    auto* doc = qobject_cast<CanvasDocument*>(object);
    if (m_document == doc) return;
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
        disconnect(m_document->layers(), nullptr, this, nullptr);
        for (int i = 0; i < m_document->layers()->rowCount(); ++i)
            disconnect(m_document->layers()->layerAt(i), nullptr, this, nullptr);
    }
    cancelStroke();
    m_document = doc;
    m_invalidation.reset();
    m_cache.clear(); m_cacheBytes = 0;
    m_originInitialized = false;
    if (doc) {
        connect(doc, &CanvasDocument::backgroundChanged, this, &CanvasViewport::invalidateAll);
        connect(doc, &CanvasDocument::sizeChanged, this, [this] {
            m_invalidation.reset(); m_cache.clear(); m_cacheBytes = 0; invalidateAll();
        });
        connect(doc, &QObject::destroyed, this, [this] { m_document = nullptr; m_invalidation.reset(); invalidateAll(); });
        auto* model = doc->layers();
        connect(model, &QAbstractItemModel::dataChanged, this, &CanvasViewport::invalidateAll);
        connect(model, &QAbstractItemModel::rowsInserted, this, [this] { reconnectLayers(); invalidateAll(); });
        connect(model, &QAbstractItemModel::rowsRemoved, this, [this] { reconnectLayers(); invalidateAll(); });
        connect(model, &QAbstractItemModel::rowsMoved, this, [this] { reconnectLayers(); invalidateAll(); });
        connect(model, &QAbstractItemModel::modelReset, this, [this] { reconnectLayers(); invalidateAll(); });
        reconnectLayers();
    }
    emit documentChanged();
    invalidateAll();
}
void CanvasViewport::reconnectLayers()
{
    if (!m_document) return;
    for (int i = 0; i < m_document->layers()->rowCount(); ++i) {
        auto* layer = m_document->layers()->layerAt(i);
        disconnect(layer, &Layer::artworkChanged, this, &CanvasViewport::changedTile);
        connect(layer, &Layer::artworkChanged, this, &CanvasViewport::changedTile);
    }
}
void CanvasViewport::invalidateAll() { schedule(QRectF(0, 0, width(), height())); }
void CanvasViewport::schedule(const QRectF& region)
{
    m_pending = m_pending.united(region.intersected(QRectF(0, 0, width(), height())));
    if (m_scheduled) return;
    m_scheduled = true;
    QTimer::singleShot(0, this, [this] { refreshNow(); });
}
void CanvasViewport::changedTile(qint64 x, qint64 y)
{
    if (!m_document) return;
    const qreal side = m_document->layers()->layerAt(0)->tiles().geometry().tileSize();
    const qreal scale = m_zoom / 100;
    QRectF rect(m_origin.x() + x * side * scale, m_origin.y() + y * side * scale, side * scale, side * scale);
    schedule(rect.adjusted(-2, -2, 2, 2));
}
void CanvasViewport::setZoom(qreal zoom)
{
    if (!std::isfinite(zoom)) return;
    zoom = qBound(6.25, zoom, 6400.0);
    if (qFuzzyCompare(m_zoom, zoom)) return;
    QPointF center(width() / 2, height() / 2);
    const QPointF point = viewportToDocument(center.x(), center.y());
    m_zoom = zoom;
    m_origin = center - point * (zoom / 100);
    emit zoomChanged(); invalidateAll();
}
void CanvasViewport::zoomAt(qreal x, qreal y, qreal factor)
{
    if (!std::isfinite(factor) || factor <= 0) return;
    const auto next = qBound(6.25, m_zoom * factor, 6400.0);
    if (qFuzzyCompare(next, m_zoom)) return;
    const QPointF doc = viewportToDocument(x, y);
    m_zoom = next; m_origin = QPointF(x, y) - doc * (next / 100);
    emit zoomChanged(); emit zoomRequested(next); invalidateAll();
}
void CanvasViewport::panBy(qreal dx, qreal dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    m_origin.setX(qBound(-1.0e9, m_origin.x() + dx, 1.0e9));
    m_origin.setY(qBound(-1.0e9, m_origin.y() + dy, 1.0e9));
    invalidateAll();
}
QPointF CanvasViewport::viewportToDocument(qreal x, qreal y) const { return (QPointF(x, y) - m_origin) / (m_zoom / 100); }
QPointF CanvasViewport::documentToViewport(qreal x, qreal y) const { return m_origin + QPointF(x, y) * (m_zoom / 100); }
void CanvasViewport::fit()
{
    if (!m_document || width() <= 0 || height() <= 0) return;
    m_zoom = qBound(6.25, 100 * std::min(width() / m_document->width(), height() / m_document->height()), 6400.0);
    m_origin = QPointF((width() - m_document->width() * m_zoom / 100) / 2, (height() - m_document->height() * m_zoom / 100) / 2);
    m_originInitialized = true;
    emit zoomChanged(); emit zoomRequested(m_zoom); invalidateAll();
}
void CanvasViewport::geometryChange(const QRectF& now, const QRectF& old)
{
    QQuickPaintedItem::geometryChange(now, old);
    if (now.size() != old.size()) invalidateAll();
}
std::vector<CompositeLayer> CanvasViewport::layers() const
{
    std::vector<CompositeLayer> result;
    if (!m_document) return result;
    auto* model = m_document->layers();
    result.reserve(static_cast<std::size_t>(model->rowCount()));
    for (int i = 0; i < model->rowCount(); ++i) {
        auto* l = model->layerAt(i);
        result.push_back({&l->tiles(), l->isVisible(), l->opacity()});
    }
    return result;
}
QImage CanvasViewport::composedTile(TilePoint tile, const std::vector<CompositeLayer>& list, std::uint32_t background)
{
    auto found = m_cache.find(tile);
    if (found != m_cache.end()) { found->second.used = ++m_clock; return found->second.image; }
    if (list.empty()) return {};
    auto composite = CpuTileCompositor::compose(list[0].store->geometry(), tile, list, background);
    if (!composite) return {};
    QImage image(static_cast<int>(composite->width), static_cast<int>(composite->height), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    for (int row = 0; row < image.height(); ++row)
        std::memcpy(image.scanLine(row), composite->pixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width()), static_cast<std::size_t>(image.width()) * sizeof(std::uint32_t));
    ++m_compositeCount;
    const auto bytes = static_cast<std::size_t>(image.sizeInBytes());
    while (!m_cache.empty() && (bytes > CacheLimit || m_cacheBytes > CacheLimit - bytes)) {
        auto victim = std::min_element(m_cache.begin(), m_cache.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
        m_cacheBytes -= victim->second.bytes; m_cache.erase(victim);
    }
    if (bytes <= CacheLimit) { m_cache.emplace(tile, CacheEntry{image, bytes, ++m_clock}); m_cacheBytes += bytes; }
    return image;
}
bool CanvasViewport::beginStroke(qreal x, qreal y, qreal pressure, QObject* settings, const QColor& color)
{
    cancelStroke();
    auto* brush = qobject_cast<BrushSettings*>(settings);
    if (!m_document || !brush || !color.isValid()) return false;
    auto* model = m_document->layers();
    auto* layer = model->layerAt(model->activeLayerIndex());
    if (!layer || layer->isLocked() || !layer->isVisible()) {
        emit strokeError(QStringLiteral("Select a visible, unlocked layer to paint."));
        return false;
    }
    BrushDefinition definition;
    definition.size = brush->size(); definition.opacity = brush->opacity() / 100.0;
    definition.flow = brush->flow() / 100.0; definition.spacing = brush->spacing() / 100.0;
    definition.tip = brush->textured() ? BrushTip::Textured : BrushTip::Round;
    definition.color = color.rgba();
    try { m_stroke = std::make_unique<BrushStroke>(layer->tiles(), definition); }
    catch (const std::bad_alloc&) { emit strokeError(QStringLiteral("Not enough memory to start a stroke.")); return false; }
    m_strokeLayer = layer;
    auto point = viewportToDocument(x, y);
    if (!m_stroke->begin({point.x(), point.y(), pressure})) {
        cancelStroke(); emit strokeError(QStringLiteral("Stroke stopped: memory or tile limit reached. Some paint may already have been applied.")); return false;
    }
    return true;
}
void CanvasViewport::continueStroke(qreal x, qreal y, qreal pressure)
{
    if (!m_stroke || !m_strokeLayer || !m_document) { cancelStroke(); return; }
    if (m_strokeLayer->isLocked() || !m_strokeLayer->isVisible() ||
        m_document->layers()->layerAt(m_document->layers()->activeLayerIndex()) != m_strokeLayer) { cancelStroke(); return; }
    auto point = viewportToDocument(x, y);
    if (!m_stroke->extend({point.x(), point.y(), pressure})) {
        cancelStroke(); emit strokeError(QStringLiteral("Stroke stopped: memory or tile limit reached. Some paint may already have been applied."));
    }
}
void CanvasViewport::endStroke(qreal x, qreal y, qreal pressure)
{
    if (!m_stroke || !m_strokeLayer || !m_document) { cancelStroke(); return; }
    if (m_strokeLayer->isLocked() || !m_strokeLayer->isVisible() ||
        m_document->layers()->layerAt(m_document->layers()->activeLayerIndex()) != m_strokeLayer) { cancelStroke(); return; }
    auto point = viewportToDocument(x, y);
    if (!m_stroke->end({point.x(), point.y(), pressure}))
        emit strokeError(QStringLiteral("Stroke stopped: memory or tile limit reached. Some paint may already have been applied."));
    cancelStroke();
}
void CanvasViewport::cancelStroke() { m_stroke.reset(); m_strokeLayer = nullptr; }

void CanvasViewport::paint(QPainter* painter)
{
    QMutexLocker lock(&m_frameMutex);
    painter->drawImage(QPointF(0, 0), m_frame);
}
QImage CanvasViewport::frameSnapshot() const { QMutexLocker lock(&m_frameMutex); return m_frame; }
void CanvasViewport::refreshNow()
{
    m_scheduled = false;
    QRectF dirty = m_pending;
    m_pending = {};
    const QSize frameSize(qCeil(width()), qCeil(height()));
    if (frameSize.width() <= 0 || frameSize.height() <= 0) return;
    // Bound viewport memory independently of document size.
    if (static_cast<qint64>(frameSize.width()) * frameSize.height() > 32LL * 1024 * 1024) {
        qWarning("Canvas viewport too large to allocate safely"); return;
    }
    try {
        QMutexLocker lock(&m_frameMutex);
        if (m_frame.size() != frameSize) {
            QImage next(frameSize, QImage::Format_ARGB32_Premultiplied);
            if (next.isNull()) { qWarning("Canvas viewport allocation failed"); return; }
            m_frame = std::move(next);
            dirty = QRectF(QPointF(0, 0), QSizeF(frameSize));
        }
        if (dirty.isEmpty()) return;
        if (m_document && !m_originInitialized) {
            m_origin = QPointF((width() - m_document->width() * m_zoom / 100) / 2,
                               (height() - m_document->height() * m_zoom / 100) / 2);
            m_originInitialized = true;
            dirty = QRectF(QPointF(0, 0), QSizeF(frameSize));
        }
        QPainter painter(&m_frame);
        painter.setClipRect(dirty);
        painter.fillRect(dirty, QColor("#4a4d52"));
        if (m_document) {
            auto list = layers();
            const auto background = m_document->hasTransparentBackground() ? 0U : m_document->backgroundColor().rgba();
            auto plan = m_invalidation.prepare(list, background);
            if (plan.invalidateAll) { m_cache.clear(); m_cacheBytes = 0; }
            if (!m_invalidation.accept(plan, list, background)) return;
            for (const auto& tile : plan.tiles) {
                auto it = m_cache.find(tile);
                if (it != m_cache.end()) { m_cacheBytes -= it->second.bytes; m_cache.erase(it); }
            }
            const qreal scale = m_zoom / 100;
            painter.save();
            painter.translate(m_origin);
            painter.scale(scale, scale);
            const QRectF documentRect(0, 0, m_document->width(), m_document->height());
            painter.setClipRect(documentRect, Qt::IntersectClip);
            QImage checks(16, 16, QImage::Format_RGB32);
            checks.fill(QColor("#767a81"));
            { QPainter cp(&checks); cp.fillRect(0, 0, 8, 8, QColor("#5a5d63")); cp.fillRect(8, 8, 8, 8, QColor("#5a5d63")); }
            painter.fillRect(documentRect, QBrush(checks));
            if (!list.empty()) {
                const auto& geometry = list.front().store->geometry();
                auto a = viewportToDocument(dirty.left(), dirty.top());
                auto b = viewportToDocument(dirty.right(), dirty.bottom());
                TileRect requested{static_cast<std::int64_t>(std::floor(qBound<qreal>(0, a.x(), m_document->width()))),
                                   static_cast<std::int64_t>(std::floor(qBound<qreal>(0, a.y(), m_document->height()))),
                                   static_cast<std::int64_t>(std::ceil(qBound<qreal>(0, b.x(), m_document->width()))),
                                   static_cast<std::int64_t>(std::ceil(qBound<qreal>(0, b.y(), m_document->height())))};
                auto range = geometry.tilesForRect(requested);
                if (range) {
                    for (auto y = range->first.y; y <= range->last.y; ++y) {
                        for (auto x = range->first.x; x <= range->last.x; ++x) {
                            TilePoint tile{x, y};
                            QImage image = composedTile(tile, list, background);
                            if (image.isNull()) { qWarning("Canvas tile composition failed"); continue; }
                            auto bounds = geometry.tileBounds(tile);
                            painter.drawImage(QPointF(bounds->left, bounds->top), image);
                            if (plan.tiles.find(tile) != plan.tiles.end() && !m_invalidation.acknowledge(plan, tile, list, background)) {
                                qWarning("Canvas tile changed before acknowledgment");
                                auto it = m_cache.find(tile);
                                if (it != m_cache.end()) { m_cacheBytes -= it->second.bytes; m_cache.erase(it); }
                            }
                        }
                    }
                }
            }
            painter.restore();
        }
        painter.end();
    } catch (const std::bad_alloc&) { qWarning("Canvas viewport allocation failed"); return; }
    update(dirty.toAlignedRect());
}
} // namespace komapaint
