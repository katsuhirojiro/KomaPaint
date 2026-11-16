#pragma once

#include <QQuickPaintedItem>
#include <QPointer>

namespace komapaint {

class PaintMixerController;

class MixerSurfaceItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(komapaint::PaintMixerController* mixer READ mixer WRITE setMixer NOTIFY mixerChanged)
    Q_PROPERTY(QString hoverDepositId READ hoverDepositId NOTIFY hoverChanged)

public:
    explicit MixerSurfaceItem(QQuickItem* parent = nullptr);

    PaintMixerController* mixer() const { return m_mixer; }
    void setMixer(PaintMixerController* mixer);
    QString hoverDepositId() const { return m_hoverDepositId; }

    void paint(QPainter* painter) override;

signals:
    void mixerChanged();
    void hoverChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    QPointF toSurfaceCoords(const QPointF& itemPos) const;
    QPointF toItemCoords(double sx, double sy) const;

    QPointer<PaintMixerController> m_mixer;
    QString m_hoverDepositId;
    QString m_dragDepositId;
    QString m_dropTargetId;
    QPointF m_dragStartItemPos;
    QPointF m_dragCurrentItemPos;
    bool m_dragging = false;
};

} // namespace komapaint
