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
#include <stdexcept>

namespace komapaint {
namespace {
const material::MaterialEngine& canvasMaterialEngine()
{
    static const material::PigmentLibrary pigments;
    static const material::MaterialEngine engine(pigments);
    return engine;
}

bool materialBatchFromMap(const QVariantMap& value, material::PaintBatch* output)
{
    if (!output) return false;
    const QString mediumName = value.value(QStringLiteral("mediumId")).toString();
    const QByteArray mediumBytes = mediumName.toUtf8();
    material::PaintBatch batch;
    batch.mediumId.assign(mediumBytes.constData(), static_cast<std::size_t>(mediumBytes.size()));
    if (!material::findMediumProfile(batch.mediumId)) return false;
    const QVariantList components = value.value(QStringLiteral("pigments")).toList();
    for (const QVariant& item : components) {
        const QVariantMap component = item.toMap();
        const QString pigmentName = component.value(QStringLiteral("pigmentId")).toString();
        const QByteArray pigmentBytes = pigmentName.toUtf8();
        const std::string pigmentId(pigmentBytes.constData(), static_cast<std::size_t>(pigmentBytes.size()));
        bool massOk = false;
        const double mass = component.value(QStringLiteral("mass")).toDouble(&massOk);
        if (!massOk || !std::isfinite(mass) || mass <= 0.0) return false;
        if (pigmentId.empty()) {
            bool colourOk = false;
            const auto colour = static_cast<std::uint32_t>(
                component.value(QStringLiteral("residualArgb")).toULongLong(&colourOk));
            if (!colourOk || (colour >> 24U) != 0xffU || !batch.addResidual(colour, mass)) return false;
        } else {
            if (!canvasMaterialEngine().pigments().find(pigmentId)
                || !batch.addPigment(pigmentId, mass)) return false;
        }
    }
    bool waterOk = false, binderOk = false, fillerOk = false;
    batch.water = value.value(QStringLiteral("water")).toDouble(&waterOk);
    batch.binder = value.value(QStringLiteral("binder")).toDouble(&binderOk);
    batch.filler = value.value(QStringLiteral("filler")).toDouble(&fillerOk);
    if (!waterOk || !binderOk || !fillerOk) return false;
    bool stateOk = true;
    if (value.contains(QStringLiteral("drying"))) batch.drying = value.value(QStringLiteral("drying")).toDouble(&stateOk);
    if (!stateOk) return false;
    if (value.contains(QStringLiteral("settling"))) batch.settling = value.value(QStringLiteral("settling")).toDouble(&stateOk);
    if (!stateOk) return false;
    const double total = batch.totalMass();
    batch.wetness = total > 0.0 ? std::clamp(batch.water / total, 0.0, 1.0) : 0.0;
    batch.consistency = total > 0.0
        ? std::clamp((batch.pigmentMass() + batch.binder + batch.filler) / total, 0.0, 1.0) : 0.0;
    if (!batch.valid() || total <= 0.0) return false;
    *output = std::move(batch);
    return true;
}

const material::MaterialRenderer& canvasMaterialRenderer()
{
    static const material::MaterialRenderer renderer(canvasMaterialEngine());
    return renderer;
}

bool prepareMaterialRenderLayers(std::vector<CompositeLayer>& layers, TilePoint tile,
                                 std::vector<std::unique_ptr<SparseTileStore>>& temporaryStores)
{
    try {
        temporaryStores.reserve(layers.size());
        for (auto& compositeLayer : layers) {
            Layer* source = compositeLayer.materialSource;
            if (!source || source->materialTiles().tileEntries(tile).empty()) continue;
            const auto& geometry = source->tiles().geometry();
            std::vector<std::uint32_t> rendered;
            if (!canvasMaterialRenderer().renderTile(source->materialTiles(), tile,
                    source->tiles().tilePixels(tile), material::gouacheProfile(),
                    material::absorbentPaper(), &rendered)) return false;
            auto output = std::make_unique<SparseTileStore>(geometry.width(), geometry.height(), geometry.tileSize());
            if (std::any_of(rendered.begin(), rendered.end(), [](std::uint32_t pixel) { return pixel != 0; })) {
                std::vector<SparseTileStore::TileSnapshot> snapshot;
                snapshot.push_back({tile, std::move(rendered)});
                if (output->restoreTiles(snapshot, false) != SparseTileStore::WriteResult::Changed) return false;
            }
            compositeLayer.store = output.get();
            temporaryStores.push_back(std::move(output));
        }
        return true;
    } catch (const std::bad_alloc&) { return false; }
      catch (const std::length_error&) { return false; }
}
} // namespace

CanvasViewport::CanvasViewport(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    setAntialiasing(false);
    m_materialClock.start();
    m_materialTimer = new QTimer(this);
    m_materialTimer->setInterval(200);
    connect(m_materialTimer, &QTimer::timeout, this, &CanvasViewport::tickMaterialSimulation);
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
        m_stroke.reset();
        m_materialStroke.reset();
        m_materialReservoir.reset();
        m_strokeLayer = nullptr;
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
    if (m_materialTimer) m_materialTimer->stop();
    cancelStroke();
    m_materialReservoir.reset();
    m_document = doc;
    m_invalidation.reset();
    clearCache();
    m_cacheBudget = doc ? doc->layers()->documentBudget() : nullptr;
    m_originInitialized = false;
    m_pendingView = false;
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
    refreshMaterialSimulation();
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

void CanvasViewport::refreshMaterialSimulation()
{
    if (!m_materialTimer) return;
    bool active = false;
    if (m_document) {
        const auto* model = m_document->layers();
        for (int row = 0; row < model->rowCount(); ++row) {
            if (!model->layerAt(row)->materialTiles().activeTilePoints().empty()) {
                active = true;
                break;
            }
        }
    }
    if (!active) {
        m_materialTimer->stop();
        return;
    }
    m_lastMaterialTick = m_materialClock.elapsed();
    if (!m_materialTimer->isActive()) m_materialTimer->start();
}

void CanvasViewport::tickMaterialSimulation()
{
    if (!m_document || m_materialStroke) {
        m_lastMaterialTick = m_materialClock.elapsed();
        if (!m_document) m_materialTimer->stop();
        return;
    }
    const qint64 now = m_materialClock.elapsed();
    const double elapsed = std::clamp(double(now - m_lastMaterialTick) / 1000.0, 0.0, 5.0);
    m_lastMaterialTick = now;
    bool active = false;
    auto* model = m_document->layers();
    for (int row = 0; row < model->rowCount(); ++row) {
        auto* layer = model->layerAt(row);
        if (layer->materialTiles().activeTilePoints().empty()) continue;
        active = true;
        const auto result = canvasMaterialEngine().advanceWetMaterial(
            layer->materialTiles(), elapsed, material::coldPressedPaper());
        if (result.failed)
            qWarning() << "[CanvasViewport] wet material update stopped early on layer" << row;
    }
    if (!active) m_materialTimer->stop();
}

void CanvasViewport::setDeskColour(const QColor& colour)
{
    if (!colour.isValid() || m_deskColour == colour) return;
    m_deskColour = colour;
    emit canvasColoursChanged();
    invalidateAll();
}

void CanvasViewport::setCheckerLight(const QColor& colour)
{
    if (!colour.isValid() || m_checkerLight == colour) return;
    m_checkerLight = colour;
    emit canvasColoursChanged();
    invalidateAll();
}

void CanvasViewport::setCheckerDark(const QColor& colour)
{
    if (!colour.isValid() || m_checkerDark == colour) return;
    m_checkerDark = colour;
    emit canvasColoursChanged();
    invalidateAll();
}
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
    eraseCacheTile({x, y});
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
    emit zoomChanged(); emit viewChanged(); invalidateAll();
}
void CanvasViewport::zoomAt(qreal x, qreal y, qreal factor)
{
    if (!std::isfinite(factor) || factor <= 0) return;
    const auto next = qBound(6.25, m_zoom * factor, 6400.0);
    if (qFuzzyCompare(next, m_zoom)) return;
    const QPointF doc = viewportToDocument(x, y);
    m_zoom = next; m_origin = QPointF(x, y) - doc * (next / 100);
    emit zoomChanged(); emit zoomRequested(next); emit viewChanged(); invalidateAll();
}
void CanvasViewport::panBy(qreal dx, qreal dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    m_origin.setX(qBound(-1.0e9, m_origin.x() + dx, 1.0e9));
    m_origin.setY(qBound(-1.0e9, m_origin.y() + dy, 1.0e9));
    emit viewChanged();
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
    m_pendingView = false;
    qDebug() << "[CanvasViewport] fit: zoom=" << m_zoom << "origin=" << m_origin
             << "docSize=" << m_document->width() << "x" << m_document->height();
    emit zoomChanged(); emit zoomRequested(m_zoom); emit viewChanged(); invalidateAll();
    qDebug() << "[CanvasViewport] fit: done";
}
QPointF CanvasViewport::viewCenter() const
{
    if (m_pendingView) return m_pendingCenter;
    return viewportToDocument(width() / 2, height() / 2);
}
void CanvasViewport::restoreView(qreal zoom, qreal centerX, qreal centerY)
{
    if (!std::isfinite(zoom) || !std::isfinite(centerX) || !std::isfinite(centerY)) return;
    zoom = qBound(6.25, zoom, 6400.0);
    const bool zoomDiffers = !qFuzzyCompare(m_zoom, zoom);
    m_zoom = zoom;
    if (width() > 0 && height() > 0) {
        m_origin = QPointF(width() / 2, height() / 2) - QPointF(centerX, centerY) * (zoom / 100);
        m_originInitialized = true;
        m_pendingView = false;
    } else {
        // No size yet: remember the request and apply it on the first resize.
        m_pendingView = true;
        m_pendingCenter = QPointF(centerX, centerY);
    }
    if (zoomDiffers) emit zoomChanged();
    emit viewChanged();
    invalidateAll();
}
void CanvasViewport::geometryChange(const QRectF& now, const QRectF& old)
{
    QQuickPaintedItem::geometryChange(now, old);
    if (now.size() == old.size()) return;
    if (m_pendingView && now.width() > 0 && now.height() > 0) {
        m_origin = QPointF(now.width() / 2, now.height() / 2) - m_pendingCenter * (m_zoom / 100);
        m_originInitialized = true;
        m_pendingView = false;
    } else if (m_originInitialized) {
        // Keep the same point of the artwork in the middle of the view when
        // the window or a dock is resized. Without this a project's stored
        // position would drift every time the layout changed.
        //
        // Every size counts, including zero: a dock layout rebuild hides the
        // canvas for a moment as the tree is re-created, and compensating one
        // half of that round trip but not the other would shift the view by
        // half a canvas per panel opened or closed.
        m_origin += QPointF((now.width() - old.width()) / 2, (now.height() - old.height()) / 2);
    }
    emit viewChanged();
    invalidateAll();
}
std::vector<CompositeLayer> CanvasViewport::layers() const
{
    std::vector<CompositeLayer> result;
    if (!m_document) return result;
    auto* model = m_document->layers();
    result.reserve(static_cast<std::size_t>(model->rowCount()));
    for (int i = 0; i < model->rowCount(); ++i) {
        auto* l = model->layerAt(i);
        result.push_back({&l->tiles(), l->isVisible(), l->opacity(), static_cast<BlendMode>(l->blendMode()), l});
    }
    return result;
}
QImage CanvasViewport::composedTile(TilePoint tile, const std::vector<CompositeLayer>& list, std::uint32_t background)
{
    auto found = m_cache.find(tile);
    if (found != m_cache.end()) { found->second.used = ++m_clock; return found->second.image; }
    if (list.empty()) return {};
    auto renderedLayers = list;
    std::vector<std::unique_ptr<SparseTileStore>> materialStores;
    if (!prepareMaterialRenderLayers(renderedLayers, tile, materialStores)) return {};
    auto composite = CpuTileCompositor::compose(list[0].store->geometry(), tile, renderedLayers, background);
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
    case R::HistoryLimit: detail = QStringLiteral("Undo history could not record this stroke."); break;
    case R::HistoryAllocationFailed: detail = QStringLiteral("Memory allocation for undo history failed."); break;
    case R::MemoryLimit:
        detail = QStringLiteral("This layer's pixel storage limit was reached (%1 of %2 MiB).")
            .arg(tileBytes / (1024.0 * 1024.0), 0, 'f', 1)
            .arg(tileBudget / (1024.0 * 1024.0), 0, 'f', 1);
        break;
    case R::DocumentMemoryLimit: detail = QStringLiteral("Not enough available memory for the document or undo history. Free memory and try again."); break;
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
    if (brush->traditionalMedia()) {
        if (!m_history->beginMaterialStroke(layer)) {
            emit strokeError(QStringLiteral("Could not start material stroke history; no paint was applied."));
            return false;
        }
        try {
            auto presets = material::defaultBrushDefinitions();
            material::PaintBatch mixerPaint;
            bool mixerPaintLoaded = false;
            if (!brush->erase() && !brush->materialMixture().isEmpty()) {
                if (!materialBatchFromMap(brush->materialMixture(), &mixerPaint)) {
                    m_history->cancelStroke();
                    emit strokeError(QStringLiteral("The mixer batch has an invalid medium or pigment composition."));
                    return false;
                }
                mixerPaintLoaded = true;
            }
            auto preset = std::find_if(presets.begin(), presets.end(),
                [mixerPaintLoaded, &mixerPaint](const auto& item) {
                    if (!mixerPaintLoaded) return item.id == "gouache-round";
                    return item.mediumId == mixerPaint.mediumId
                        && item.tip == material::TipGeometry::Round;
                });
            if (preset == presets.end()) {
                preset = std::find_if(presets.begin(), presets.end(), [](const auto& item) {
                    return item.id == "gouache-round";
                });
            }
            if (preset == presets.end()) {
                m_history->cancelStroke();
                emit strokeError(QStringLiteral("The default material brush is unavailable."));
                return false;
            }
            material::BrushDefinition definition = *preset;
            if (mixerPaintLoaded) {
                definition.initialPaint = std::move(mixerPaint);
                definition.mediumId = definition.initialPaint.mediumId;
            }
            const material::MediumProfile* selectedMedium = material::findMediumProfile(definition.mediumId);
            if (!selectedMedium) {
                m_history->cancelStroke();
                emit strokeError(QStringLiteral("The selected material medium is unavailable."));
                return false;
            }
            definition.size = brush->size();
            definition.hardness = brush->hardness() / 100.0;
            definition.spacingRatio = brush->spacing() / 100.0;
            definition.releaseRate *= brush->flow() / 100.0;
            definition.contactTransfer *= brush->opacity() / 100.0
                * (brush->erase() ? brush->flow() / 100.0 : 1.0);
            definition.autoReloadOnStroke = !brush->persistentReservoir();
            if (brush->textured()) {
                definition.tip = material::TipGeometry::Bristle;
                definition.bristleCount = 13;
                definition.bristleSpread = 0.55;
            }
            if (!brush->erase() && !mixerPaintLoaded) {
                const double alpha = color.alphaF();
                definition.initialPaint = material::makeResidualPaint(
                    color.rgba(), alpha, 0.3 * alpha, 0.2 * alpha, definition.mediumId);
            }
            if (!m_materialReservoir || !brush->persistentReservoir()
                || m_materialReservoir->capacity() != definition.reservoirCapacity) {
                m_materialReservoir = std::make_unique<material::BrushReservoir>(definition.reservoirCapacity);
                if (!brush->erase()
                    && !m_materialReservoir->load(definition.initialPaint, definition.initialPaint.totalMass())) {
                    m_materialReservoir.reset();
                    m_history->cancelStroke();
                    emit strokeError(QStringLiteral("The selected colour could not be loaded into the material brush."));
                    return false;
                }
            }
            m_materialStroke = std::make_unique<material::MaterialStroke>(layer->materialTiles(),
                std::move(definition), *m_materialReservoir, canvasMaterialEngine(),
                *selectedMedium, material::coldPressedPaper(), brush->erase());
            m_strokeLayer = layer;
            const auto point = viewportToDocument(x, y);
            m_stabilizer = StrokeStabilizer(brush->stabilization());
            m_stabilizer.begin({point.x(), point.y(), pressure});
            if (!m_materialStroke->begin({point.x(), point.y(), {pressure, 0.0, 0.0, 0.0}})) {
                const auto reason = m_materialStroke->failureReason();
                const bool rolledBack = m_history->cancelStroke();
                m_materialStroke.reset();
                m_materialReservoir.reset();
                m_strokeLayer = nullptr;
                reportStrokeFailure(reason, rolledBack, layer->materialTiles().allocatedBytes(), 0);
                return false;
            }
            return true;
        } catch (const std::bad_alloc&) {
            m_materialStroke.reset();
            m_materialReservoir.reset();
            m_strokeLayer = nullptr;
            const bool rolledBack = m_history->cancelStroke();
            reportStrokeFailure(SparseTileStore::WriteResult::AllocationFailed, rolledBack, 0, 0);
            return false;
        } catch (const std::length_error&) {
            m_materialStroke.reset();
            m_materialReservoir.reset();
            m_strokeLayer = nullptr;
            const bool rolledBack = m_history->cancelStroke();
            reportStrokeFailure(SparseTileStore::WriteResult::AllocationFailed, rolledBack, 0, 0);
            return false;
        }
    }
    if (!m_history->beginStroke(layer)) {
        emit strokeError(QStringLiteral("Could not start stroke history; no paint was applied."));
        return false;
    }
    BrushDefinition definition;
    definition.size = brush->size(); definition.opacity = brush->opacity() / 100.0;
    definition.flow = brush->flow() / 100.0; definition.spacing = brush->spacing() / 100.0;
    definition.tip = brush->textured() ? BrushTip::Textured : BrushTip::Round;
    definition.hardness = brush->hardness() / 100.0;
    definition.erase = brush->erase();
    definition.color = color.rgba();
    try { m_stroke = std::make_unique<BrushStroke>(layer->tiles(), definition); }
    catch (const std::bad_alloc&) { m_history->cancelStroke(); emit strokeError(QStringLiteral("Not enough memory to start a stroke.")); return false; }
    m_strokeLayer = layer;
    auto point = viewportToDocument(x, y);
    qDebug() << "[CanvasViewport] beginStroke: viewport(" << x << "," << y << ") -> document("
             << point.x() << "," << point.y() << ")";
    m_stabilizer = StrokeStabilizer(brush->stabilization());
    m_stabilizer.begin({point.x(), point.y(), pressure});
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

    if ((!m_stroke && !m_materialStroke) || !m_strokeLayer || !m_document) { qDebug() << "[CanvasViewport] continueStroke: no stroke/layer/document"; cancelStroke(); return; }
    if (m_strokeLayer->isLocked() || !m_strokeLayer->isVisible() ||
        m_document->layers()->layerAt(m_document->layers()->activeLayerIndex()) != m_strokeLayer) {
        qDebug() << "[CanvasViewport] continueStroke: layer changed or invalid";
        cancelStroke(); return; }
    auto point = viewportToDocument(x, y);
    qDebug() << "[CanvasViewport] continueStroke: viewport(" << x << "," << y << ") -> document("
             << point.x() << "," << point.y() << ")";
    // Stabilisation smooths the samples between pen-down and pen-up; endStroke()
    // then draws on to the real pen-up position.
    const InputSample sample = m_stabilizer.filter({point.x(), point.y(), pressure});
    const bool extended = m_materialStroke
        ? m_materialStroke->extend({sample.x, sample.y, {sample.pressure, 0.0, 0.0, 0.0}})
        : m_stroke->extend(sample);
    if (!extended) {
        const auto reason = m_materialStroke ? m_materialStroke->failureReason() : m_stroke->failureReason();
        const auto tileBytes = m_materialStroke ? m_strokeLayer->materialTiles().allocatedBytes()
                                               : m_strokeLayer->tiles().allocatedPixelBytes();
        const auto tileBudget = m_materialStroke ? 0 : m_strokeLayer->tiles().pixelBudget();
        const bool rolledBack = m_history && m_history->cancelStroke();
        reportStrokeFailure(reason, rolledBack, tileBytes, tileBudget);
    }
}
void CanvasViewport::endStroke(qreal x, qreal y, qreal pressure)
{
    qDebug() << "[CanvasViewport] endStroke() called:"
             << "viewport=(" << x << "," << y << ")" << "pressure=" << pressure;

    if ((!m_stroke && !m_materialStroke) || !m_strokeLayer || !m_document) { qDebug() << "[CanvasViewport] endStroke: no stroke/layer/document"; cancelStroke(); return; }
    if (m_strokeLayer->isLocked() || !m_strokeLayer->isVisible() ||
        m_document->layers()->layerAt(m_document->layers()->activeLayerIndex()) != m_strokeLayer) {
        qDebug() << "[CanvasViewport] endStroke: layer changed or invalid";
        cancelStroke(); return; }
    auto point = viewportToDocument(x, y);
    const bool materialStroke = bool(m_materialStroke);
    const bool ok = materialStroke
        ? m_materialStroke->end({point.x(), point.y(), {pressure, 0.0, 0.0, 0.0}})
        : m_stroke->end({point.x(), point.y(), pressure});
    const auto reason = materialStroke ? m_materialStroke->failureReason() : m_stroke->failureReason();
    const auto tileBytes = materialStroke ? m_strokeLayer->materialTiles().allocatedBytes()
                                         : m_strokeLayer->tiles().allocatedPixelBytes();
    const auto tileBudget = materialStroke ? 0 : m_strokeLayer->tiles().pixelBudget();
    if (!ok) {
        const bool rolledBack = m_history && m_history->cancelStroke();
        reportStrokeFailure(reason, rolledBack, tileBytes, tileBudget);
        m_stroke.reset();
        m_materialStroke.reset();
        m_materialReservoir.reset();
        m_strokeLayer = nullptr;
        refreshMaterialSimulation();
        return;
    }
    m_stroke.reset();
    m_materialStroke.reset();
    if (m_history && !m_history->commitStroke()) {
        if (m_history->strokeActive()) m_history->cancelStroke();
        if (m_strokeLayer && m_document) m_document->setModified(m_modifiedBeforeStroke);
        m_strokeLayer = nullptr;
        emit strokeError(QStringLiteral("Could not commit stroke history."));
    } else {
        if (materialStroke && m_strokeLayer)
            m_strokeLayer->materialTiles().reclaimUnreferenced();
        m_strokeLayer = nullptr;
    }
    refreshMaterialSimulation();
    qDebug() << "[CanvasViewport] endStroke: stroke ended, ok=" << ok;
}
void CanvasViewport::cancelStroke() {
    qDebug() << "[CanvasViewport] cancelStroke() called";
    if (m_history && m_history->strokeActive()) m_history->cancelStroke();
    m_stroke.reset();
    m_materialStroke.reset();
    m_strokeLayer = nullptr;
}

QVariant CanvasViewport::colorAt(qreal x, qreal y) const
{
    if (!m_document) return {};
    const QPointF point = viewportToDocument(x, y);
    if (!std::isfinite(point.x()) || !std::isfinite(point.y())) return {};
    if (point.x() < 0 || point.y() < 0 || point.x() >= m_document->width() || point.y() >= m_document->height()) return {};
    const auto list = layers();
    if (list.empty()) return {};
    const auto& geometry = list.front().store->geometry();
    const auto address = geometry.address({static_cast<std::int64_t>(std::floor(point.x())),
                                           static_cast<std::int64_t>(std::floor(point.y()))});
    if (!address) return {};
    // Composed directly, without the viewport cache: sampling must not disturb
    // the cache or the invalidation bookkeeping of the display.
    const auto background = m_document->hasTransparentBackground() ? 0U : m_document->backgroundColor().rgba();
    auto renderedLayers = list;
    std::vector<std::unique_ptr<SparseTileStore>> materialStores;
    if (!prepareMaterialRenderLayers(renderedLayers, address->tile, materialStores)) return {};
    const auto tile = CpuTileCompositor::compose(geometry, address->tile, renderedLayers, background);
    if (!tile) return {};
    const auto index = static_cast<std::size_t>(address->local.y) * static_cast<std::size_t>(tile->width)
                     + static_cast<std::size_t>(address->local.x);
    if (index >= tile->pixels.size()) return {};
    const std::uint32_t pixel = tile->pixels[index];   // premultiplied ARGB
    const unsigned alpha = (pixel >> 24) & 255U;
    if (alpha == 0) return {};
    auto channel = [&](unsigned shift) { return static_cast<int>(std::min(255U, (((pixel >> shift) & 255U) * 255U + alpha / 2) / alpha)); };
    return QColor(channel(16), channel(8), channel(0));
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
            // Reported after this function returns: the frame lock is held here.
            QMetaObject::invokeMethod(this, &CanvasViewport::viewChanged, Qt::QueuedConnection);
            qDebug() << "[CanvasViewport] refreshNow: auto-centered origin=" << m_origin;
        }
        QPainter painter(&m_frame);
        painter.setClipRect(dirty);
        painter.fillRect(dirty, m_deskColour);
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
            checks.fill(m_checkerLight);
            { QPainter cp(&checks); cp.fillRect(0, 0, 8, 8, m_checkerDark); cp.fillRect(8, 8, 8, 8, m_checkerDark); }
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
