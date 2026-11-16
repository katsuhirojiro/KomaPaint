#include "MixerSurfaceItem.h"
#include "PaintMixerController.h"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace komapaint {

MixerSurfaceItem::MixerSurfaceItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
}

void MixerSurfaceItem::setMixer(PaintMixerController* mixer)
{
    if (m_mixer == mixer) return;
    if (m_mixer) disconnect(m_mixer, nullptr, this, nullptr);
    m_mixer = mixer;
    if (m_mixer) {
        connect(m_mixer, &PaintMixerController::stateChanged, this, [this] { update(); });
    }
    emit mixerChanged();
    update();
}

QPointF MixerSurfaceItem::toSurfaceCoords(const QPointF& itemPos) const
{
    if (!m_mixer || width() <= 0.0 || height() <= 0.0) return {};
    const double sx = std::clamp(itemPos.x() / width(), 0.0, 1.0) * m_mixer->surfaceWidth();
    const double sy = std::clamp(itemPos.y() / height(), 0.0, 1.0) * m_mixer->surfaceHeight();
    return {sx, sy};
}

QPointF MixerSurfaceItem::toItemCoords(double sx, double sy) const
{
    if (!m_mixer) return {};
    return {
        (sx / double(m_mixer->surfaceWidth())) * width(),
        (sy / double(m_mixer->surfaceHeight())) * height()
    };
}

void MixerSurfaceItem::paint(QPainter* painter)
{
    if (!painter) return;
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (!m_mixer) {
        painter->fillRect(boundingRect(), QColor(QStringLiteral("#FAF8F4")));
        return;
    }

    const QImage surface = m_mixer->renderSurfaceImage();
    if (!surface.isNull()) {
        painter->drawImage(boundingRect(), surface);
    } else {
        painter->fillRect(boundingRect(), QColor(QStringLiteral("#FAF8F4")));
    }

    // Subtle porcelain grid lines
    painter->setPen(QPen(QColor(0, 0, 0, 18), 1.0));
    for (int col = 1; col < 4; ++col) {
        const qreal gx = width() * (col / 4.0);
        painter->drawLine(QPointF(gx, 0), QPointF(gx, height()));
    }
    painter->drawLine(QPointF(0, height() * 0.5), QPointF(width(), height() * 0.5));

    const auto& deposits = m_mixer->model().deposits();
    const std::string& activeId = m_mixer->model().activeDepositId();
    const double scaleX = width() / double(m_mixer->surfaceWidth());
    const double scaleY = height() / double(m_mixer->surfaceHeight());
    const double scaleR = 0.5 * (scaleX + scaleY);

    for (const auto& d : deposits) {
        const QPointF center = toItemCoords(d.x, d.y);
        const qreal r = d.radius * scaleR;
        const bool isActive = (d.id == activeId);
        const bool isDropTarget = (!m_dropTargetId.isEmpty() && d.id == m_dropTargetId.toStdString());
        const bool isHover = (!m_hoverDepositId.isEmpty() && d.id == m_hoverDepositId.toStdString());

        if (isDropTarget) {
            painter->setPen(QPen(QColor(QStringLiteral("#F08A3C")), 2.4));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(center, r + 4.0, r + 4.0);
        } else if (isActive) {
            painter->setPen(QPen(QColor(QStringLiteral("#3B82F6")), 2.0));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(center, r + 3.0, r + 3.0);
        } else if (isHover) {
            painter->setPen(QPen(QColor(0, 0, 0, 90), 1.2, Qt::DashLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(center, r + 2.0, r + 2.0);
        }

        if (d.containsDigitalImport()) {
            painter->setPen(QPen(QColor(QStringLiteral("#64748B")), 1.2, Qt::DotLine));
            painter->drawEllipse(center, std::max(4.0, r - 2.0), std::max(4.0, r - 2.0));
        }
    }

    if (m_dragging && !m_dragDepositId.isEmpty()) {
        painter->setPen(QPen(QColor(59, 130, 246, 170), 1.6, Qt::DashLine));
        painter->drawLine(m_dragStartItemPos, m_dragCurrentItemPos);
    }
}

void MixerSurfaceItem::mousePressEvent(QMouseEvent* event)
{
    if (!m_mixer) return;
    const QPointF sp = toSurfaceCoords(event->position());
    const QString hit = m_mixer->depositIdAt(sp.x(), sp.y());
    if (!hit.isEmpty()) {
        m_mixer->selectDeposit(hit);
        m_dragDepositId = hit;
        m_dragStartItemPos = event->position();
        m_dragCurrentItemPos = event->position();
        m_dragging = false;
        m_dropTargetId.clear();
        event->accept();
    } else {
        m_dragDepositId.clear();
        event->accept();
    }
}

void MixerSurfaceItem::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_mixer || m_dragDepositId.isEmpty()) return;
    const QPointF delta = event->position() - m_dragStartItemPos;
    if (!m_dragging && std::hypot(delta.x(), delta.y()) >= 6.0)
        m_dragging = true;
    if (m_dragging) {
        m_dragCurrentItemPos = event->position();
        const QPointF sp = toSurfaceCoords(event->position());
        const QString target = m_mixer->depositIdAt(sp.x(), sp.y());
        m_dropTargetId = (target != m_dragDepositId) ? target : QString{};
        update();
    }
}

void MixerSurfaceItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_mixer) return;
    if (m_dragging && !m_dragDepositId.isEmpty()) {
        const QPointF sp = toSurfaceCoords(event->position());
        if (!m_dropTargetId.isEmpty()) {
            m_mixer->mixDeposits(m_dragDepositId, m_dropTargetId, 0.5, false);
        } else {
            m_mixer->moveDeposit(m_dragDepositId, sp.x(), sp.y());
        }
    }
    m_dragDepositId.clear();
    m_dropTargetId.clear();
    m_dragging = false;
    update();
}

void MixerSurfaceItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!m_mixer) return;
    const QPointF sp = toSurfaceCoords(event->position());
    if (m_mixer->depositIdAt(sp.x(), sp.y()).isEmpty()) {
        m_mixer->createEmptyDeposit({}, sp.x(), sp.y());
    }
}

void MixerSurfaceItem::hoverMoveEvent(QHoverEvent* event)
{
    if (!m_mixer) return;
    const QPointF sp = toSurfaceCoords(event->position());
    const QString hit = m_mixer->depositIdAt(sp.x(), sp.y());
    if (hit != m_hoverDepositId) {
        m_hoverDepositId = hit;
        emit hoverChanged();
        update();
    }
}

void MixerSurfaceItem::hoverLeaveEvent(QHoverEvent*)
{
    if (!m_hoverDepositId.isEmpty()) {
        m_hoverDepositId.clear();
        emit hoverChanged();
        update();
    }
}

} // namespace komapaint
