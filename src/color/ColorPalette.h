#pragma once

#include <QObject>
#include <QColor>

namespace komapaint {

// Current foreground/background drawing colors. Exposed to QML so the color
// panel, toolbar and eyedropper share one source of truth.

class ColorPalette : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor foreground READ foreground WRITE setForeground NOTIFY foregroundChanged)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY backgroundChanged)

public:
    explicit ColorPalette(QObject* parent = nullptr);

    QColor foreground() const { return m_fg; }
    QColor background() const { return m_bg; }

    Q_INVOKABLE void swap();
    Q_INVOKABLE void reset();

public slots:
    void setForeground(const QColor& c);
    void setBackground(const QColor& c);

signals:
    void foregroundChanged(const QColor& c);
    void backgroundChanged(const QColor& c);

private:
    QColor m_fg = QColor(Qt::black);
    QColor m_bg = QColor(Qt::white);
};

}  // namespace komapaint
