#include "CanvasDocument.h"

namespace komapaint {

CanvasDocument::CanvasDocument(QObject* parent) : QObject(parent), m_layers(new LayerModel(this))
{
    connect(m_layers, &QAbstractItemModel::rowsInserted, this, [this] { connectArtworkSignals(); });
    connectArtworkSignals();
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
    emit sizeChanged();
}

void CanvasDocument::setHeight(int h)
{
    if (!validSize(QSize(width(), h)) || h == m_size.height()) return;
    if (!m_layers->resizeEmptyLayers(QSize(width(), h))) return;
    m_size.setHeight(h);
    emit sizeChanged();
}

void CanvasDocument::setSize(const QSize& size)
{
    if (!validSize(size) || size == m_size) return;
    if (!m_layers->resizeEmptyLayers(size)) return;
    m_size = size;
    emit sizeChanged();
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

}  // namespace komapaint
