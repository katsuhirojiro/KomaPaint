#pragma once

// A small before/after view for the Levels dialog's in-memory image target
// (the extracted brush-tip artwork). Document targets preview on the canvas
// itself, so this item is only shown when the controller has an image.
#include <QColor>
#include <QQuickPaintedItem>

namespace komapaint {

class LevelsController;

class LevelsPreviewItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(LevelsController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY borderColorChanged)

public:
    explicit LevelsPreviewItem(QQuickItem* parent = nullptr);

    LevelsController* controller() const { return m_controller; }
    void setController(LevelsController* controller);
    QColor borderColor() const { return m_borderColor; }
    void setBorderColor(const QColor& color);

    void paint(QPainter* painter) override;

signals:
    void controllerChanged();
    void borderColorChanged();

private:
    LevelsController* m_controller = nullptr;
    QColor m_borderColor{70, 70, 70};
};

}  // namespace komapaint
