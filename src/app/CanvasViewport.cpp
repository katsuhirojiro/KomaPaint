#include "CanvasViewport.h"
#include "BrushSettings.h"
#include "../history/UndoStack.h"
#include "../canvas/CanvasDocument.h"
#include "../layers/Layer.h"
#include "../layers/LayerModel.h"
#include <QPainter>
#include <QMutexLocker>
#include <QTimer>
#include <QDebug>
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
CanvasViewport::~CanvasViewport() { clearCache(); }
void CanvasViewport::clearCache() noexcept
{
    m_cache.clear();
    if (m_cacheBudget) m_cacheBudget->release(DocumentMemoryBudget::Category::CompositeCache, m_cacheBytes);
    m_cacheBytes = 0;
}
void CanvasViewport::eraseCacheTile(TilePoint tile) noexcept
{
    auto it = m_cache.find(tile);
    if (it == m_cache.end()) return;
    const auto bytes = it->second.bytes;
    m_cache.erase(it);
    m_cacheBytes -= bytes;
    if (m_cacheBudget) m_cacheBudget->release(DocumentMemoryBudget::Category::CompositeCache, bytes);
}
QObject* CanvasViewport::history() const { return m_history.data(); }
void CanvasViewport::setHistory(QObject* object)
{
    auto* next = qobject_cast<UndoStack*>(object);
    if (m_history == next) return;
    cancelStroke();
    if (m_history) disconnect(m_history, nullptr, this, nullptr);
    m_history = next;
    if (m_history) connect(m_history, &UndoStack::strokeRolledBack, this, [this] {
        if (!m_strokeLayer) return;
        m_stroke.reset(); m_strokeLayer = nullptr;
        if (m_document) m_document->setModified(m_modifiedBeforeStroke);
        m_invalidation.reset(); clearCache();
        invalidateAll();
    });
    emit historyChanged();
}
QObject* CanvasViewport::document() const { return m_document.data(); }
void CanvasViewport::setDocument(QObject* object)
{
    qDebug() << "[CanvasViewport] setDocument() called:"
             << "object=" << object << "old m_document=" << m_document.data();

    auto* doc = qobject_cast<CanvasDocument*>(object);
    if (m_document == doc) {
        qDebug() << "[CanvasViewport] setDocument: same document, returning early";
        return;
    }
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
        disconnect(m_document->layers(), nullptr, this, nullptr);
        for (int i = 0; i < m_document->layers()->rowCount(); ++i)
            disconnect(m_document->layers()->layerAt(i), nullptr, this, nullptr);
    }
    cancelStroke();
    m_document = doc;
    m_invalidation.reset();
    clearCache();
    m_cacheBudget = doc ? doc->layers()->documentBudget() : nullptr;
    m_originInitialized = false;
    qDebug() << "[CanvasViewport] setDocument: m_document updated to" << m_document.data()
             << "m_originInitialized reset to false";

    if (doc) {
        qDebug() << "[CanvasViewport] setDocument: connecting signals for new document"
                 << "size=" << doc->width() << "x" << doc->height();
        connect(doc, &CanvasDocument::backgroundChanged, this, &CanvasViewport::invalidateAll);
        connect(doc, &CanvasDocument::sizeChanged, this, [this] {
            qDebug() << "[CanvasViewport] sizeChanged signal received";
            m_invalidation.reset(); clearCache(); invalidateAll();
        });
        connect(doc, &QObject::destroyed, this, [this] { m_document = nullptr; m_invalidation.reset(); clearCache(); m_cacheBudget.reset(); invalidateAll(); });
        auto* model = doc->layers();
        connect(model, &QAbstractItemModel::dataChanged, this, &CanvasViewport::invalidateAll);
        connect(model, &QAbstractItemModel::rowsInserted, this, [this] { reconnectLayers(); invalidateAll(); });
        connect(model, &QAbstractItemModel::rowsRemoved, this, [this] { reconnectLayers(); invalidateAll(); });
        connect(model, &QAbstractItemModel::rowsMoved, this, [this] { reconnectLayers(); invalidateAll(); });
        connect(model, &QAbstractItemModel::modelReset, this, [this] { reconnectLayers(); invalidateAll(); });
        reconnectLayers();
    }
    qDebug() << "[CanvasViewport] setDocument: emitting documentChanged(), calling invalidateAll()";
    emit documentChanged();
    invalidateAll();
    qDebug() << "[CanvasViewport] setDocument: done";
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
    qDebug() << "[CanvasViewport] changedTile() tile=(" << x << "," << y << ")";
    if (!m_document) return;
    const qreal side = m_document->layers()->layerAt(0)->tiles().geometry().tileSize();
    const qreal scale = m_zoom / 100;
    QRectF rect(m_origin.x() + x * side * scale, m_origin.y() + y * side * scale, side * scale, side * scale);
    qDebug() << "[CanvasViewport] changedTile: scheduling viewport rect" << rect;
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
    qDebug() << "[CanvasViewport] fit() called:"
             << "m_document=" << m_document.data() << "width=" << width() << "height=" << height();

    if (!m_document) {
        qDebug() << "[CanvasViewport] fit: no document, returning";
        return;
    }
    if (width() <= 0 || height() <= 0) {
        qDebug() << "[CanvasViewport] fit: viewport has zero size, returning";
        return;
    }

    m_zoom = qBound(6.25, 100 * std::min(width() / m_document->width(), height() / m_document->height()), 6400.0);
    m_origin = QPointF((width() - m_document->width() * m_zoom / 100) / 2, (height() - m_document->height() * m_zoom / 100) / 2);
    m_originInitialized = true;
    qDebug() << "[CanvasViewport] fit: zoom=" << m_zoom << "origin=" << m_origin
             << "docSize=" << m_document->width() << "x" << m_document->height();
    emit zoomChanged(); emit zoomRequested(m_zoom); invalidateAll();
    qDebug() << "[CanvasViewport] fit: done";
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
        eraseCacheTile(victim->first);
    }
    if (bytes <= CacheLimit && (!m_cacheBudget || m_cacheBudget->reserve(DocumentMemoryBudget::Category::CompositeCache, bytes))) {
        try {
            m_cache.emplace(tile, CacheEntry{image, bytes, ++m_clock});
            m_cacheBytes += bytes;
        } catch (...) {
            if (m_cacheBudget) m_cacheBudget->release(DocumentMemoryBudget::Category::CompositeCache, bytes);
            throw;
        }
    }
    return image;
}
void CanvasViewport::reportStrokeFailure(SparseTileStore::WriteResult reason, bool rolledBack,
                                         std::size_t tileBytes, std::size_t tileBudget)
{
    using R = SparseTileStore::WriteResult;
    QString detail;
    switch (reason) {
    case R::HistoryLimit:
        detail = QStringLiteral("Undo history capacity reached (accounted %1 of %2 MiB).")
            .arg(m_history ? m_history->memoryBytes() / (1024.0 * 1024.0) : 0.0, 0, 'f', 1)
            .arg(m_history ? m_history->byteLimit() / (1024.0 * 1024.0) : 0.0, 0, 'f', 1);
        break;
    case R::HistoryAllocationFailed: detail = QStringLiteral("Memory allocation for undo history failed."); break;
    case R::MemoryLimit:
        detail = QStringLiteral("This layer's pixel storage limit was reached (%1 of %2 MiB).")
            .arg(tileBytes / (1024.0 * 1024.0), 0, 'f', 1)
            .arg(tileBudget / (1024.0 * 1024.0), 0, 'f', 1);
        break;
    case R::DocumentMemoryLimit: detail = QStringLiteral("Document pixel storage capacity was reached."); break;
    case R::TrackingLimit: detail = QStringLiteral("Tile change tracking capacity was reached."); break;
    case R::AllocationFailed: detail = QStringLiteral("Memory allocation for artwork or tile tracking failed."); break;
    case R::InvalidPixel: detail = QStringLiteral("The brush received invalid settings or input."); break;
    case R::OutOfBounds: detail = QStringLiteral("The canvas tile store is invalid or the paint position is out of bounds."); break;
    default: detail = QStringLiteral("The brush could not continue for an unknown reason."); break;
    }
    detail += rolledBack
        ? QStringLiteral(" The stroke was cancelled and its paint was rolled back.")
        : QStringLiteral(" The stroke could not be rolled back; its history transaction remains active.");
    emit strokeError(detail);
}

bool CanvasViewport::beginStroke(qreal x, qreal y, qreal pressure, QObject* settings, const QColor& color)
{
    qDebug() << "[CanvasViewport] beginStroke() called:"
             << "viewport=(" << x << "," << y << ")" << "pressure=" << pressure
             << "m_document=" << m_document.data();

    cancelStroke();
    auto* brush = qobject_cast<BrushSettings*>(settings);
    if (!m_document) {
        qDebug() << "[CanvasViewport] beginStroke: no document";
        return false;
    }
    if (!m_history) {
        qDebug() << "[CanvasViewport] beginStroke: no history";
        return false;
    }
    if (!brush) {
        qDebug() << "[CanvasViewport] beginStroke: no brush settings";
        return false;
    }
    if (!color.isValid()) {
        qDebug() << "[CanvasViewport] beginStroke: invalid color";
        return false;
    }

    auto* model = m_document->layers();
    auto* layer = model->layerAt(model->activeLayerIndex());
    qDebug() << "[CanvasViewport] beginStroke: activeLayerIndex=" << model->activeLayerIndex()
             << "layer=" << layer << "locked=" << (layer ? layer->isLocked() : true)
             << "visible=" << (layer ? layer->isVisible() : true);

    if (!layer || layer->isLocked() || !layer->isVisible()) {
        emit strokeError(QStringLiteral("Select a visible, unlocked layer to paint."));
        return false;
    }
    m_modifiedBeforeStroke = m_document->isModified();
    if (!m_history->beginStroke(layer)) {
        emit strokeError(QStringLiteral("Could not start stroke history; no paint was applied."));
        return false;
    }
    BrushDefinition definition;
    definition.size = brush->size(); definition.opacity = brush->opacity() / 100.0;
    definition.flow = brush->flow() / 100.0; definition.spacing = brush->spacing() / 100.0;
    definition.tip = brush->textured() ? BrushTip::Textured : BrushTip::Round;
    definition.color = color.rgba();
    try { m_stroke = std::make_unique<BrushStroke>(layer->tiles(), definition); }
    catch (const std::bad_alloc&) { m_history->cancelStroke(); emit strokeError(QStringLiteral("Not enough memory to start a stroke.")); return false; }
    m_strokeLayer = layer;
    auto point = viewportToDocument(x, y);
    qDebug() << "[CanvasViewport] beginStroke: viewport(" << x << "," << y << ") -> document("
             << point.x() << "," << point.y() << ")";
    if (!m_stroke->begin({point.x(), point.y(), pressure})) {
        const auto reason = m_stroke->failureReason();
        const auto tileBytes = layer->tiles().allocatedPixelBytes();
        const auto tileBudget = layer->tiles().pixelBudget();
        const bool rolledBack = m_history->cancelStroke();
        reportStrokeFailure(reason, rolledBack, tileBytes, tileBudget);
        return false;
    }
    qDebug() << "[CanvasViewport] beginStroke: stroke started on layer" << layer->name();
    return true;
}
void CanvasViewport::continueStroke(qreal x, qreal y, qreal pressure)
{
    qDebug() << "[CanvasViewport] continueStroke() called:"
             << "viewport=(" << x << "," << y << ")" << "pressure=" << pressure
             << "m_stroke=" << (bool)m_stroke;

    if (!m_stroke || !m_strokeLayer || !m_document) { qDebug() << "[CanvasViewport] continueStroke: no stroke/layer/document"; cancelStroke(); return; }
    if (m_strokeLayer->isLocked() || !m_strokeLayer->isVisible() ||
        m_document->layers()->layerAt(m_document->layers()->activeLayerIndex()) != m_strokeLayer) {
        qDebug() << "[CanvasViewport] continueStroke: layer changed or invalid";
        cancelStroke(); return; }
    auto point = viewportToDocument(x, y);
    qDebug() << "[CanvasViewport] continueStroke: viewport(" << x << "," << y << ") -> document("
             << point.x() << "," << point.y() << ")";
    if (!m_stroke->extend({point.x(), point.y(), pressure})) {
        const auto reason = m_stroke->failureReason();
        const auto tileBytes = m_strokeLayer->tiles().allocatedPixelBytes();
        const auto tileBudget = m_strokeLayer->tiles().pixelBudget();
        const bool rolledBack = m_history && m_history->cancelStroke();
        reportStrokeFailure(reason, rolledBack, tileBytes, tileBudget);
    }
}
void CanvasViewport::endStroke(qreal x, qreal y, qreal pressure)
{
    qDebug() << "[CanvasViewport] endStroke() called:"
             << "viewport=(" << x << "," << y << ")" << "pressure=" << pressure;

    if (!m_stroke || !m_strokeLayer || !m_document) { qDebug() << "[CanvasViewport] endStroke: no stroke/layer/document"; cancelStroke(); return; }
    if (m_strokeLayer->isLocked() || !m_strokeLayer->isVisible() ||
        m_document->layers()->layerAt(m_document->layers()->activeLayerIndex()) != m_strokeLayer) {
        qDebug() << "[CanvasViewport] endStroke: layer changed or invalid";
        cancelStroke(); return; }
    auto point = viewportToDocument(x, y);
    const bool ok = m_stroke->end({point.x(), point.y(), pressure});
    const auto reason = m_stroke->failureReason();
    const auto tileBytes = m_strokeLayer->tiles().allocatedPixelBytes();
    const auto tileBudget = m_strokeLayer->tiles().pixelBudget();
    if (!ok) {
        const bool rolledBack = m_history && m_history->cancelStroke();
        reportStrokeFailure(reason, rolledBack, tileBytes, tileBudget);
        return;
    }
    m_stroke.reset(); m_strokeLayer = nullptr;
    if (m_history && !m_history->commitStroke())
        emit strokeError(QStringLiteral("Could not commit stroke history."));
    qDebug() << "[CanvasViewport] endStroke: stroke ended, ok=" << ok;
}
void CanvasViewport::cancelStroke() {
    qDebug() << "[CanvasViewport] cancelStroke() called";
    if (m_history && m_history->strokeActive()) m_history->cancelStroke();
    m_stroke.reset(); m_strokeLayer = nullptr;
}

void CanvasViewport::paint(QPainter* painter)
{
    QMutexLocker lock(&m_frameMutex);
    painter->drawImage(QPointF(0, 0), m_frame);
}
QImage CanvasViewport::frameSnapshot() const { QMutexLocker lock(&m_frameMutex); return m_frame; }
void CanvasViewport::refreshNow()
{
    qDebug() << "[CanvasViewport] refreshNow() called:"
             << "m_document=" << m_document.data()
             << "m_originInitialized=" << m_originInitialized
             << "zoom=" << m_zoom << "origin=" << m_origin
             << "frameSize=" << m_frame.size();

    m_scheduled = false;
    QRectF dirty = m_pending;
    m_pending = {};
    const QSize frameSize(qCeil(width()), qCeil(height()));
    if (frameSize.width() <= 0 || frameSize.height() <= 0) {
        qDebug() << "[CanvasViewport] refreshNow: zero frame size, returning";
        return;
    }
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
            qDebug() << "[CanvasViewport] refreshNow: reallocated frame to" << frameSize;
        }
        if (dirty.isEmpty()) {
            qDebug() << "[CanvasViewport] refreshNow: empty dirty region, returning";
            return;
        }
        if (m_document && !m_originInitialized) {
            m_origin = QPointF((width() - m_document->width() * m_zoom / 100) / 2,
                               (height() - m_document->height() * m_zoom / 100) / 2);
            m_originInitialized = true;
            dirty = QRectF(QPointF(0, 0), QSizeF(frameSize));
            qDebug() << "[CanvasViewport] refreshNow: auto-centered origin=" << m_origin;
        }
        QPainter painter(&m_frame);
        painter.setClipRect(dirty);
        painter.fillRect(dirty, QColor("#4a4d52"));
        if (m_document) {
            auto list = layers();
            const auto background = m_document->hasTransparentBackground() ? 0U : m_document->backgroundColor().rgba();
            auto plan = m_invalidation.prepare(list, background);
            if (plan.invalidateAll) clearCache();
            if (!m_invalidation.accept(plan, list, background)) {
                qDebug() << "[CanvasViewport] refreshNow: invalidation accept failed";
                return;
            }
            for (const auto& tile : plan.tiles) {
                eraseCacheTile(tile);
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
                    qDebug() << "[CanvasViewport] refreshNow: composing" << (range->last.y - range->first.y + 1)
                             << "x" << (range->last.x - range->first.x + 1) << "tiles";
                    for (auto y = range->first.y; y <= range->last.y; ++y) {
                        for (auto x = range->first.x; x <= range->last.x; ++x) {
                            TilePoint tile{x, y};
                            QImage image = composedTile(tile, list, background);
                            if (image.isNull()) { qWarning("Canvas tile composition failed"); continue; }
                            auto bounds = geometry.tileBounds(tile);
                            painter.drawImage(QPointF(bounds->left, bounds->top), image);
                            if (plan.tiles.find(tile) != plan.tiles.end() && !m_invalidation.acknowledge(plan, tile, list, background)) {
                                qWarning("Canvas tile changed before acknowledgment");
                                eraseCacheTile(tile);
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
    qDebug() << "[CanvasViewport] refreshNow: done, update scheduled for" << dirty.toAlignedRect();
}
} // namespace komapaint
