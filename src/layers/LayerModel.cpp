#include "LayerModel.h"

#include <algorithm>

namespace komapaint {

LayerModel::LayerModel(QObject* parent, bool createDefaultLayer) : QAbstractListModel(parent)
{
    if (!createDefaultLayer) return;
    auto* l = new Layer(QStringLiteral("Layer 1"), this);
    l->tiles().setDocumentBudget(m_documentBudget);
    l->materialTiles().setDocumentBudget(m_documentBudget);
    l->setIndex(0);
    m_layers.append(l);
    m_active = 0;
    m_selectedRows = {0};
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
        if (layer->tiles().allocatedTiles() != 0 || !layer->tiles().dirtyRegions().empty()
            || layer->materialTiles().allocatedTiles() != 0 || !layer->materialTiles().dirtyRegions().empty()) return false;
    for (auto* layer : m_layers)
        if (!layer->tiles().resizeEmpty(size.width(), size.height())
            || !layer->materialTiles().resizeEmpty(size.width(), size.height())) return false;
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
    m_selectedRows = {row};
    emit activeLayerIndexChanged(m_active);
    emit layerSelectionChanged();
}

QVariantList LayerModel::selectedLayerRows() const
{
    QVariantList rows;
    rows.reserve(m_selectedRows.size());
    for (int row : m_selectedRows) rows.append(row);
    return rows;
}

void LayerModel::setSelectedLayerRows(const QVariantList& rows)
{
    QList<int> next;
    for (const auto& value : rows) {
        bool ok = false;
        const int row = value.toInt(&ok);
        if (ok && row >= 0 && row < m_layers.size() && !next.contains(row)) next.append(row);
    }
    std::sort(next.begin(), next.end());
    if (next == m_selectedRows) return;
    m_selectedRows = std::move(next);
    emit layerSelectionChanged();
}

void LayerModel::selectLayerRow(int row, bool additive)
{
    if (row < 0 || row >= m_layers.size()) return;
    if (!additive) {
        if (m_active != row) {
            m_active = row;
            emit activeLayerIndexChanged(m_active);
        }
        setSelectedLayerRows({row});
        return;
    }
    if (m_selectedRows.contains(row)) m_selectedRows.removeAll(row);
    else m_selectedRows.append(row);
    std::sort(m_selectedRows.begin(), m_selectedRows.end());
    emit layerSelectionChanged();
}

bool LayerModel::isLayerRowSelected(int row) const
{
    return m_selectedRows.contains(row);
}

void LayerModel::addLayer()
{
    const int row = m_layers.size();
    beginInsertRows({}, row, row);
    auto* l = new Layer(QString("Layer %1").arg(row + 1), this);
    l->tiles().setDocumentBudget(m_documentBudget);
    l->materialTiles().setDocumentBudget(m_documentBudget);
    l->tiles().resizeEmpty(m_canvasSize.width(), m_canvasSize.height());
    l->materialTiles().resizeEmpty(m_canvasSize.width(), m_canvasSize.height());
    l->setIndex(row);
    connect(l, &Layer::nameChanged,       this, [this,l]{ onLayerChanged(l, {NameRole}); });
    connect(l, &Layer::visibilityChanged, this, [this,l]{ onLayerChanged(l, {VisibleRole}); });
    connect(l, &Layer::lockedChanged,     this, [this,l]{ onLayerChanged(l, {LockedRole}); });
    connect(l, &Layer::opacityChanged,    this, [this,l]{ onLayerChanged(l, {OpacityRole}); });
    connect(l, &Layer::blendModeChanged,  this, [this,l]{ onLayerChanged(l, {BlendModeRole}); });
    connect(l, &Layer::colorLabelChanged, this, [this,l]{ onLayerChanged(l, {ColorLabelRole}); });
    m_layers.append(l);
    endInsertRows();
    if (m_active < 0) {
        m_active = row;
        m_selectedRows = {row};
        emit activeLayerIndexChanged(m_active);
        emit layerSelectionChanged();
    }
}

void LayerModel::removeLayer(int row)
{
    if (row < 0 || row >= m_layers.size() || m_layers.size() <= 1) return;
    // A synchronous artwork callback runs inside the store or a brush dab.
    // Deleting any layer there can invalidate an active operation's owner.
    for (const auto* layer : m_layers)
        if (layer->tiles().notifyingChange() || layer->materialTiles().notifyingChange()) return;
    const int previousActive = m_active;
    beginRemoveRows({}, row, row);
    delete m_layers.takeAt(row);
    if (row < m_active) --m_active;
    else if (m_active >= m_layers.size()) m_active = m_layers.size() - 1;
    for (int i = 0; i < m_layers.size(); ++i) m_layers[i]->setIndex(i);
    endRemoveRows();
    // Removing the active row may leave the numeric index unchanged while
    // replacing the selected object. Consumers need to re-read layerAt().
    m_selectedRows = m_active >= 0 ? QList<int>{m_active} : QList<int>{};
    emit layerSelectionChanged();
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
    m_selectedRows = m_active >= 0 ? QList<int>{m_active} : QList<int>{};
    emit layerSelectionChanged();
    if (m_active != previousActive) emit activeLayerIndexChanged(m_active);
}

}  // namespace komapaint
