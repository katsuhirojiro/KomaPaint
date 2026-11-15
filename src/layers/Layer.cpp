#include "Layer.h"

#include <QImage>
#include <QPainter>
#include <QRectF>
#include <algorithm>
#include <new>
#include <stdexcept>

namespace komapaint {

Layer::Layer(QObject* parent) : QObject(parent)
{
    m_tiles.setChangeCallback([](void* context, TilePoint tile) noexcept {
        emit static_cast<Layer*>(context)->artworkChanged(tile.x, tile.y);
    }, this);
}
Layer::Layer(const QString& name, QObject* parent)
    : Layer(parent) { m_name = name; }

QVariantList Layer::thumbnailPixels(int side) const
{
    // The thumbnail is deliberately bounded and requested by a coalesced UI
    // timer, never from inside a tile-store mutation callback.
    side = std::clamp(side, 16, 64);
    if (m_tiles.notifyingChange()) return {};
    const auto& geometry = m_tiles.geometry();
    if (!geometry.isValid() || geometry.width() <= 0 || geometry.height() <= 0) return {};
    try {
        QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) return {};
        image.fill(Qt::transparent);
        const qreal scale = std::min(qreal(side) / qreal(geometry.width()),
                                     qreal(side) / qreal(geometry.height()));
        const qreal ox = (side - qreal(geometry.width()) * scale) / 2;
        const qreal oy = (side - qreal(geometry.height()) * scale) / 2;
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            for (const auto tile : m_tiles.allocatedTilePoints()) {
                const auto bounds = geometry.tileBounds(tile);
                const auto pixels = m_tiles.tilePixels(tile);
                if (!bounds || pixels.empty()) continue;
                const int tileSize = geometry.tileSize();
                // QImage borrows this span only for the immediate draw call.
                const QImage source(reinterpret_cast<const uchar*>(pixels.data()), tileSize, tileSize,
                                    tileSize * int(sizeof(std::uint32_t)), QImage::Format_ARGB32_Premultiplied);
                const qreal w = qreal(bounds->right - bounds->left);
                const qreal h = qreal(bounds->bottom - bounds->top);
                painter.drawImage(QRectF(ox + qreal(bounds->left) * scale, oy + qreal(bounds->top) * scale,
                                         w * scale, h * scale), source, QRectF(0, 0, w, h));
            }
        }
        // Canvas ImageData expects unpremultiplied channels.
        const QImage straight = image.convertToFormat(QImage::Format_ARGB32);
        if (straight.isNull()) return {};
        QVariantList result;
        result.reserve(side * side);
        for (int y = 0; y < side; ++y) {
            const auto* line = reinterpret_cast<const QRgb*>(straight.constScanLine(y));
            for (int x = 0; x < side; ++x) result.append(QVariant::fromValue(quint32(line[x])));
        }
        return result;
    } catch (const std::bad_alloc&) { return {}; }
      catch (const std::length_error&) { return {}; }
}

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
void Layer::setBlendMode(int mode)
{
    // Values are part of the v3 file format; reject unknown modes rather than
    // silently saving a different appearance.
    if (mode < 0 || mode > 3 || mode == m_blendMode) return;
    m_blendMode = mode;
    emit blendModeChanged();
}
void Layer::setColorLabel(int label)
{
    if (label < 0 || label > 7 || label == m_colorLabel) return;
    m_colorLabel = label;
    emit colorLabelChanged();
}
void Layer::setIndex(int i)
{
    if (m_index == i) return;
    m_index = i;
    emit indexChanged();
}

}  // namespace komapaint
