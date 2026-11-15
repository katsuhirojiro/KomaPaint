#pragma once

#include "Layer.h"

#include <QAbstractListModel>
#include <QList>
#include <QSize>

namespace komapaint {

// Model that lists the layers of a document. Document-owned models start with
// one raster layer; the workspace's detached empty-state model starts empty.

class LayerModel : public QAbstractListModel
{
    Q_OBJECT
    // Bound from QML (LayersPanel currentIndex); must be a property so the
    // binding tracks activeLayerIndexChanged instead of reading a method.
    Q_PROPERTY(int activeLayerIndex READ activeLayerIndex WRITE setActiveLayerIndex NOTIFY activeLayerIndexChanged)

public:
    enum Roles { NameRole = Qt::UserRole + 1, VisibleRole, LockedRole, OpacityRole, IndexRole, BlendModeRole, ColorLabelRole };

    explicit LayerModel(QObject* parent = nullptr, bool createDefaultLayer = true);

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool resizeEmptyLayers(const QSize& size);
    std::shared_ptr<DocumentMemoryBudget> documentBudget() const noexcept { return m_documentBudget; }
    Q_INVOKABLE Layer* layerAt(int row) const;
    Q_INVOKABLE int    activeLayerIndex() const { return m_active; }
    Q_INVOKABLE void   setActiveLayerIndex(int row);

    Q_INVOKABLE void addLayer();
    Q_INVOKABLE void removeLayer(int row);
    Q_INVOKABLE void moveLayer(int from, int to);

signals:
    void activeLayerIndexChanged(int index);

private:
    void onLayerChanged(Layer* layer, const QList<int>& roles);

    std::shared_ptr<DocumentMemoryBudget> m_documentBudget = std::make_shared<DocumentMemoryBudget>();
    QSize m_canvasSize{1600, 1200};
    QList<Layer*> m_layers;
    int           m_active = -1;
};

}  // namespace komapaint
