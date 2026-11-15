#include "LayerModel.h"

namespace komapaint {

LayerModel::LayerModel(QObject* parent) : QAbstractListModel(parent)
{
    auto* l = new Layer(QStringLiteral("Layer 1"), this);
    l->tiles().setDocumentBudget(m_documentBudget);
    l->setIndex(0);
    m_layers.append(l);
    connect(l, &Layer::nameChanged,       this, [this,l]{ onLayerChanged(l, {NameRole}); });
    connect(l, &Layer::visibilityChanged, this, [this,l]{ onLayerChanged(l, {VisibleRole}); });
    connect(l, &Layer::lockedChanged,     this, [this,l]{ onLayerChanged(l, {LockedRole}); });
    connect(l, &Layer::opacityChanged,    this, [this,l]{ onLayerChanged(l, {OpacityRole}); });
    connect(l, &Layer::blendModeChanged,  this, [this,l]{ onLayerChanged(l, {BlendModeRole}); });
    connect(l, &Layer::colorLabelChanged, this, [this,l]{ onLayerChanged(l, {ColorLabelRole}); });
}

bool LayerModel::resizeEmptyLayers(const QSize& size)
{
    if (size.width() <= 0 || size.height() <= 0) return false;
    for (const auto* layer : m_layers)
        if (layer->tiles().allocatedTiles() != 0 || !layer->tiles().dirtyRegions().empty()) return false;
    for (auto* layer : m_layers)
        if (!layer->tiles().resizeEmpty(size.width(), size.height())) return false;
    m_canvasSize = size;
    return true;
}

void LayerModel::onLayerChanged(Layer* layer, const QList<int>& roles)
{
    const int row = m_layers.indexOf(layer);
    if (row < 0) return;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, roles);
}

int LayerModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_layers.size(); }

QVariant LayerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 || index.row() >= m_layers.size() || index.column() != 0) return {};
    const Layer* l = m_layers[index.row()];
    switch (role) {
        case NameRole:    return l->name();
        case VisibleRole: return l->isVisible();
        case LockedRole:  return l->isLocked();
        case OpacityRole: return l->opacity();
        case IndexRole:   return l->index();
        case BlendModeRole: return l->blendMode();
        case ColorLabelRole: return l->colorLabel();
    }
    return {};
}

QHash<int, QByteArray> LayerModel::roleNames() const
{
    return {
        { NameRole,    "name"    },
        { VisibleRole, "visible" },
        { LockedRole,  "locked"  },
        { OpacityRole, "opacity" },
        { IndexRole,   "index"   },
        { BlendModeRole, "blendMode" },
        { ColorLabelRole, "colorLabel" },
    };
}

Layer* LayerModel::layerAt(int row) const
{
    if (row < 0 || row >= m_layers.size()) return nullptr;
    return m_layers[row];
}

void LayerModel::setActiveLayerIndex(int row)
{
    if (row < 0 || row >= m_layers.size() || row == m_active) return;
    m_active = row;
    emit activeLayerIndexChanged(m_active);
}

void LayerModel::addLayer()
{
    const int row = m_layers.size();
    beginInsertRows({}, row, row);
    auto* l = new Layer(QString("Layer %1").arg(row + 1), this);
    l->tiles().setDocumentBudget(m_documentBudget);
    l->tiles().resizeEmpty(m_canvasSize.width(), m_canvasSize.height());
    l->setIndex(row);
    connect(l, &Layer::nameChanged,       this, [this,l]{ onLayerChanged(l, {NameRole}); });
    connect(l, &Layer::visibilityChanged, this, [this,l]{ onLayerChanged(l, {VisibleRole}); });
    connect(l, &Layer::lockedChanged,     this, [this,l]{ onLayerChanged(l, {LockedRole}); });
    connect(l, &Layer::opacityChanged,    this, [this,l]{ onLayerChanged(l, {OpacityRole}); });
    connect(l, &Layer::blendModeChanged,  this, [this,l]{ onLayerChanged(l, {BlendModeRole}); });
    connect(l, &Layer::colorLabelChanged, this, [this,l]{ onLayerChanged(l, {ColorLabelRole}); });
    m_layers.append(l);
    endInsertRows();
}

void LayerModel::removeLayer(int row)
{
    if (row < 0 || row >= m_layers.size() || m_layers.size() <= 1) return;
    // A synchronous artwork callback runs inside the store or a brush dab.
    // Deleting any layer there can invalidate an active operation's owner.
    for (const auto* layer : m_layers)
        if (layer->tiles().notifyingChange()) return;
    const int previousActive = m_active;
    beginRemoveRows({}, row, row);
    delete m_layers.takeAt(row);
    if (row < m_active) --m_active;
    else if (m_active >= m_layers.size()) m_active = m_layers.size() - 1;
    for (int i = 0; i < m_layers.size(); ++i) m_layers[i]->setIndex(i);
    endRemoveRows();
    // Removing the active row may leave the numeric index unchanged while
    // replacing the selected object. Consumers need to re-read layerAt().
    if (m_active != previousActive || row == previousActive) emit activeLayerIndexChanged(m_active);
}

void LayerModel::moveLayer(int from, int to)
{
    if (from == to) return;
    if (from < 0 || from >= m_layers.size()) return;
    if (to   < 0 || to   >= m_layers.size()) return;
    const int previousActive = m_active;
    if (!beginMoveRows({}, from, from, {}, to + (to > from ? 1 : 0))) return;
    m_layers.move(from, to);
    if (m_active == from) m_active = to;
    else if (from < m_active && to >= m_active) --m_active;
    else if (from > m_active && to <= m_active) ++m_active;
    for (int i = 0; i < m_layers.size(); ++i) m_layers[i]->setIndex(i);
    endMoveRows();
    if (m_active != previousActive) emit activeLayerIndexChanged(m_active);
}

}  // namespace komapaint
