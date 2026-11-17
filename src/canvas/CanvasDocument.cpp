#include "CanvasDocument.h"

#include <algorithm>
#include <cmath>

namespace komapaint {

CanvasDocument::CanvasDocument(QObject* parent) : QObject(parent), m_layers(new LayerModel(this))
{
    connect(m_layers, &QAbstractItemModel::rowsInserted, this, [this] { connectArtworkSignals(); });
    connectArtworkSignals();
    auto markModified = [this] { setModified(true); };
    connect(this, &CanvasDocument::titleChanged, this, markModified);
    connect(this, &CanvasDocument::sizeChanged, this, markModified);
    connect(this, &CanvasDocument::dpiChanged, this, markModified);
    connect(this, &CanvasDocument::backgroundChanged, this, markModified);
    connect(this, &CanvasDocument::surfaceChanged, this, markModified);
    connect(m_layers, &QAbstractItemModel::rowsInserted, this, markModified);
    connect(m_layers, &QAbstractItemModel::rowsRemoved, this, markModified);
    connect(m_layers, &QAbstractItemModel::rowsMoved, this, markModified);
    connect(m_layers, &QAbstractItemModel::dataChanged, this, markModified);
}
void CanvasDocument::connectArtworkSignals()
{
    for (int i = 0; i < m_layers->rowCount(); ++i) {
        auto* layer = m_layers->layerAt(i);
        disconnect(layer, &Layer::artworkChanged, this, nullptr);
        connect(layer, &Layer::artworkChanged, this, [this] {
            if (!m_modified) { m_modified = true; emit modifiedChanged(); }
        });
    }
}

bool CanvasDocument::validSize(const QSize& size)
{
    return size.width() >= 1 && size.height() >= 1
        && size.width() <= MaxDimension && size.height() <= MaxDimension;
}

void CanvasDocument::setBackgroundColor(const QColor& color)
{
    if (!color.isValid() || color.alpha() != 255 || color == m_backgroundColor) return;
    m_backgroundColor = color;
    emit backgroundChanged();
}

CanvasDocument* CanvasDocument::createDefault(QObject* parent)
{
    auto* doc = new CanvasDocument(parent);
    doc->setTitle(QStringLiteral("Untitled"));
    return doc;
}

void CanvasDocument::setTitle(const QString& title)
{
    if (m_title == title) return;
    m_title = title;
    emit titleChanged();
}

void CanvasDocument::setWidth(int w)
{
    if (!validSize(QSize(w, height())) || w == m_size.width()) return;
    if (!m_layers->resizeEmptyLayers(QSize(w, height()))) return;
    m_size.setWidth(w);
    m_selection.reset(w, m_size.height());
    m_selectionUndo.clear();
    m_selectionRedo.clear();
    emit sizeChanged();
    emit selectionChanged();
}

void CanvasDocument::setHeight(int h)
{
    if (!validSize(QSize(width(), h)) || h == m_size.height()) return;
    if (!m_layers->resizeEmptyLayers(QSize(width(), h))) return;
    m_size.setHeight(h);
    m_selection.reset(m_size.width(), h);
    m_selectionUndo.clear();
    m_selectionRedo.clear();
    emit sizeChanged();
    emit selectionChanged();
}

void CanvasDocument::setSize(const QSize& size)
{
    if (!validSize(size) || size == m_size) return;
    if (!m_layers->resizeEmptyLayers(size)) return;
    m_size = size;
    m_selection.reset(size.width(), size.height());
    m_selectionUndo.clear();
    m_selectionRedo.clear();
    emit sizeChanged();
    emit selectionChanged();
}

void CanvasDocument::setDpi(qreal dpi)
{
    if (dpi <= 0 || qFuzzyCompare(dpi, m_dpi)) return;
    m_dpi = dpi;
    emit dpiChanged();
}

void CanvasDocument::setTransparentBackground(bool t)
{
    if (t == m_transparent) return;
    m_transparent = t;
    emit backgroundChanged();
}

namespace {
int toPercent(double value)
{
    return std::clamp(static_cast<int>(std::lround(value * 100.0)), 0, 100);
}
double fromPercent(int value)
{
    return std::clamp(double(value) / 100.0, 0.0, 1.0);
}
} // namespace

void CanvasDocument::setSurfaceProfile(const material::SurfaceProfile& profile)
{
    if (profile.id.empty()) return;
    m_surface = profile;
    if (m_surface.name.empty()) {
        if (const auto* found = material::findSurfaceProfile(m_surface.id))
            m_surface.name = found->name;
        else
            m_surface.name = m_surface.id;
    }
    emit surfaceChanged();
}

bool CanvasDocument::hasCustomSurfaceSettings() const noexcept
{
    const auto& def = material::coldPressedPaper();
    return m_surface.id != def.id
        || std::abs(m_surface.absorbency - def.absorbency) > 1e-9
        || std::abs(m_surface.absorptionRate - def.absorptionRate) > 1e-9
        || std::abs(m_surface.porosity - def.porosity) > 1e-9
        || std::abs(m_surface.waterRetention - def.waterRetention) > 1e-9
        || std::abs(m_surface.tooth - def.tooth) > 1e-9
        || std::abs(m_surface.roughness - def.roughness) > 1e-9
        || std::abs(m_surface.pigmentRetention - def.pigmentRetention) > 1e-9
        || std::abs(m_surface.surfaceWetness - def.surfaceWetness) > 1e-9
        || std::abs(m_surface.granulationInteraction - def.granulationInteraction) > 1e-9
        || std::abs(m_surface.rewetting - def.rewetting) > 1e-9;
}

int CanvasDocument::surfaceAbsorbency() const { return toPercent(m_surface.absorbency); }
int CanvasDocument::surfaceAbsorptionRate() const { return toPercent(m_surface.absorptionRate); }
int CanvasDocument::surfacePorosity() const { return toPercent(m_surface.porosity); }
int CanvasDocument::surfaceWaterRetention() const { return toPercent(m_surface.waterRetention); }
int CanvasDocument::surfaceTooth() const { return toPercent(m_surface.tooth); }
int CanvasDocument::surfaceRoughness() const { return toPercent(m_surface.roughness); }
int CanvasDocument::surfacePigmentRetention() const { return toPercent(m_surface.pigmentRetention); }
int CanvasDocument::surfaceWetness() const { return toPercent(m_surface.surfaceWetness); }
int CanvasDocument::surfaceGranulation() const { return toPercent(m_surface.granulationInteraction); }
int CanvasDocument::surfaceRewetting() const { return toPercent(m_surface.rewetting); }

void CanvasDocument::setSurfaceId(const QString& id)
{
    const QByteArray bytes = id.trimmed().toUtf8();
    const auto* found = material::findSurfaceProfile(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!found) return;
    if (m_surface.id == found->id && !hasCustomSurfaceSettings()) return;
    m_surface = *found;
    emit surfaceChanged();
}

void CanvasDocument::resetSurfaceSettings()
{
    const auto* found = material::findSurfaceProfile(m_surface.id);
    m_surface = found ? *found : material::coldPressedPaper();
    emit surfaceChanged();
}

void CanvasDocument::setSurfaceAbsorbency(int value)
{
    const double next = fromPercent(value);
    if (std::abs(m_surface.absorbency - next) <= 1e-9) return;
    m_surface.absorbency = next;
    emit surfaceChanged();
}

void CanvasDocument::setSurfaceAbsorptionRate(int value)
{
    const double next = fromPercent(value);
    if (std::abs(m_surface.absorptionRate - next) <= 1e-9) return;
    m_surface.absorptionRate = next;
    emit surfaceChanged();
}

void CanvasDocument::setSurfacePorosity(int value)
{
    const double next = fromPercent(value);
    if (std::abs(m_surface.porosity - next) <= 1e-9) return;
    m_surface.porosity = next;
    emit surfaceChanged();
}

void CanvasDocument::setSurfaceWaterRetention(int value)
{
    const double next = fromPercent(value);
    if (std::abs(m_surface.waterRetention - next) <= 1e-9) return;
    m_surface.waterRetention = next;
    emit surfaceChanged();
}

void CanvasDocument::setSurfaceTooth(int value)
{
    const double next = fromPercent(value);
    if (std::abs(m_surface.tooth - next) <= 1e-9) return;
    m_surface.tooth = next;
    emit surfaceChanged();
}

void CanvasDocument::setSurfaceRoughness(int value)
{
    const double next = fromPercent(value);
    if (std::abs(m_surface.roughness - next) <= 1e-9) return;
    m_surface.roughness = next;
    emit surfaceChanged();
}

void CanvasDocument::setSurfacePigmentRetention(int value)
{
    const double next = fromPercent(value);
    if (std::abs(m_surface.pigmentRetention - next) <= 1e-9) return;
    m_surface.pigmentRetention = next;
    emit surfaceChanged();
}

void CanvasDocument::setSurfaceWetness(int value)
{
    const double next = fromPercent(value);
    if (std::abs(m_surface.surfaceWetness - next) <= 1e-9) return;
    m_surface.surfaceWetness = next;
    emit surfaceChanged();
}

void CanvasDocument::setSurfaceGranulation(int value)
{
    const double next = fromPercent(value);
    if (std::abs(m_surface.granulationInteraction - next) <= 1e-9) return;
    m_surface.granulationInteraction = next;
    emit surfaceChanged();
}

void CanvasDocument::setSurfaceRewetting(int value)
{
    const double next = fromPercent(value);
    if (std::abs(m_surface.rewetting - next) <= 1e-9) return;
    m_surface.rewetting = next;
    emit surfaceChanged();
}

QRectF CanvasDocument::selectionBounds() const
{
    const auto b = m_selection.bounds();
    if (b.empty()) return {};
    return QRectF(static_cast<qreal>(b.x), static_cast<qreal>(b.y),
                  static_cast<qreal>(b.width), static_cast<qreal>(b.height));
}

void CanvasDocument::pushSelectionSnapshot()
{
    m_selectionUndo.push_back(m_selection.snapshot());
    if (m_selectionUndo.size() > 64) {
        m_selectionUndo.erase(m_selectionUndo.begin());
    }
    m_selectionRedo.clear();
}

bool CanvasDocument::undoSelection()
{
    if (m_selectionUndo.empty()) return false;
    m_selectionRedo.push_back(m_selection.snapshot());
    m_selection.restore(m_selectionUndo.back());
    m_selectionUndo.pop_back();
    emit selectionChanged();
    return true;
}

bool CanvasDocument::redoSelection()
{
    if (m_selectionRedo.empty()) return false;
    m_selectionUndo.push_back(m_selection.snapshot());
    m_selection.restore(m_selectionRedo.back());
    m_selectionRedo.pop_back();
    emit selectionChanged();
    return true;
}

void CanvasDocument::selectAll()
{
    pushSelectionSnapshot();
    m_selection.selectAll();
    emit selectionChanged();
}

void CanvasDocument::deselect()
{
    if (m_selection.empty()) return;
    pushSelectionSnapshot();
    m_selection.clear();
    emit selectionChanged();
}

void CanvasDocument::invertSelection()
{
    pushSelectionSnapshot();
    m_selection.invert();
    emit selectionChanged();
}

void CanvasDocument::expandSelection(int radiusPx)
{
    if (m_selection.empty() || radiusPx <= 0) return;
    pushSelectionSnapshot();
    m_selection.expand(radiusPx);
    emit selectionChanged();
}

void CanvasDocument::contractSelection(int radiusPx)
{
    if (m_selection.empty() || radiusPx <= 0) return;
    pushSelectionSnapshot();
    m_selection.contract(radiusPx);
    emit selectionChanged();
}

void CanvasDocument::featherSelection(int radiusPx)
{
    if (m_selection.empty() || radiusPx <= 0) return;
    pushSelectionSnapshot();
    m_selection.feather(radiusPx);
    emit selectionChanged();
}

}  // namespace komapaint
