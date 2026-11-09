#include "Layer.h"

namespace komapaint {

Layer::Layer(QObject* parent) : QObject(parent)
{
    m_tiles.setChangeCallback([](void* context, TilePoint tile) noexcept {
        emit static_cast<Layer*>(context)->artworkChanged(tile.x, tile.y);
    }, this);
}
Layer::Layer(const QString& name, QObject* parent)
    : Layer(parent) { m_name = name; }

void Layer::setName(const QString& name)
{
    if (m_name == name) return;
    m_name = name;
    emit nameChanged();
}
void Layer::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit visibilityChanged();
}
void Layer::setLocked(bool l)
{
    if (m_locked == l) return;
    m_locked = l;
    emit lockedChanged();
}
void Layer::setOpacity(qreal o)
{
    o = qBound(0.0, o, 1.0);
    if (qFuzzyCompare(o, m_opacity)) return;
    m_opacity = o;
    emit opacityChanged();
}
void Layer::setIndex(int i)
{
    if (m_index == i) return;
    m_index = i;
    emit indexChanged();
}

}  // namespace komapaint
