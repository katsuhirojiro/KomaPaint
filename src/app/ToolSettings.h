#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace komapaint {

// Live configuration parameters for intelligent painting, fill, selection,
// cleanup, gradient, shape and future tools. Synchronized with SubToolLibrary
// just like BrushSettings so every sub-tool preset preserves all of its
// tool-specific configuration across tabs, projects, and restarts.
class ToolSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString executableTool READ executableTool WRITE setExecutableTool NOTIFY changed)

    // Shared reference & region analysis settings
    Q_PROPERTY(QString referenceMode READ referenceMode WRITE setReferenceMode NOTIFY changed)
    Q_PROPERTY(int tolerance READ tolerance WRITE setTolerance NOTIFY changed)
    Q_PROPERTY(bool perceptualColor READ perceptualColor WRITE setPerceptualColor NOTIFY changed)
    Q_PROPERTY(bool alphaAware READ alphaAware WRITE setAlphaAware NOTIFY changed)
    Q_PROPERTY(int gapClosing READ gapClosing WRITE setGapClosing NOTIFY changed)
    Q_PROPERTY(int boundarySensitivity READ boundarySensitivity WRITE setBoundarySensitivity NOTIFY changed)
    Q_PROPERTY(int areaScaling READ areaScaling WRITE setAreaScaling NOTIFY changed)
    Q_PROPERTY(bool antialiasing READ antialiasing WRITE setAntialiasing NOTIFY changed)
    Q_PROPERTY(bool colorBleeding READ colorBleeding WRITE setColorBleeding NOTIFY changed)
    Q_PROPERTY(int edgeQuality READ edgeQuality WRITE setEdgeQuality NOTIFY changed)
    Q_PROPERTY(bool previewBeforeCommit READ previewBeforeCommit WRITE setPreviewBeforeCommit NOTIFY changed)
    Q_PROPERTY(QString fillTarget READ fillTarget WRITE setFillTarget NOTIFY changed)
    Q_PROPERTY(bool contiguous READ contiguous WRITE setContiguous NOTIFY changed)

    // Selection & Lasso settings
    Q_PROPERTY(int feather READ feather WRITE setFeather NOTIFY changed)
    Q_PROPERTY(QString selectionOp READ selectionOp WRITE setSelectionOp NOTIFY changed)
    Q_PROPERTY(QString lassoMode READ lassoMode WRITE setLassoMode NOTIFY changed)
    Q_PROPERTY(int magneticStrength READ magneticStrength WRITE setMagneticStrength NOTIFY changed)
    Q_PROPERTY(QString selectionShape READ selectionShape WRITE setSelectionShape NOTIFY changed)

    // Boundary-Aware Eraser settings
    Q_PROPERTY(bool protectInterior READ protectInterior WRITE setProtectInterior NOTIFY changed)
    Q_PROPERTY(bool conservativeBoundary READ conservativeBoundary WRITE setConservativeBoundary NOTIFY changed)

    // Gradient / Shape / Text settings
    Q_PROPERTY(QString gradientType READ gradientType WRITE setGradientType NOTIFY changed)
    Q_PROPERTY(QString shapeMode READ shapeMode WRITE setShapeMode NOTIFY changed)
    Q_PROPERTY(int shapeStrokeWidth READ shapeStrokeWidth WRITE setShapeStrokeWidth NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY changed)

public:
    explicit ToolSettings(QObject* parent = nullptr) : QObject(parent) {}

    QString executableTool() const { return m_executableTool; }
    QString referenceMode() const { return m_referenceMode; }
    int tolerance() const { return m_tolerance; }
    bool perceptualColor() const { return m_perceptualColor; }
    bool alphaAware() const { return m_alphaAware; }
    int gapClosing() const { return m_gapClosing; }
    int boundarySensitivity() const { return m_boundarySensitivity; }
    int areaScaling() const { return m_areaScaling; }
    bool antialiasing() const { return m_antialiasing; }
    bool colorBleeding() const { return m_colorBleeding; }
    int edgeQuality() const { return m_edgeQuality; }
    bool previewBeforeCommit() const { return m_previewBeforeCommit; }
    QString fillTarget() const { return m_fillTarget; }
    bool contiguous() const { return m_contiguous; }
    int feather() const { return m_feather; }
    QString selectionOp() const { return m_selectionOp; }
    QString lassoMode() const { return m_lassoMode; }
    int magneticStrength() const { return m_magneticStrength; }
    QString selectionShape() const { return m_selectionShape; }
    bool protectInterior() const { return m_protectInterior; }
    bool conservativeBoundary() const { return m_conservativeBoundary; }
    QString gradientType() const { return m_gradientType; }
    QString shapeMode() const { return m_shapeMode; }
    int shapeStrokeWidth() const { return m_shapeStrokeWidth; }
    int fontSize() const { return m_fontSize; }

    void setExecutableTool(const QString& v) {
        const QString n = v.trimmed();
        if (m_executableTool != n) { m_executableTool = n; emit changed(); }
    }
    void setReferenceMode(const QString& v) {
        QString n = v.trimmed().toLower();
        if (n != QStringLiteral("current") && n != QStringLiteral("visible") &&
            n != QStringLiteral("selected") && n != QStringLiteral("exclude")) {
            n = QStringLiteral("visible");
        }
        if (m_referenceMode != n) { m_referenceMode = n; emit changed(); }
    }
    void setTolerance(int v) {
        v = qBound(0, v, 255);
        if (m_tolerance != v) { m_tolerance = v; emit changed(); }
    }
    void setPerceptualColor(bool v) {
        if (m_perceptualColor != v) { m_perceptualColor = v; emit changed(); }
    }
    void setAlphaAware(bool v) {
        if (m_alphaAware != v) { m_alphaAware = v; emit changed(); }
    }
    void setGapClosing(int v) {
        v = qBound(0, v, 32);
        if (m_gapClosing != v) { m_gapClosing = v; emit changed(); }
    }
    void setBoundarySensitivity(int v) {
        v = qBound(0, v, 100);
        if (m_boundarySensitivity != v) { m_boundarySensitivity = v; emit changed(); }
    }
    void setAreaScaling(int v) {
        v = qBound(-16, v, 16);
        if (m_areaScaling != v) { m_areaScaling = v; emit changed(); }
    }
    void setAntialiasing(bool v) {
        if (m_antialiasing != v) { m_antialiasing = v; emit changed(); }
    }
    void setColorBleeding(bool v) {
        if (m_colorBleeding != v) { m_colorBleeding = v; emit changed(); }
    }
    void setEdgeQuality(int v) {
        v = qBound(0, v, 100);
        if (m_edgeQuality != v) { m_edgeQuality = v; emit changed(); }
    }
    void setPreviewBeforeCommit(bool v) {
        if (m_previewBeforeCommit != v) { m_previewBeforeCommit = v; emit changed(); }
    }
    void setFillTarget(const QString& v) {
        QString n = v.trimmed().toLower();
        if (n != QStringLiteral("new_layer") && n != QStringLiteral("active"))
            n = QStringLiteral("active");
        if (m_fillTarget != n) { m_fillTarget = n; emit changed(); }
    }
    void setContiguous(bool v) {
        if (m_contiguous != v) { m_contiguous = v; emit changed(); }
    }
    void setFeather(int v) {
        v = qBound(0, v, 64);
        if (m_feather != v) { m_feather = v; emit changed(); }
    }
    void setSelectionOp(const QString& v) {
        QString n = v.trimmed().toLower();
        if (n != QStringLiteral("add") && n != QStringLiteral("subtract") &&
            n != QStringLiteral("intersect") && n != QStringLiteral("replace")) {
            n = QStringLiteral("replace");
        }
        if (m_selectionOp != n) { m_selectionOp = n; emit changed(); }
    }
    void setLassoMode(const QString& v) {
        QString n = v.trimmed().toLower();
        if (n != QStringLiteral("polygonal") && n != QStringLiteral("magnetic") &&
            n != QStringLiteral("freehand")) {
            n = QStringLiteral("freehand");
        }
        if (m_lassoMode != n) { m_lassoMode = n; emit changed(); }
    }
    void setMagneticStrength(int v) {
        v = qBound(0, v, 100);
        if (m_magneticStrength != v) { m_magneticStrength = v; emit changed(); }
    }
    void setSelectionShape(const QString& v) {
        QString n = v.trimmed().toLower();
        if (n != QStringLiteral("ellipse") && n != QStringLiteral("rectangle"))
            n = QStringLiteral("rectangle");
        if (m_selectionShape != n) { m_selectionShape = n; emit changed(); }
    }
    void setProtectInterior(bool v) {
        if (m_protectInterior != v) { m_protectInterior = v; emit changed(); }
    }
    void setConservativeBoundary(bool v) {
        if (m_conservativeBoundary != v) { m_conservativeBoundary = v; emit changed(); }
    }
    void setGradientType(const QString& v) {
        QString n = v.trimmed().toLower();
        if (n != QStringLiteral("radial") && n != QStringLiteral("linear"))
            n = QStringLiteral("linear");
        if (m_gradientType != n) { m_gradientType = n; emit changed(); }
    }
    void setShapeMode(const QString& v) {
        QString n = v.trimmed().toLower();
        if (n != QStringLiteral("fill") && n != QStringLiteral("both") && n != QStringLiteral("stroke"))
            n = QStringLiteral("stroke");
        if (m_shapeMode != n) { m_shapeMode = n; emit changed(); }
    }
    void setShapeStrokeWidth(int v) {
        v = qBound(1, v, 128);
        if (m_shapeStrokeWidth != v) { m_shapeStrokeWidth = v; emit changed(); }
    }
    void setFontSize(int v) {
        v = qBound(6, v, 256);
        if (m_fontSize != v) { m_fontSize = v; emit changed(); }
    }

    QVariantMap toMap() const {
        QVariantMap map = m_extraParams;
        map.insert(QStringLiteral("referenceMode"), m_referenceMode);
        map.insert(QStringLiteral("tolerance"), m_tolerance);
        map.insert(QStringLiteral("perceptualColor"), m_perceptualColor);
        map.insert(QStringLiteral("alphaAware"), m_alphaAware);
        map.insert(QStringLiteral("gapClosing"), m_gapClosing);
        map.insert(QStringLiteral("boundarySensitivity"), m_boundarySensitivity);
        map.insert(QStringLiteral("areaScaling"), m_areaScaling);
        map.insert(QStringLiteral("antialiasing"), m_antialiasing);
        map.insert(QStringLiteral("colorBleeding"), m_colorBleeding);
        map.insert(QStringLiteral("edgeQuality"), m_edgeQuality);
        map.insert(QStringLiteral("previewBeforeCommit"), m_previewBeforeCommit);
        map.insert(QStringLiteral("fillTarget"), m_fillTarget);
        map.insert(QStringLiteral("contiguous"), m_contiguous);
        map.insert(QStringLiteral("feather"), m_feather);
        map.insert(QStringLiteral("selectionOp"), m_selectionOp);
        map.insert(QStringLiteral("lassoMode"), m_lassoMode);
        map.insert(QStringLiteral("magneticStrength"), m_magneticStrength);
        map.insert(QStringLiteral("selectionShape"), m_selectionShape);
        map.insert(QStringLiteral("protectInterior"), m_protectInterior);
        map.insert(QStringLiteral("conservativeBoundary"), m_conservativeBoundary);
        map.insert(QStringLiteral("gradientType"), m_gradientType);
        map.insert(QStringLiteral("shapeMode"), m_shapeMode);
        map.insert(QStringLiteral("shapeStrokeWidth"), m_shapeStrokeWidth);
        map.insert(QStringLiteral("fontSize"), m_fontSize);
        return map;
    }

    void fromMap(const QString& execTool, const QVariantMap& map) {
        m_executableTool = execTool.trimmed();
        m_extraParams = map;
        auto strVal = [&](const char* k, const QString& def) {
            const auto v = map.value(QLatin1String(k));
            return v.isValid() ? v.toString() : def;
        };
        auto intVal = [&](const char* k, int def) {
            const auto v = map.value(QLatin1String(k));
            return v.isValid() ? v.toInt() : def;
        };
        auto boolVal = [&](const char* k, bool def) {
            const auto v = map.value(QLatin1String(k));
            return v.isValid() ? v.toBool() : def;
        };

        m_referenceMode = strVal("referenceMode", QStringLiteral("visible"));
        m_tolerance = qBound(0, intVal("tolerance", 28), 255);
        m_perceptualColor = boolVal("perceptualColor", true);
        m_alphaAware = boolVal("alphaAware", true);
        m_gapClosing = qBound(0, intVal("gapClosing", 0), 32);
        m_boundarySensitivity = qBound(0, intVal("boundarySensitivity", 65), 100);
        m_areaScaling = qBound(-16, intVal("areaScaling", 1), 16);
        m_antialiasing = boolVal("antialiasing", true);
        m_colorBleeding = boolVal("colorBleeding", true);
        m_edgeQuality = qBound(0, intVal("edgeQuality", 80), 100);
        m_previewBeforeCommit = boolVal("previewBeforeCommit", false);
        m_fillTarget = strVal("fillTarget", QStringLiteral("active"));
        m_contiguous = boolVal("contiguous", true);
        m_feather = qBound(0, intVal("feather", 0), 64);
        m_selectionOp = strVal("selectionOp", QStringLiteral("replace"));
        m_lassoMode = strVal("lassoMode", QStringLiteral("freehand"));
        m_magneticStrength = qBound(0, intVal("magneticStrength", 65), 100);
        m_selectionShape = strVal("selectionShape", QStringLiteral("rectangle"));
        m_protectInterior = boolVal("protectInterior", true);
        m_conservativeBoundary = boolVal("conservativeBoundary", true);
        m_gradientType = strVal("gradientType", QStringLiteral("linear"));
        m_shapeMode = strVal("shapeMode", QStringLiteral("stroke"));
        m_shapeStrokeWidth = qBound(1, intVal("shapeStrokeWidth", 4), 128);
        m_fontSize = qBound(6, intVal("fontSize", 24), 256);

        // Normalize validated strings back
        setReferenceMode(m_referenceMode);
        setFillTarget(m_fillTarget);
        setSelectionOp(m_selectionOp);
        setLassoMode(m_lassoMode);
        setSelectionShape(m_selectionShape);
        setGradientType(m_gradientType);
        setShapeMode(m_shapeMode);
        emit changed();
    }

signals:
    void changed();

private:
    QString m_executableTool;
    QString m_referenceMode = QStringLiteral("visible");
    int m_tolerance = 28;
    bool m_perceptualColor = true;
    bool m_alphaAware = true;
    int m_gapClosing = 0;
    int m_boundarySensitivity = 65;
    int m_areaScaling = 1;
    bool m_antialiasing = true;
    bool m_colorBleeding = true;
    int m_edgeQuality = 80;
    bool m_previewBeforeCommit = false;
    QString m_fillTarget = QStringLiteral("active");
    bool m_contiguous = true;
    int m_feather = 0;
    QString m_selectionOp = QStringLiteral("replace");
    QString m_lassoMode = QStringLiteral("freehand");
    int m_magneticStrength = 65;
    QString m_selectionShape = QStringLiteral("rectangle");
    bool m_protectInterior = true;
    bool m_conservativeBoundary = true;
    QString m_gradientType = QStringLiteral("linear");
    QString m_shapeMode = QStringLiteral("stroke");
    int m_shapeStrokeWidth = 4;
    int m_fontSize = 24;
    QVariantMap m_extraParams;
};

} // namespace komapaint
