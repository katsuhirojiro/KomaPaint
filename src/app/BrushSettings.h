#pragma once
#include <QObject>

namespace komapaint {
// The settings of the brush that paints the next stroke. Every property is
// read by CanvasViewport::beginStroke() and handed to the brush engine.
class BrushSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(int size READ size WRITE setSize NOTIFY changed)
    Q_PROPERTY(int opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(int flow READ flow WRITE setFlow NOTIFY changed)
    Q_PROPERTY(int spacing READ spacing WRITE setSpacing NOTIFY changed)
    Q_PROPERTY(bool textured READ textured WRITE setTextured NOTIFY changed)
    // 100 = crisp edge, 0 = fades from the centre of the tip to its rim.
    Q_PROPERTY(int hardness READ hardness WRITE setHardness NOTIFY changed)
    // 0 = the line follows the pointer exactly; higher values smooth it.
    Q_PROPERTY(int stabilization READ stabilization WRITE setStabilization NOTIFY changed)
    // True while an eraser is selected: strokes remove paint from the layer.
    Q_PROPERTY(bool erase READ erase WRITE setErase NOTIFY changed)
    Q_PROPERTY(int minimumSize READ minimumSize CONSTANT)
    Q_PROPERTY(int maximumSize READ maximumSize CONSTANT)
    Q_PROPERTY(int minimumSpacing READ minimumSpacing CONSTANT)
    Q_PROPERTY(int maximumSpacing READ maximumSpacing CONSTANT)
public:
    using QObject::QObject;
    // Limits of the brush engine (see BrushRasterizer::dab and BrushStroke).
    static constexpr int MinSize = 1, MaxSize = 256, MinSpacing = 5, MaxSpacing = 50;
    int size() const { return m_size; }
    int opacity() const { return m_opacity; }
    int flow() const { return m_flow; }
    int spacing() const { return m_spacing; }
    bool textured() const { return m_textured; }
    int hardness() const { return m_hardness; }
    int stabilization() const { return m_stabilization; }
    bool erase() const { return m_erase; }
    int minimumSize() const { return MinSize; }
    int maximumSize() const { return MaxSize; }
    int minimumSpacing() const { return MinSpacing; }
    int maximumSpacing() const { return MaxSpacing; }
    void setSize(int v) { v = qBound(MinSize, v, MaxSize); if (m_size != v) { m_size = v; emit changed(); } }
    void setOpacity(int v) { v = qBound(0, v, 100); if (m_opacity != v) { m_opacity = v; emit changed(); } }
    void setFlow(int v) { v = qBound(0, v, 100); if (m_flow != v) { m_flow = v; emit changed(); } }
    void setSpacing(int v) { v = qBound(MinSpacing, v, MaxSpacing); if (m_spacing != v) { m_spacing = v; emit changed(); } }
    void setTextured(bool v) { if (m_textured != v) { m_textured = v; emit changed(); } }
    void setHardness(int v) { v = qBound(0, v, 100); if (m_hardness != v) { m_hardness = v; emit changed(); } }
    void setStabilization(int v) { v = qBound(0, v, 100); if (m_stabilization != v) { m_stabilization = v; emit changed(); } }
    void setErase(bool v) { if (m_erase != v) { m_erase = v; emit changed(); } }
signals:
    void changed();
private:
    int m_size = 16, m_opacity = 100, m_flow = 100, m_spacing = 15;
    int m_hardness = 100, m_stabilization = 0;
    bool m_textured = false;
    bool m_erase = false;
};
}
