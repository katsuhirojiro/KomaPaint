#include "LevelsPreviewItem.h"
#include "LevelsController.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPixmap>

#include <algorithm>

namespace komapaint {

LevelsPreviewItem::LevelsPreviewItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void LevelsPreviewItem::setController(LevelsController* controller)
{
    if (m_controller == controller) return;
    if (m_controller) disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &LevelsController::previewChanged, this, [this] { update(); });
        connect(m_controller, &LevelsController::stateChanged, this, [this] { update(); });
        connect(m_controller, &LevelsController::showOriginalChanged, this, [this] { update(); });
    }
    emit controllerChanged();
    update();
}

void LevelsPreviewItem::setBorderColor(const QColor& color)
{
    if (m_borderColor == color) return;
    m_borderColor = color;
    emit borderColorChanged();
    update();
}

void LevelsPreviewItem::paint(QPainter* painter)
{
    const QRectF area(0, 0, width(), height());
    // Checkerboard makes transparent brush-tip margins legible.
    QPixmap checker(16, 16);
    checker.fill(QColor(58, 58, 58));
    {
        QPainter tile(&checker);
        tile.fillRect(0, 0, 8, 8, QColor(72, 72, 72));
        tile.fillRect(8, 8, 8, 8, QColor(72, 72, 72));
    }
    painter->fillRect(area, QBrush(checker));

    if (!m_controller) {
        painter->setPen(QColor(150, 150, 150));
        painter->drawText(area, Qt::AlignCenter, tr("No preview"));
        return;
    }
    const QImage image = m_controller->showOriginal() ? m_controller->beforeImage()
                                                      : m_controller->afterImage();
    if (image.isNull()) {
        painter->setPen(QColor(150, 150, 150));
        painter->drawText(area, Qt::AlignCenter, tr("No preview"));
        return;
    }

    const qreal inset = 1.0;
    const QRectF available = area.adjusted(inset, inset, -inset, -inset);
    const qreal scale = std::min(available.width() / image.width(),
                                 available.height() / image.height());
    const QSizeF target(qMax(1.0, image.width() * scale), qMax(1.0, image.height() * scale));
    const QRectF targetRect(available.center().x() - target.width() / 2.0,
                            available.center().y() - target.height() / 2.0,
                            target.width(), target.height());

    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(targetRect, image);

    painter->setPen(QPen(m_borderColor, 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(targetRect.adjusted(-0.5, -0.5, 0.5, 0.5));
}

}  // namespace komapaint
