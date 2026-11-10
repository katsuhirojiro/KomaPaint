#pragma once

#include "Layer.h"

#include <QAbstractListModel>
#include <QList>
#include <QSize>

namespace komapaint {

// Model that lists the layers of the active document. In Phase 1 it ships
// pre-populated with a single default layer so the UI has something to show.

class LayerModel : public QAbstractListModel
{
    Q_OBJECT
    // Bound from QML (LayersPanel currentIndex); must be a property so the
    // binding tracks activeLayerIndexChanged instead of reading a method.
    Q_PROPERTY(int activeLayerIndex READ activeLayerIndex WRITE setActiveLayerIndex NOTIFY activeLayerIndexChanged)

public:
    enum Roles { NameRole = Qt::UserRole + 1, VisibleRole, LockedRole, OpacityRole, IndexRole };

    explicit LayerModel(QObject* parent = nullptr);

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
    int           m_active = 0;
};

}  // namespace komapaint
