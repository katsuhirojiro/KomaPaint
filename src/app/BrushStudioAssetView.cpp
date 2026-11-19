#include "BrushStudioAssetView.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace komapaint {
namespace {

QImage sampleResource(const brush::BrushResource& source) {
    const auto bytes = source.expectedBytes();
    if (!bytes || bytes != source.pixels().size()) return {};
    // Keep this display cache bounded even for large .kb resources. View
    // magnification scales the sample; it does not claim a 1:1 pixel view.
    constexpr int maxEdge = 640;
    QSize size(source.width, source.height);
    if (std::max(size.width(), size.height()) > maxEdge) size.scale(maxEdge, maxEdge, Qt::KeepAspectRatio);
    size = size.expandedTo(QSize(1, 1));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    const auto& pixels = source.pixels();
    const bool rgba = source.kind == brush::ResourceKind::Rgba8;
    for (int y = 0; y < size.height(); ++y) {
        const auto sy = static_cast<std::size_t>((y + 0.5) * source.height / size.height());
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const auto sx = static_cast<std::size_t>((x + 0.5) * source.width / size.width());
            const auto index = sy * static_cast<std::size_t>(source.width) + sx;
            if (rgba) {
                const auto* pixel = pixels.data() + 4 * index;
                line[x] = qPremultiply(qRgba(pixel[0], pixel[1], pixel[2], pixel[3]));
            } else if (source.kind == brush::ResourceKind::Alpha8) {
                line[x] = qRgba(0, 0, 0, pixels[index]);
            } else {
                const auto gray = pixels[index];
                line[x] = qRgb(gray, gray, gray);
            }
        }
    }
    return image;
}

} // namespace

BrushStudioAssetView::BrushStudioAssetView(QQuickItem* parent) : QQuickPaintedItem(parent) {
    connect(this, &BrushStudioAssetView::viewChanged, this, [this] { update(); });
}

void BrushStudioAssetView::setDraft(BrushStudioModel* model) {
    if (m_draft == model) return;
    if (m_draft) disconnect(m_draft, nullptr, this, nullptr);
    m_draft = model;
    if (m_draft) {
        connect(m_draft, &BrushStudioModel::componentsChanged, this, &BrushStudioAssetView::refreshImage);
        connect(m_draft, &QObject::destroyed, this, [this] {
            m_draft = nullptr;
            refreshImage();
            emit draftChanged();
        });
    }
    refreshImage();
    emit draftChanged();
}

void BrushStudioAssetView::refreshImage() {
    const auto* source = m_draft ? m_draft->selectedResource() : nullptr;
    if (source && m_source && &source->pixels() == &m_source->pixels() &&
        source->width == m_source->width && source->height == m_source->height && source->kind == m_source->kind) return;
    if (!source && !m_source) return;
    m_source = source ? std::optional(*source) : std::nullopt;
    m_image = source ? sampleResource(*source) : QImage{};
    emit imageChanged();
    update();
}

void BrushStudioAssetView::paint(QPainter* painter) {
    if (!hasImage() || width() <= 0 || height() <= 0) return;
    const qreal zoom = std::isfinite(m_magnification) ? std::clamp(m_magnification, 0.5, 3.0) : 1.0;
    const auto viewport = boundingRect();
    // Fit one image, or fit the centre tile to roughly a third of the
    // viewport. The tiled brush fills the viewport without a per-tile loop.
    const QSizeF source(m_source->width, m_source->height);
    const QSizeF sample = source.scaled(viewport.size() * (m_tiled ? 0.32 : 0.9), Qt::KeepAspectRatio) * zoom;
    const QRectF base(viewport.center() - QPointF(sample.width() / 2, sample.height() / 2), sample);
    QImage checker(16, 16, QImage::Format_RGB32);
    if (checker.isNull()) return;
    checker.fill(m_checkerLight);
    {
        QPainter fill(&checker);
        fill.fillRect(8, 0, 8, 8, m_checkerDark);
        fill.fillRect(0, 8, 8, 8, m_checkerDark);
    }
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    if (m_tiled) {
        painter->fillRect(viewport, QBrush(checker));
        QBrush texture(m_image);
        QTransform transform;
        transform.translate(base.x(), base.y());
        transform.scale(base.width() / m_image.width(), base.height() / m_image.height());
        texture.setTransform(transform);
        painter->fillRect(viewport, texture);
        painter->setPen(QPen(m_checkerLight, 1, Qt::DashLine));
        painter->drawRect(base);
    } else {
        painter->fillRect(base, QBrush(checker));
        painter->drawImage(base, m_image);
    }
}

} // namespace komapaint
