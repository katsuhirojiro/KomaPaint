#include "ColorPalette.h"

namespace komapaint {

ColorPalette::ColorPalette(QObject* parent) : QObject(parent) {}

void ColorPalette::setForeground(const QColor& c)
{
    if (!c.isValid() || c == m_fg) return;
    m_fg = c;
    emit foregroundChanged(m_fg);
}
void ColorPalette::setBackground(const QColor& c)
{
    if (!c.isValid() || c == m_bg) return;
    m_bg = c;
    emit backgroundChanged(m_bg);
}
void ColorPalette::swap()
{
    QColor tmp = m_fg;
    setForeground(m_bg);
    setBackground(tmp);
}
void ColorPalette::reset()
{
    setForeground(QColor(Qt::black));
    setBackground(QColor(Qt::white));
}

}  // namespace komapaint
