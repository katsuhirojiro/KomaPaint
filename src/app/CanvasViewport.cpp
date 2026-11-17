#include "CanvasViewport.h"
#include "BrushSettings.h"
#include "../input/QtPointerAdapter.h"
#include "../input/TabletInputSettings.h"
#include "../history/UndoStack.h"
#include "../canvas/CanvasDocument.h"
#include "../layers/Layer.h"
#include "../layers/LayerModel.h"
#include <QCoreApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QPainter>
#include <QQuickWindow>
#include <QTabletEvent>
#include <QTimer>
#include <QTouchEvent>
#include <QtMath>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>

namespace komapaint {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::uint64_t steadyNowNs() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

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

bool prepareMaterialRenderLayers(const std::vector<CompositeLayer>& layers, TilePoint tile,
                                 const material::SurfaceProfile& surface,
                                 std::vector<std::vector<std::uint32_t>>& scratchBuffers,
                                 std::vector<std::span<const std::uint32_t>>& overrides)
{
    try {
        const std::size_t count = layers.size();
        if (scratchBuffers.size() < count) scratchBuffers.resize(count);
        overrides.assign(count, {});
        for (std::size_t i = 0; i < count; ++i) {
            const auto& compositeLayer = layers[i];
            if (!compositeLayer.visible || compositeLayer.opacity == 0.0) continue;
            Layer* source = compositeLayer.materialSource;
            if (!source || source->materialTiles().tileEntries(tile).empty()) continue;
            if (!canvasMaterialRenderer().renderTile(source->materialTiles(), tile,
                    source->tiles().tilePixels(tile), material::gouacheProfile(),
                    surface, &scratchBuffers[i])) return false;
            overrides[i] = scratchBuffers[i];
        }
        return true;
    } catch (const std::bad_alloc&) { return false; }
      catch (const std::length_error&) { return false; }
}

QPointF rotateAround(QPointF pt, QPointF center, qreal degrees) noexcept
{
    if (degrees == 0.0) return pt;
    const qreal rad = degrees * (kPi / 180.0);
    const qreal c = std::cos(rad);
    const qreal s = std::sin(rad);
    const qreal dx = pt.x() - center.x();
    const qreal dy = pt.y() - center.y();
    return QPointF(center.x() + dx * c - dy * s, center.y() + dx * s + dy * c);
}

QString barrelActionId(input::BarrelAction action)
{
    switch (action) {
    case input::BarrelAction::RightClick: return QStringLiteral("right_click");
    case input::BarrelAction::MiddlePan: return QStringLiteral("pan");
    case input::BarrelAction::Eyedropper: return QStringLiteral("eyedropper");
    case input::BarrelAction::EraserToggle: return QStringLiteral("eraser");
    case input::BarrelAction::Undo: return QStringLiteral("undo");
    case input::BarrelAction::None: default: return QStringLiteral("none");
    }
}

} // namespace

CanvasViewport::CanvasViewport(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    setAntialiasing(false);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptTouchEvents(true);
    setAcceptHoverEvents(true);

    m_touchScratch.reserve(16);

    m_materialClock.start();
    m_materialTimer = new QTimer(this);
    m_materialTimer->setInterval(200);
    connect(m_materialTimer, &QTimer::timeout, this, &CanvasViewport::tickMaterialSimulation);

    m_diagnosticsTimer = new QTimer(this);
    m_diagnosticsTimer->setInterval(120);
    connect(m_diagnosticsTimer, &QTimer::timeout, this, [this] {
        m_arbiter.checkStaleStroke(steadyNowNs());
        if ((m_stroke || m_materialStroke) && !m_arbiter.strokeActive()) {
            recoverOpenStroke(true);
        }
        if (m_inputSettings && m_arbiter.telemetry().hasLastSample()) {
            const bool wtActive = m_winTab && m_winTab->isContextOpen();
            m_inputSettings->updateLiveDiagnostics(
                m_arbiter.telemetry().lastSample(), m_arbiter.telemetry(), wtActive);
        }
    });
    m_diagnosticsTimer->start();

    m_winTab = std::make_unique<input::WinTabBackend>(this);
    m_winTab->setPacketCallback([this](const input::RawPointerEvent& raw) {
        injectRawPointerEvent(raw);
    });
    m_winTab->setProximityCallback([this](std::uint64_t deviceId, bool enter, std::uint64_t tsNs) {
        m_arbiter.setProximity(deviceId, enter, input::InputBackend::WinTab, tsNs);
        if (!enter) {
            if (m_arbiter.strokeActive() && m_arbiter.activeBackend() == input::InputBackend::WinTab) {
                recoverOpenStroke(true);
            }
            m_cursorStylusInProximity = false;
            clearCursorState();
        } else {
            m_cursorStylusInProximity = true;
            emit cursorStateChanged();
        }
    });

    for (const auto& dev : input::QtPointerAdapter::enumerateDevices()) {
        m_arbiter.registerDevice(dev);
    }
}

CanvasViewport::~CanvasViewport()
{
    if (m_winTab) m_winTab->detachWindow();
    clearCache();
}

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
        m_arbiter.markStrokeEndedExternally();
        if (m_document) m_document->setModified(m_modifiedBeforeStroke);
        m_invalidation.reset(); clearCache();
        invalidateAll();
    });
    emit historyChanged();
}

QObject* CanvasViewport::inputSettings() const { return m_inputSettings.data(); }
void CanvasViewport::setInputSettings(QObject* settings)
{
    auto* next = qobject_cast<input::TabletInputSettings*>(settings);
    if (m_inputSettings == next) return;
    if (m_inputSettings) disconnect(m_inputSettings, nullptr, this, nullptr);
    m_inputSettings = next;
    if (m_inputSettings) {
        connect(m_inputSettings, &input::TabletInputSettings::configChanged,
                this, &CanvasViewport::syncInputConfig);
        syncInputConfig();
    }
    emit inputSettingsChanged();
}

QObject* CanvasViewport::brushSettings() const { return m_brushSettings.data(); }
void CanvasViewport::setBrushSettings(QObject* settings)
{
    auto* next = qobject_cast<BrushSettings*>(settings);
    if (m_brushSettings == next) return;
    m_brushSettings = next;
    emit brushSettingsChanged();
}

void CanvasViewport::setBrushColor(const QColor& color)
{
    if (!color.isValid() || m_brushColor == color) return;
    m_brushColor = color;
    emit brushColorChanged();
}

void CanvasViewport::setActiveTool(const QString& tool)
{
    if (m_activeTool == tool) return;
    m_activeTool = tool;
    emit activeToolChanged();
}

void CanvasViewport::syncInputConfig()
{
    if (!m_inputSettings) return;
    m_arbiter.setConfig(m_inputSettings->toInputConfig());
    syncWinTabAttachment();
}

void CanvasViewport::syncWinTabAttachment()
{
    if (!m_winTab) return;
    bool enableWinTab = false;
    if (m_inputSettings) {
        const QString mode = m_inputSettings->windowsTabletMode();
        enableWinTab = (mode == QStringLiteral("wintab") || mode == QStringLiteral("auto"));
    } else {
        enableWinTab = true;
    }
    m_winTab->setEnabled(enableWinTab);
    if (enableWinTab && window()) {
        m_winTab->attachWindow(window());
    } else if (!enableWinTab) {
        m_winTab->detachWindow();
    }
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
    if (m_materialTimer) m_materialTimer->stop();
    cancelStroke();
    m_materialReservoir.reset();
    m_document = doc;
    m_invalidation.reset();
    clearCache();
    m_cacheBudget = doc ? doc->layers()->documentBudget() : nullptr;
    m_originInitialized = false;
    m_pendingView = false;

    if (doc) {
        connect(doc, &CanvasDocument::backgroundChanged, this, &CanvasViewport::invalidateAll);
        connect(doc, &CanvasDocument::surfaceChanged, this, [this] {
            m_invalidation.reset(); clearCache(); invalidateAll();
        });
        connect(doc, &CanvasDocument::sizeChanged, this, [this] {
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
    emit documentChanged();
    invalidateAll();
    refreshMaterialSimulation();
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
    const auto& surface = m_document->surfaceProfile();
    for (int row = 0; row < model->rowCount(); ++row) {
        auto* layer = model->layerAt(row);
        if (layer->materialTiles().activeTilePoints().empty()) continue;
        active = true;
        const auto result = canvasMaterialEngine().advanceWetMaterial(
            layer->materialTiles(), elapsed, surface);
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
    if (!m_document) return;
    eraseCacheTile({x, y});
    const qreal side = m_document->layers()->layerAt(0)->tiles().geometry().tileSize();
    if (m_viewRotation == 0.0) {
        const qreal scale = m_zoom / 100;
        QRectF rect(m_origin.x() + x * side * scale, m_origin.y() + y * side * scale, side * scale, side * scale);
        schedule(rect.adjusted(-2, -2, 2, 2));
    } else {
        const QPointF p0 = documentToViewport(x * side, y * side);
        const QPointF p1 = documentToViewport((x + 1) * side, y * side);
        const QPointF p2 = documentToViewport(x * side, (y + 1) * side);
        const QPointF p3 = documentToViewport((x + 1) * side, (y + 1) * side);
        const qreal minX = std::min({p0.x(), p1.x(), p2.x(), p3.x()});
        const qreal maxX = std::max({p0.x(), p1.x(), p2.x(), p3.x()});
        const qreal minY = std::min({p0.y(), p1.y(), p2.y(), p3.y()});
        const qreal maxY = std::max({p0.y(), p1.y(), p2.y(), p3.y()});
        schedule(QRectF(minX - 2, minY - 2, (maxX - minX) + 4, (maxY - minY) + 4));
    }
}

void CanvasViewport::setZoom(qreal zoom)
{
    if (!std::isfinite(zoom)) return;
    zoom = qBound(6.25, zoom, 6400.0);
    if (qFuzzyCompare(m_zoom, zoom)) return;
    QPointF center(width() / 2, height() / 2);
    const QPointF point = viewportToDocument(center.x(), center.y());
    m_zoom = zoom;
    if (m_viewRotation == 0.0) {
        m_origin = center - point * (zoom / 100);
    } else {
        m_origin = center - point * (zoom / 100);
    }
    emit zoomChanged(); emit viewChanged(); invalidateAll();
}

void CanvasViewport::setViewRotation(qreal degrees)
{
    if (!std::isfinite(degrees)) return;
    qreal norm = std::fmod(degrees, 360.0);
    if (norm > 180.0) norm -= 360.0;
    if (norm <= -180.0) norm += 360.0;
    if (std::abs(norm) < 0.05) norm = 0.0;
    if (qFuzzyCompare(m_viewRotation + 1000.0, norm + 1000.0)) return;
    m_viewRotation = norm;
    emit viewRotationChanged();
    emit viewChanged();
    invalidateAll();
}

void CanvasViewport::resetRotation()
{
    setViewRotation(0.0);
}

void CanvasViewport::zoomAt(qreal x, qreal y, qreal factor)
{
    if (!std::isfinite(factor) || factor <= 0) return;
    const auto next = qBound(6.25, m_zoom * factor, 6400.0);
    if (qFuzzyCompare(next, m_zoom)) return;
    const QPointF doc = viewportToDocument(x, y);
    const QPointF center(width() / 2, height() / 2);
    const QPointF unrotated = m_viewRotation == 0.0
        ? QPointF(x, y)
        : rotateAround(QPointF(x, y), center, -m_viewRotation);
    m_zoom = next;
    m_origin = unrotated - doc * (next / 100);
    emit zoomChanged(); emit zoomRequested(next); emit viewChanged(); invalidateAll();
}

void CanvasViewport::panBy(qreal dx, qreal dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    QPointF delta(dx, dy);
    if (m_viewRotation != 0.0) {
        delta = rotateAround(delta, QPointF(0, 0), -m_viewRotation);
    }
    m_origin.setX(qBound(-1.0e9, m_origin.x() + delta.x(), 1.0e9));
    m_origin.setY(qBound(-1.0e9, m_origin.y() + delta.y(), 1.0e9));
    emit viewChanged();
    invalidateAll();
}

QPointF CanvasViewport::viewportToDocument(qreal x, qreal y) const
{
    const QPointF pt = m_viewRotation == 0.0
        ? QPointF(x, y)
        : rotateAround(QPointF(x, y), QPointF(width() / 2, height() / 2), -m_viewRotation);
    return (pt - m_origin) / (m_zoom / 100);
}

QPointF CanvasViewport::documentToViewport(qreal x, qreal y) const
{
    const QPointF unrotated = m_origin + QPointF(x, y) * (m_zoom / 100);
    return m_viewRotation == 0.0
        ? unrotated
        : rotateAround(unrotated, QPointF(width() / 2, height() / 2), m_viewRotation);
}

void CanvasViewport::fit()
{
    if (!m_document || width() <= 0 || height() <= 0) return;

    m_zoom = qBound(6.25, 100 * std::min(width() / m_document->width(), height() / m_document->height()), 6400.0);
    m_origin = QPointF((width() - m_document->width() * m_zoom / 100) / 2, (height() - m_document->height() * m_zoom / 100) / 2);
    m_originInitialized = true;
    m_pendingView = false;
    emit zoomChanged(); emit zoomRequested(m_zoom); emit viewChanged(); invalidateAll();
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
        m_origin += QPointF((now.width() - old.width()) / 2, (now.height() - old.height()) / 2);
    }
    emit viewChanged();
    invalidateAll();
}

void CanvasViewport::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickPaintedItem::itemChange(change, value);
    if (change == ItemSceneChange) {
        if (value.window) {
            connect(value.window, &QQuickWindow::activeChanged, this, [this] {
                if (window() && !window()->isActive()) {
                    if (m_arbiter.handleFocusLost()) {
                        recoverOpenStroke(true);
                    }
                    m_barrelPanActive = false;
                    m_barrelEyedropperActive = false;
                    m_touchNavActive = false;
                    clearCursorState();
                }
            });
            syncWinTabAttachment();
        } else if (m_winTab) {
            m_winTab->detachWindow();
        }
    }
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
    const auto surface = m_document ? m_document->surfaceProfile() : material::coldPressedPaper();
    if (!prepareMaterialRenderLayers(list, tile, surface, m_materialScratchBuffers, m_materialOverrides))
        return {};
    std::int64_t tileWidth = 0;
    std::int64_t tileHeight = 0;
    if (!CpuTileCompositor::composeInto(list[0].store->geometry(), tile, list, background,
                                        m_materialOverrides, m_composeTilePixels, tileWidth, tileHeight))
        return {};
    QImage image(static_cast<int>(tileWidth), static_cast<int>(tileHeight), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    const std::size_t rowBytes = static_cast<std::size_t>(image.width()) * sizeof(std::uint32_t);
    for (int row = 0; row < image.height(); ++row)
        std::memcpy(image.scanLine(row),
                    m_composeTilePixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width()),
                    rowBytes);
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

bool CanvasViewport::activeToolIsPaintable() const
{
    return m_activeTool != QStringLiteral("hand") &&
           m_activeTool != QStringLiteral("zoom") &&
           m_activeTool != QStringLiteral("eyedropper");
}

void CanvasViewport::updateCursorState(const input::PointerSample& sample, bool visible)
{
    const bool hoverEnabled = !m_inputSettings || m_inputSettings->hoverOutlineEnabled();
    const bool tiltEnabled = !m_inputSettings || m_inputSettings->tiltCursorEnabled();
    const bool nextVisible = visible && hoverEnabled;
    const QPointF nextPos(sample.viewX, sample.viewY);
    const qreal nextTiltX = tiltEnabled && sample.capabilities.hasTilt ? sample.tiltX : 0.0;
    const qreal nextTiltY = tiltEnabled && sample.capabilities.hasTilt ? sample.tiltY : 0.0;
    const qreal nextRot = sample.capabilities.hasRotation ? sample.rotationRadians * (180.0 / kPi) : 0.0;
    const bool nextHasTilt = tiltEnabled && sample.capabilities.hasTilt;
    const bool nextEraser = sample.eraserActive;
    const bool nextProx = sample.inProximity;

    if (m_cursorVisible == nextVisible && m_cursorPos == nextPos &&
        qFuzzyCompare(m_cursorTiltX + 100.0, nextTiltX + 100.0) &&
        qFuzzyCompare(m_cursorTiltY + 100.0, nextTiltY + 100.0) &&
        qFuzzyCompare(m_cursorRotationDeg + 400.0, nextRot + 400.0) &&
        m_cursorHasTilt == nextHasTilt &&
        m_cursorEraserActive == nextEraser &&
        m_cursorStylusInProximity == nextProx) {
        return;
    }
    m_cursorVisible = nextVisible;
    m_cursorPos = nextPos;
    m_cursorTiltX = nextTiltX;
    m_cursorTiltY = nextTiltY;
    m_cursorRotationDeg = nextRot;
    m_cursorHasTilt = nextHasTilt;
    m_cursorEraserActive = nextEraser;
    m_cursorStylusInProximity = nextProx;
    emit cursorStateChanged();
}

void CanvasViewport::clearCursorState()
{
    if (!m_cursorVisible && !m_cursorEraserActive && !m_cursorStylusInProximity) return;
    m_cursorVisible = false;
    m_cursorEraserActive = false;
    m_cursorStylusInProximity = false;
    emit cursorStateChanged();
}

bool CanvasViewport::beginStrokeSample(const input::PointerSample& sample, BrushSettings* brush,
                                       const QColor& color, bool eraseOverride)
{
    clearStrokeObjects();
    if (!m_document || !m_history || !brush || !color.isValid())
        return false;

    auto* model = m_document->layers();
    auto* layer = model->layerAt(model->activeLayerIndex());

    if (!layer || layer->isLocked() || !layer->isVisible()) {
        emit strokeError(QStringLiteral("Select a visible, unlocked layer to paint."));
        return false;
    }
    if (!brush->isRealisticEngine() && brush->traditionalMedia()) {
        emit strokeError(QStringLiteral("This brush uses physical materials and requires the Realistic brush engine. "
                                        "Switch to Realistic in Settings → Brush Engine, or select a Digital brush preset."));
        return false;
    }
    const bool effectiveErase = eraseOverride || brush->erase();
    m_modifiedBeforeStroke = m_document->isModified();
    const int effectiveStab = brush->stabilization() > 0
        ? brush->stabilization()
        : (m_inputSettings ? m_inputSettings->stabilizationDefault() : 0);

    if (brush->isRealisticEngine()) {
        if (!m_history->beginMaterialStroke(layer)) {
            emit strokeError(QStringLiteral("Could not start material stroke history; no paint was applied."));
            return false;
        }
        try {
            auto presets = material::defaultBrushDefinitions();
            material::PaintBatch mixerPaint;
            bool mixerPaintLoaded = false;
            if (!effectiveErase && !brush->materialMixture().isEmpty()) {
                if (!materialBatchFromMap(brush->materialMixture(), &mixerPaint)) {
                    m_history->cancelStroke();
                    emit strokeError(QStringLiteral("The mixer batch has an invalid medium or pigment composition."));
                    return false;
                }
                mixerPaintLoaded = true;
            }
            const QByteArray requestedPreset = brush->materialPresetId().toUtf8();
            const std::string requestedMedium = brush->mediumId().toStdString();
            auto preset = std::find_if(presets.begin(), presets.end(),
                [mixerPaintLoaded, &mixerPaint, &requestedPreset](const auto& item) {
                    if (!mixerPaintLoaded) return item.id == requestedPreset.constData();
                    return item.id == requestedPreset.constData()
                        && item.mediumId == mixerPaint.mediumId
                        && !item.initialPaint.pigments.empty();
                });
            if (mixerPaintLoaded && preset == presets.end()) {
                preset = std::find_if(presets.begin(), presets.end(),
                    [&mixerPaint](const auto& item) {
                        return item.mediumId == mixerPaint.mediumId
                            && item.tip == material::TipGeometry::Round
                            && !item.initialPaint.pigments.empty();
                    });
            }
            if (!mixerPaintLoaded && preset != presets.end()
                && preset->mediumId != requestedMedium) {
                const std::string fallbackId = requestedMedium == "watercolour"
                    ? "watercolour-round" : "gouache-round";
                auto mediumMatch = std::find_if(presets.begin(), presets.end(),
                    [&fallbackId](const auto& item) { return item.id == fallbackId; });
                if (mediumMatch != presets.end()) preset = mediumMatch;
            }
            if (preset == presets.end()) {
                preset = std::find_if(presets.begin(), presets.end(), [&requestedMedium](const auto& item) {
                    return item.mediumId == requestedMedium && item.tip == material::TipGeometry::Round;
                });
            }
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
            definition.reservoirCapacity = definition.effectiveReservoirCapacity();
            definition.hardness = brush->hardness() / 100.0;
            definition.spacingRatio = brush->spacing() / 100.0;
            definition.aspect = brush->aspectRatio() / 100.0;
            definition.rotationRadians = brush->tipAngle() * (kPi / 180.0);
            definition.followDirection = brush->followDirection();
            definition.initialLoad = brush->paintLoad() / 100.0;
            definition.depletionRate = brush->depletion() / 100.0;
            definition.pressureTransfer = brush->pressureToAmount() / 100.0;
            definition.contactPressureScale = brush->pressureToContact() / 100.0;
            definition.paintBody = brush->paintBody() / 100.0;
            definition.dryingRate = brush->dryingSpeed() / 100.0;
            definition.edgeAccumulation = brush->edgeAccumulation() / 100.0;
            definition.toothResponse = brush->toothResponse() / 100.0;
            definition.granulationResponse = brush->granulation() / 100.0;
            const double pickupFactor = brush->pickup() / 100.0;
            definition.pickupRate = pickupFactor;
            definition.paintLiftability = std::clamp(0.15 + pickupFactor * 0.85, 0.0, 1.0);
            definition.rewetting = pickupFactor * 0.9;
            definition.releaseRate *= brush->flow() / 100.0;
            definition.contactTransfer *= brush->opacity() / 100.0
                * (effectiveErase ? brush->flow() / 100.0 : 1.0);
            definition.autoReloadOnStroke = !brush->persistentReservoir();
            if (brush->textured() && definition.tip == material::TipGeometry::Round) {
                definition.tip = material::TipGeometry::Bristle;
                definition.bristleCount = 13;
                definition.bristleSpread = 0.55;
                definition.textureStrength = std::max(definition.textureStrength, 0.45);
            }
            if (!effectiveErase && !mixerPaintLoaded) {
                if (preset->initialPaint.pigments.empty()) {
                    definition.initialPaint = preset->initialPaint;
                    const double waterScale = std::max(0.1, brush->waterAmount() / 100.0);
                    definition.initialPaint.water = std::max(0.25, preset->initialPaint.water * waterScale);
                    definition.initialPaint.wetness = 1.0;
                    definition.initialPaint.consistency = 0.0;
                } else {
                    const double alpha = std::max(0.05, double(color.alphaF()));
                    const double concentration = brush->pigmentConcentration() / 100.0;
                    const double waterRatio = brush->waterAmount() / 100.0;
                    const double bodyRatio = brush->paintBody() / 100.0;
                    if (definition.mediumId == "watercolour") {
                        const double pigmentMass = alpha * (0.35 + 0.90 * concentration);
                        const double binderMass = alpha * (0.04 + 0.14 * bodyRatio);
                        const double waterMass = alpha * (0.40 + 2.20 * waterRatio);
                        definition.initialPaint = material::makeResidualPaint(
                            color.rgba(), pigmentMass, waterMass, binderMass, definition.mediumId);
                    } else {
                        const double pigmentMass = alpha * (0.35 + 1.25 * concentration);
                        const double binderMass = alpha * (0.12 + 0.45 * bodyRatio);
                        const double waterMass = alpha * (0.08 + 1.60 * waterRatio);
                        definition.initialPaint = material::makeResidualPaint(
                            color.rgba(), pigmentMass, waterMass, binderMass, definition.mediumId);
                        if (bodyRatio > 0.0)
                            definition.initialPaint.addFiller(alpha * 0.22 * bodyRatio);
                    }
                }
            }
            const bool mixtureUpdated = mixerPaintLoaded
                && brush->mixtureRevision() != m_loadedMixtureRevision;
            if (!m_materialReservoir || !brush->persistentReservoir() || mixtureUpdated
                || m_materialReservoir->capacity() != definition.reservoirCapacity) {
                m_materialReservoir = std::make_unique<material::BrushReservoir>(definition.reservoirCapacity);
                if (!effectiveErase && !m_materialReservoir->reload(definition)) {
                    m_materialReservoir.reset();
                    m_history->cancelStroke();
                    emit strokeError(QStringLiteral("The selected colour could not be loaded into the material brush."));
                    return false;
                }
                if (mixerPaintLoaded) m_loadedMixtureRevision = brush->mixtureRevision();
            }
            m_materialStroke = std::make_unique<material::MaterialStroke>(layer->materialTiles(),
                std::move(definition), *m_materialReservoir, canvasMaterialEngine(),
                *selectedMedium, m_document->surfaceProfile(), effectiveErase);
            m_strokeLayer = layer;
            m_stabilizer = StrokeStabilizer(effectiveStab);
            InputSample initSample{
                sample.docX, sample.docY, sample.pressure,
                sample.tiltX, sample.tiltY, sample.rotationRadians,
                sample.tangentialPressure, sample.capabilities.hasTilt, sample.capabilities.hasRotation
            };
            m_stabilizer.begin(initSample);
            const material::BrushInput mInput{
                sample.pressure,
                sample.speedNormalized,
                sample.capabilities.hasTilt ? sample.tiltMagnitude : 0.0,
                sample.capabilities.hasRotation ? sample.rotationRadians : 0.0
            };
            if (!m_materialStroke->begin({sample.docX, sample.docY, mInput})) {
                const auto reason = m_materialStroke->failureReason();
                const bool rolledBack = m_history->cancelStroke();
                m_materialStroke.reset();
                m_materialReservoir.reset();
                m_strokeLayer = nullptr;
                reportStrokeFailure(reason, rolledBack, layer->materialTiles().allocatedBytes(), 0);
                return false;
            }
            m_lastStrokeSample = sample;
            m_hasLastStrokeSample = true;
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
    definition.size = brush->size();
    definition.opacity = brush->opacity() / 100.0;
    definition.flow = brush->flow() / 100.0;
    definition.spacing = brush->spacing() / 100.0;
    const QString tipStyle = brush->tipStyle();
    if (tipStyle == QStringLiteral("bristle")) definition.tip = BrushTip::Bristle;
    else if (tipStyle == QStringLiteral("graphite")) definition.tip = BrushTip::Graphite;
    else if (tipStyle == QStringLiteral("stipple")) definition.tip = BrushTip::Stipple;
    else if (tipStyle == QStringLiteral("textured") || brush->textured()) definition.tip = BrushTip::Textured;
    else definition.tip = BrushTip::Round;
    definition.hardness = brush->hardness() / 100.0;
    definition.aspectRatio = brush->aspectRatio() / 100.0;
    definition.angleRadians = qDegreesToRadians(double(brush->tipAngle()));
    definition.followDirection = brush->followDirection();
    definition.pressureSize = brush->pressureSize() / 100.0;
    definition.pressureOpacity = brush->pressureOpacity() / 100.0;
    definition.pressureFlow = brush->pressureFlow() / 100.0;
    definition.pressureGamma = brush->pressureGamma() / 100.0;
    definition.minSizeRatio = brush->minSizeRatio() / 100.0;
    definition.tiltSize = brush->tiltSize() / 100.0;
    definition.tiltAngle = brush->tiltAngle();
    definition.stylusRotation = brush->stylusRotation();
    definition.blendStrength = brush->blendStrength() / 100.0;
    definition.colorMix = brush->colorMix() / 100.0;
    definition.erase = effectiveErase;
    definition.color = color.rgba();

    try { m_stroke = std::make_unique<BrushStroke>(layer->tiles(), definition); }
    catch (const std::bad_alloc&) {
        m_history->cancelStroke();
        emit strokeError(QStringLiteral("Not enough memory to start a stroke."));
        return false;
    }
    m_strokeLayer = layer;
    m_stabilizer = StrokeStabilizer(effectiveStab);
    InputSample initSample{
        sample.docX, sample.docY, sample.pressure,
        sample.tiltX, sample.tiltY, sample.rotationRadians,
        sample.tangentialPressure, sample.capabilities.hasTilt, sample.capabilities.hasRotation
    };
    m_stabilizer.begin(initSample);
    if (!m_stroke->begin(initSample)) {
        const auto reason = m_stroke->failureReason();
        const auto tileBytes = layer->tiles().allocatedPixelBytes();
        const auto tileBudget = layer->tiles().pixelBudget();
        const bool rolledBack = m_history->cancelStroke();
        m_stroke.reset();
        m_strokeLayer = nullptr;
        reportStrokeFailure(reason, rolledBack, tileBytes, tileBudget);
        return false;
    }
    m_lastStrokeSample = sample;
    m_hasLastStrokeSample = true;
    return true;
}

void CanvasViewport::continueStrokeSample(const input::PointerSample& sample)
{
    if ((!m_stroke && !m_materialStroke) || !m_strokeLayer || !m_document) { cancelStroke(); return; }
    if (m_strokeLayer->isLocked() || !m_strokeLayer->isVisible() ||
        m_document->layers()->layerAt(m_document->layers()->activeLayerIndex()) != m_strokeLayer) {
        cancelStroke(); return;
    }
    const InputSample filtered = m_stabilizer.filter({
        sample.docX, sample.docY, sample.pressure,
        sample.tiltX, sample.tiltY, sample.rotationRadians,
        sample.tangentialPressure, sample.capabilities.hasTilt, sample.capabilities.hasRotation
    });
    const double tiltMag = filtered.hasTilt
        ? std::clamp(std::hypot(filtered.tiltX, filtered.tiltY) / 90.0, 0.0, 1.0)
        : 0.0;
    const bool extended = m_materialStroke
        ? m_materialStroke->extend({filtered.x, filtered.y,
            {filtered.pressure, sample.speedNormalized, tiltMag,
             filtered.hasRotation ? filtered.rotationRadians : 0.0}})
        : m_stroke->extend(filtered);
    if (!extended) {
        const auto reason = m_materialStroke ? m_materialStroke->failureReason() : m_stroke->failureReason();
        const auto tileBytes = m_materialStroke ? m_strokeLayer->materialTiles().allocatedBytes()
                                               : m_strokeLayer->tiles().allocatedPixelBytes();
        const auto tileBudget = m_materialStroke ? 0 : m_strokeLayer->tiles().pixelBudget();
        const bool rolledBack = m_history && m_history->cancelStroke();
        m_stroke.reset();
        m_materialStroke.reset();
        m_strokeLayer = nullptr;
        m_arbiter.markStrokeEndedExternally();
        reportStrokeFailure(reason, rolledBack, tileBytes, tileBudget);
        return;
    }
    m_lastStrokeSample = sample;
    m_hasLastStrokeSample = true;
}

void CanvasViewport::endStrokeSample(const input::PointerSample& sample)
{
    if ((!m_stroke && !m_materialStroke) || !m_strokeLayer || !m_document) { cancelStroke(); return; }
    if (m_strokeLayer->isLocked() || !m_strokeLayer->isVisible() ||
        m_document->layers()->layerAt(m_document->layers()->activeLayerIndex()) != m_strokeLayer) {
        cancelStroke(); return;
    }
    const bool materialStroke = bool(m_materialStroke);
    const InputSample endSample{
        sample.docX, sample.docY, sample.pressure,
        sample.tiltX, sample.tiltY, sample.rotationRadians,
        sample.tangentialPressure, sample.capabilities.hasTilt, sample.capabilities.hasRotation
    };
    const bool ok = materialStroke
        ? m_materialStroke->end({sample.docX, sample.docY,
            {sample.pressure, sample.speedNormalized,
             sample.capabilities.hasTilt ? sample.tiltMagnitude : 0.0,
             sample.capabilities.hasRotation ? sample.rotationRadians : 0.0}})
        : m_stroke->end(endSample);
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
        m_hasLastStrokeSample = false;
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
    m_hasLastStrokeSample = false;
    refreshMaterialSimulation();
}

void CanvasViewport::recoverOpenStroke(bool commit)
{
    if (!m_stroke && !m_materialStroke) return;
    if (commit && m_hasLastStrokeSample) {
        input::PointerSample endS = m_lastStrokeSample;
        endS.phase = input::PointerPhase::Up;
        endStrokeSample(endS);
    } else {
        cancelStroke();
    }
}

bool CanvasViewport::beginStroke(qreal x, qreal y, qreal pressure, QObject* settings, const QColor& color)
{
    m_arbiter.markStrokeEndedExternally();
    auto* brush = qobject_cast<BrushSettings*>(settings);
    const QPointF docPt = viewportToDocument(x, y);
    input::PointerSample s;
    s.viewX = x;
    s.viewY = y;
    s.docX = docPt.x();
    s.docY = docPt.y();
    s.pressure = std::clamp(double(pressure), 0.0, 1.0);
    s.rawPressure = s.pressure;
    s.tipContact = true;
    return beginStrokeSample(s, brush, color, false);
}

void CanvasViewport::continueStroke(qreal x, qreal y, qreal pressure)
{
    const QPointF docPt = viewportToDocument(x, y);
    input::PointerSample s;
    s.viewX = x;
    s.viewY = y;
    s.docX = docPt.x();
    s.docY = docPt.y();
    s.pressure = std::clamp(double(pressure), 0.0, 1.0);
    s.rawPressure = s.pressure;
    s.tipContact = true;
    continueStrokeSample(s);
}

void CanvasViewport::endStroke(qreal x, qreal y, qreal pressure)
{
    const QPointF docPt = viewportToDocument(x, y);
    input::PointerSample s;
    s.viewX = x;
    s.viewY = y;
    s.docX = docPt.x();
    s.docY = docPt.y();
    s.pressure = std::clamp(double(pressure), 0.0, 1.0);
    s.rawPressure = s.pressure;
    s.tipContact = false;
    endStrokeSample(s);
}

void CanvasViewport::clearStrokeObjects()
{
    if (m_history && m_history->strokeActive()) m_history->cancelStroke();
    m_stroke.reset();
    m_materialStroke.reset();
    m_strokeLayer = nullptr;
    m_hasLastStrokeSample = false;
}

void CanvasViewport::cancelStroke()
{
    clearStrokeObjects();
    m_arbiter.markStrokeEndedExternally();
}

void CanvasViewport::processArbiterResult(const input::ProcessResult& res)
{
    if (res.decision != input::ArbiterDecision::Accept) return;

    const input::PointerSample& sample = res.sample;
    if (sample.phase == input::PointerPhase::Leave) {
        clearCursorState();
    } else {
        updateCursorState(sample, true);
    }

    if (res.shouldCancelPreviousStroke) {
        m_barrelPanActive = false;
        m_barrelEyedropperActive = false;
        cancelStroke();
        return;
    }

    if (res.shouldEndPreviousStroke) {
        recoverOpenStroke(true);
    }

    // Handle triggered barrel actions
    if (res.triggeredBarrelAction != input::BarrelAction::None) {
        switch (res.triggeredBarrelAction) {
        case input::BarrelAction::MiddlePan:
            m_barrelPanActive = true;
            m_lastPanPos = QPointF(sample.viewX, sample.viewY);
            break;
        case input::BarrelAction::Eyedropper: {
            m_barrelEyedropperActive = true;
            const QVariant picked = colorAt(sample.viewX, sample.viewY);
            if (picked.isValid()) emit eyedropperPicked(picked.value<QColor>());
            break;
        }
        case input::BarrelAction::Undo:
            if (!m_stroke && !m_materialStroke && m_history && m_history->canUndo()) {
                m_history->undo();
            }
            emit barrelActionRequested(QStringLiteral("undo"));
            break;
        case input::BarrelAction::RightClick:
            emit barrelActionRequested(QStringLiteral("right_click"));
            break;
        case input::BarrelAction::EraserToggle:
        case input::BarrelAction::None:
            break;
        }
    }

    if (m_barrelPanActive) {
        const QPointF nowPos(sample.viewX, sample.viewY);
        const QPointF delta = nowPos - m_lastPanPos;
        m_lastPanPos = nowPos;
        if (sample.phase == input::PointerPhase::Move && !delta.isNull()) {
            panBy(delta.x(), delta.y());
        }
        if ((sample.buttons & (input::SecondaryButton | input::MiddleButton)) == 0 ||
            sample.phase == input::PointerPhase::Up || sample.phase == input::PointerPhase::Leave) {
            m_barrelPanActive = false;
        }
        if (!res.shouldBeginStroke && !res.shouldExtendStroke && !res.shouldEndStroke) return;
    }

    if (m_barrelEyedropperActive) {
        if (sample.phase == input::PointerPhase::Move) {
            const QVariant picked = colorAt(sample.viewX, sample.viewY);
            if (picked.isValid()) emit eyedropperPicked(picked.value<QColor>());
        }
        if ((sample.buttons & (input::SecondaryButton | input::MiddleButton)) == 0 ||
            sample.phase == input::PointerPhase::Up || sample.phase == input::PointerPhase::Leave) {
            m_barrelEyedropperActive = false;
        }
        if (!res.shouldBeginStroke && !res.shouldExtendStroke && !res.shouldEndStroke) return;
    }

    if (res.shouldBeginStroke) {
        if (m_document && m_brushSettings && activeToolIsPaintable()) {
            if (!beginStrokeSample(sample, m_brushSettings.data(), m_brushColor, sample.eraserActive)) {
                m_arbiter.markStrokeEndedExternally();
            }
        } else {
            m_arbiter.markStrokeEndedExternally();
        }
    } else if (res.shouldExtendStroke) {
        if (m_stroke || m_materialStroke) {
            continueStrokeSample(sample);
        }
    } else if (res.shouldEndStroke) {
        if (m_stroke || m_materialStroke) {
            endStrokeSample(sample);
        }
    }
}

void CanvasViewport::injectRawPointerEvent(const input::RawPointerEvent& raw)
{
    const QPointF docPt = viewportToDocument(raw.viewX, raw.viewY);
    const auto res = m_arbiter.process(raw, docPt.x(), docPt.y());
    processArbiterResult(res);
}

void CanvasViewport::handleTabletEvent(QTabletEvent* e)
{
    if (!e) return;
    if (!activeToolIsPaintable() &&
        e->type() != QEvent::TabletEnterProximity &&
        e->type() != QEvent::TabletLeaveProximity) {
        e->ignore();
        return;
    }
    const auto raw = input::QtPointerAdapter::fromTabletEvent(e);
    if (e->type() == QEvent::TabletEnterProximity || e->type() == QEvent::TabletLeaveProximity) {
        m_arbiter.setProximity(raw.deviceId, e->type() == QEvent::TabletEnterProximity,
                               raw.backend, raw.timestampNs);
    }
    const QPointF docPt = viewportToDocument(raw.viewX, raw.viewY);
    const auto res = m_arbiter.process(raw, docPt.x(), docPt.y());
    if (res.decision == input::ArbiterDecision::Accept) {
        e->accept();
        processArbiterResult(res);
    } else {
        e->accept(); // still consume duplicate protocol so Qt won't synthesize mouse
    }
}

bool CanvasViewport::event(QEvent* e)
{
    if (!e) return false;
    switch (e->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
    case QEvent::TabletEnterProximity:
    case QEvent::TabletLeaveProximity:
        handleTabletEvent(static_cast<QTabletEvent*>(e));
        return e->isAccepted();
    default:
        break;
    }
    return QQuickPaintedItem::event(e);
}

void CanvasViewport::mousePressEvent(QMouseEvent* e)
{
    if (!e) return;
    if (e->button() == Qt::RightButton && activeToolIsPaintable()) {
        const auto raw = input::QtPointerAdapter::fromMouseEvent(e, input::PointerPhase::Hover);
        if (!raw.synthesizedMouse && !m_arbiter.stylusRecentlyActive(raw.timestampNs)) {
            emit barrelActionRequested(QStringLiteral("right_click"));
        }
        e->accept();
        return;
    }
    if (e->button() != Qt::LeftButton || !activeToolIsPaintable()) {
        e->ignore();
        return;
    }
    const auto raw = input::QtPointerAdapter::fromMouseEvent(e, input::PointerPhase::Down);
    const QPointF docPt = viewportToDocument(raw.viewX, raw.viewY);
    const auto res = m_arbiter.process(raw, docPt.x(), docPt.y());
    e->accept();
    processArbiterResult(res);
}

void CanvasViewport::mouseMoveEvent(QMouseEvent* e)
{
    if (!e) return;
    if (!activeToolIsPaintable() && !m_stroke && !m_materialStroke) {
        e->ignore();
        return;
    }
    const auto raw = input::QtPointerAdapter::fromMouseEvent(e, input::PointerPhase::Move);
    const QPointF docPt = viewportToDocument(raw.viewX, raw.viewY);
    const auto res = m_arbiter.process(raw, docPt.x(), docPt.y());
    e->accept();
    processArbiterResult(res);
}

void CanvasViewport::mouseReleaseEvent(QMouseEvent* e)
{
    if (!e) return;
    if (e->button() != Qt::LeftButton && !m_stroke && !m_materialStroke) {
        e->ignore();
        return;
    }
    const auto raw = input::QtPointerAdapter::fromMouseEvent(e, input::PointerPhase::Up);
    const QPointF docPt = viewportToDocument(raw.viewX, raw.viewY);
    const auto res = m_arbiter.process(raw, docPt.x(), docPt.y());
    e->accept();
    processArbiterResult(res);
}

void CanvasViewport::mouseUngrabEvent()
{
    if (m_arbiter.handleFocusLost()) {
        recoverOpenStroke(true);
    }
}

void CanvasViewport::hoverEnterEvent(QHoverEvent* e)
{
    if (!e) return;
    const auto raw = input::QtPointerAdapter::fromHoverEvent(e, input::PointerPhase::Enter);
    const QPointF docPt = viewportToDocument(raw.viewX, raw.viewY);
    const auto res = m_arbiter.process(raw, docPt.x(), docPt.y());
    if (res.decision == input::ArbiterDecision::Accept) updateCursorState(res.sample, true);
    e->accept();
}

void CanvasViewport::hoverMoveEvent(QHoverEvent* e)
{
    if (!e) return;
    const auto raw = input::QtPointerAdapter::fromHoverEvent(e, input::PointerPhase::Hover);
    const QPointF docPt = viewportToDocument(raw.viewX, raw.viewY);
    const auto res = m_arbiter.process(raw, docPt.x(), docPt.y());
    if (res.decision == input::ArbiterDecision::Accept) updateCursorState(res.sample, true);
    e->accept();
}

void CanvasViewport::hoverLeaveEvent(QHoverEvent* e)
{
    if (!e) return;
    const auto raw = input::QtPointerAdapter::fromHoverEvent(e, input::PointerPhase::Leave);
    const QPointF docPt = viewportToDocument(raw.viewX, raw.viewY);
    m_arbiter.process(raw, docPt.x(), docPt.y());
    clearCursorState();
    e->accept();
}

void CanvasViewport::handleTouchNavigation(const QTouchEvent* e)
{
    if (!e) return;
    const auto& pts = e->points();
    std::array<TouchAnchor, 8> nextAnchors{};
    int activeCount = 0;
    for (const QEventPoint& pt : pts) {
        if (pt.state() == QEventPoint::State::Released) continue;
        if (activeCount < 8) {
            nextAnchors[static_cast<std::size_t>(activeCount++)] = {true, pt.id(), pt.position()};
        }
    }

    if (activeCount >= 2) {
        // Match first two active anchors with previous frame for pan + pinch + rotate.
        const TouchAnchor& aNow = nextAnchors[0];
        const TouchAnchor& bNow = nextAnchors[1];
        const TouchAnchor* aPrev = nullptr;
        const TouchAnchor* bPrev = nullptr;
        for (const auto& prev : m_touchAnchors) {
            if (!prev.active) continue;
            if (prev.id == aNow.id) aPrev = &prev;
            else if (prev.id == bNow.id) bPrev = &prev;
        }
        if (aPrev && bPrev) {
            const QPointF prevCenter = (aPrev->pos + bPrev->pos) * 0.5;
            const QPointF nowCenter = (aNow.pos + bNow.pos) * 0.5;
            const QPointF panDelta = nowCenter - prevCenter;
            if (!panDelta.isNull()) panBy(panDelta.x(), panDelta.y());

            const QPointF prevVec = bPrev->pos - aPrev->pos;
            const QPointF nowVec = bNow.pos - aNow.pos;
            const qreal prevDist = std::hypot(prevVec.x(), prevVec.y());
            const qreal nowDist = std::hypot(nowVec.x(), nowVec.y());
            const bool allowPinch = !m_inputSettings || m_inputSettings->touchPinchZoom();
            if (allowPinch && prevDist > 4.0 && nowDist > 4.0) {
                const qreal ratio = nowDist / prevDist;
                if (std::abs(ratio - 1.0) > 0.001) {
                    zoomAt(nowCenter.x(), nowCenter.y(), ratio);
                }
            }
            const bool allowRot = !m_inputSettings || m_inputSettings->touchRotate();
            if (allowRot && prevDist > 8.0 && nowDist > 8.0) {
                const qreal a0 = std::atan2(prevVec.y(), prevVec.x());
                const qreal a1 = std::atan2(nowVec.y(), nowVec.x());
                qreal dRad = a1 - a0;
                while (dRad > kPi) dRad -= 2.0 * kPi;
                while (dRad < -kPi) dRad += 2.0 * kPi;
                const qreal dDeg = dRad * (180.0 / kPi);
                if (std::abs(dDeg) > 0.05) {
                    setViewRotation(m_viewRotation + dDeg);
                }
            }
        }
        m_touchNavActive = true;
    } else if (activeCount == 1 &&
               m_arbiter.config().touchMode == input::TouchCanvasMode::StylusOnlyWithFingerNavigation) {
        const TouchAnchor& now = nextAnchors[0];
        for (const auto& prev : m_touchAnchors) {
            if (prev.active && prev.id == now.id) {
                const QPointF delta = now.pos - prev.pos;
                if (!delta.isNull()) panBy(delta.x(), delta.y());
                break;
            }
        }
        m_touchNavActive = true;
    } else {
        m_touchNavActive = false;
    }

    m_touchAnchors = nextAnchors;
}

void CanvasViewport::touchEvent(QTouchEvent* e)
{
    if (!e) return;
    input::QtPointerAdapter::fromTouchEvent(e, m_touchScratch);
    if (m_touchScratch.empty()) {
        e->ignore();
        return;
    }

    // Check whether an active stylus is riding through QTouchEvent (e.g. Android active stylus).
    bool hasStylusPoint = false;
    for (const auto& r : m_touchScratch) {
        if (r.pointerType == input::PointerType::Pen || r.pointerType == input::PointerType::Eraser) {
            hasStylusPoint = true;
            break;
        }
    }

    if (hasStylusPoint) {
        for (const auto& r : m_touchScratch) {
            if (r.pointerType == input::PointerType::Pen || r.pointerType == input::PointerType::Eraser) {
                injectRawPointerEvent(r);
            }
        }
        for (qsizetype i = 0; i < e->pointCount(); ++i) {
            e->point(i).setAccepted(true);
        }
        e->accept();
        return;
    }

    if (m_arbiter.config().touchMode == input::TouchCanvasMode::StylusOnlyDisableTouch) {
        e->ignore();
        return;
    }

    if (m_arbiter.config().palmRejection && m_arbiter.stylusRecentlyActive(m_touchScratch.front().timestampNs)) {
        m_arbiter.process(m_touchScratch.front(), 0.0, 0.0); // records palm rejection telemetry
        e->ignore();
        return;
    }

    if (m_touchScratch.size() >= 2 ||
        m_arbiter.config().touchMode == input::TouchCanvasMode::StylusOnlyWithFingerNavigation) {
        if (m_stroke || m_materialStroke) {
            recoverOpenStroke(false);
        }
        handleTouchNavigation(e);
        for (qsizetype i = 0; i < e->pointCount(); ++i) {
            e->point(i).setAccepted(true);
        }
        e->accept();
        return;
    }

    // Single-finger painting when AllowAll mode is enabled.
    const auto& raw = m_touchScratch.front();
    const QPointF docPt = viewportToDocument(raw.viewX, raw.viewY);
    const auto res = m_arbiter.process(raw, docPt.x(), docPt.y());
    processArbiterResult(res);
    for (qsizetype i = 0; i < e->pointCount(); ++i) {
        e->point(i).setAccepted(true);
    }
    e->accept();
}

void CanvasViewport::touchUngrabEvent()
{
    m_touchNavActive = false;
    m_touchAnchors = {};
    if (m_arbiter.handleFocusLost()) {
        recoverOpenStroke(true);
    }
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
    const auto background = m_document->hasTransparentBackground() ? 0U : m_document->backgroundColor().rgba();
    std::vector<std::vector<std::uint32_t>> scratchBuffers;
    std::vector<std::span<const std::uint32_t>> overrides;
    if (!prepareMaterialRenderLayers(list, address->tile, m_document->surfaceProfile(), scratchBuffers, overrides))
        return {};
    std::vector<std::uint32_t> pixels;
    std::int64_t tileWidth = 0;
    std::int64_t tileHeight = 0;
    if (!CpuTileCompositor::composeInto(geometry, address->tile, list, background, overrides,
                                        pixels, tileWidth, tileHeight))
        return {};
    const auto index = static_cast<std::size_t>(address->local.y) * static_cast<std::size_t>(tileWidth)
                     + static_cast<std::size_t>(address->local.x);
    if (index >= pixels.size()) return {};
    const std::uint32_t pixel = pixels[index];   // premultiplied ARGB
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
    m_scheduled = false;
    QRectF dirty = m_pending;
    m_pending = {};
    const QSize frameSize(qCeil(width()), qCeil(height()));
    if (frameSize.width() <= 0 || frameSize.height() <= 0) return;
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
            QMetaObject::invokeMethod(this, &CanvasViewport::viewChanged, Qt::QueuedConnection);
        }
        QPainter painter(&m_frame);
        painter.setClipRect(dirty);
        painter.fillRect(dirty, m_deskColour);
        if (m_document) {
            auto list = layers();
            const auto background = m_document->hasTransparentBackground() ? 0U : m_document->backgroundColor().rgba();
            auto plan = m_invalidation.prepare(list, background);
            if (plan.invalidateAll) clearCache();
            if (!m_invalidation.accept(plan, list, background)) return;
            for (const auto& tile : plan.tiles) {
                eraseCacheTile(tile);
            }
            const qreal scale = m_zoom / 100;
            painter.save();
            if (m_viewRotation != 0.0) {
                const QPointF center(width() / 2, height() / 2);
                painter.translate(center);
                painter.rotate(m_viewRotation);
                painter.translate(-center);
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            }
            painter.translate(m_origin);
            painter.scale(scale, scale);
            const QRectF documentRect(0, 0, m_document->width(), m_document->height());
            painter.setClipRect(documentRect, Qt::IntersectClip);
            if (m_checkerBrush.style() == Qt::NoBrush
                || m_cachedCheckerLight != m_checkerLight
                || m_cachedCheckerDark != m_checkerDark) {
                QImage checks(16, 16, QImage::Format_RGB32);
                checks.fill(m_checkerLight);
                { QPainter cp(&checks); cp.fillRect(0, 0, 8, 8, m_checkerDark); cp.fillRect(8, 8, 8, 8, m_checkerDark); }
                m_checkerBrush = QBrush(checks);
                m_cachedCheckerLight = m_checkerLight;
                m_cachedCheckerDark = m_checkerDark;
            }
            painter.fillRect(documentRect, m_checkerBrush);
            if (!list.empty()) {
                const auto& geometry = list.front().store->geometry();
                const QPointF c0 = viewportToDocument(dirty.left(), dirty.top());
                const QPointF c1 = viewportToDocument(dirty.right(), dirty.top());
                const QPointF c2 = viewportToDocument(dirty.left(), dirty.bottom());
                const QPointF c3 = viewportToDocument(dirty.right(), dirty.bottom());
                const qreal minDocX = std::min({c0.x(), c1.x(), c2.x(), c3.x()});
                const qreal maxDocX = std::max({c0.x(), c1.x(), c2.x(), c3.x()});
                const qreal minDocY = std::min({c0.y(), c1.y(), c2.y(), c3.y()});
                const qreal maxDocY = std::max({c0.y(), c1.y(), c2.y(), c3.y()});
                TileRect requested{
                    static_cast<std::int64_t>(std::floor(qBound<qreal>(0, minDocX, m_document->width()))),
                    static_cast<std::int64_t>(std::floor(qBound<qreal>(0, minDocY, m_document->height()))),
                    static_cast<std::int64_t>(std::ceil(qBound<qreal>(0, maxDocX, m_document->width()))),
                    static_cast<std::int64_t>(std::ceil(qBound<qreal>(0, maxDocY, m_document->height())))
                };
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
}
} // namespace komapaint
