#pragma once
#include <QObject>

namespace komapaint {
class BrushSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(int size READ size WRITE setSize NOTIFY changed)
    Q_PROPERTY(int opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(int flow READ flow WRITE setFlow NOTIFY changed)
    Q_PROPERTY(int spacing READ spacing WRITE setSpacing NOTIFY changed)
    Q_PROPERTY(bool textured READ textured WRITE setTextured NOTIFY changed)
public:
    using QObject::QObject;
    int size() const { return m_size; }
    int opacity() const { return m_opacity; }
    int flow() const { return m_flow; }
    int spacing() const { return m_spacing; }
    bool textured() const { return m_textured; }
    void setSize(int v) { v = qBound(1, v, 256); if (m_size != v) { m_size = v; emit changed(); } }
    void setOpacity(int v) { v = qBound(0, v, 100); if (m_opacity != v) { m_opacity = v; emit changed(); } }
    void setFlow(int v) { v = qBound(0, v, 100); if (m_flow != v) { m_flow = v; emit changed(); } }
    void setSpacing(int v) { v = qBound(5, v, 50); if (m_spacing != v) { m_spacing = v; emit changed(); } }
    void setTextured(bool v) { if (m_textured != v) { m_textured = v; emit changed(); } }
signals:
    void changed();
private:
    int m_size = 16, m_opacity = 100, m_flow = 100, m_spacing = 15;
    bool m_textured = false;
};
}
