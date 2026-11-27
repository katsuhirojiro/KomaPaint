#pragma once

#include "PointerInput.h"
#include "SpeedDynamics.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace komapaint {
class Settings;
}

namespace komapaint::input {

class TabletInputSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString windowsTabletMode READ windowsTabletMode WRITE setWindowsTabletMode NOTIFY configChanged)
    Q_PROPERTY(bool windowsDisableWmPointer READ windowsDisableWmPointer WRITE setWindowsDisableWmPointer NOTIFY configChanged)
    Q_PROPERTY(double pressureThreshold READ pressureThreshold WRITE setPressureThreshold NOTIFY configChanged)
    Q_PROPERTY(double pressureMax READ pressureMax WRITE setPressureMax NOTIFY configChanged)
    Q_PROPERTY(double pressureGamma READ pressureGamma WRITE setPressureGamma NOTIFY configChanged)
    Q_PROPERTY(double pressureCurveLow READ pressureCurveLow WRITE setPressureCurveLow NOTIFY configChanged)
    Q_PROPERTY(double pressureCurveMid READ pressureCurveMid WRITE setPressureCurveMid NOTIFY configChanged)
    Q_PROPERTY(double pressureCurveHigh READ pressureCurveHigh WRITE setPressureCurveHigh NOTIFY configChanged)
    Q_PROPERTY(int stabilizationDefault READ stabilizationDefault WRITE setStabilizationDefault NOTIFY configChanged)
    Q_PROPERTY(QString touchMode READ touchMode WRITE setTouchMode NOTIFY configChanged)
    Q_PROPERTY(bool touchPinchZoom READ touchPinchZoom WRITE setTouchPinchZoom NOTIFY configChanged)
    Q_PROPERTY(bool touchRotate READ touchRotate WRITE setTouchRotate NOTIFY configChanged)
    Q_PROPERTY(bool palmRejection READ palmRejection WRITE setPalmRejection NOTIFY configChanged)
    Q_PROPERTY(QString barrelButton1Action READ barrelButton1Action WRITE setBarrelButton1Action NOTIFY configChanged)
    Q_PROPERTY(QString barrelButton2Action READ barrelButton2Action WRITE setBarrelButton2Action NOTIFY configChanged)
    Q_PROPERTY(bool eraserEndEnabled READ eraserEndEnabled WRITE setEraserEndEnabled NOTIFY configChanged)
    Q_PROPERTY(bool hoverOutlineEnabled READ hoverOutlineEnabled WRITE setHoverOutlineEnabled NOTIFY configChanged)
    Q_PROPERTY(bool tiltCursorEnabled READ tiltCursorEnabled WRITE setTiltCursorEnabled NOTIFY configChanged)
    Q_PROPERTY(int speedDynamics READ speedDynamics WRITE setSpeedDynamics NOTIFY configChanged)
    Q_PROPERTY(QString speedDynamicsSummary READ speedDynamicsSummary NOTIFY configChanged)
    Q_PROPERTY(bool speedAffectsSize READ speedAffectsSize WRITE setSpeedAffectsSize NOTIFY configChanged)
    Q_PROPERTY(bool speedAffectsOpacity READ speedAffectsOpacity WRITE setSpeedAffectsOpacity NOTIFY configChanged)
    Q_PROPERTY(bool speedAffectsFlow READ speedAffectsFlow WRITE setSpeedAffectsFlow NOTIFY configChanged)
    Q_PROPERTY(bool speedAffectsSpacing READ speedAffectsSpacing WRITE setSpeedAffectsSpacing NOTIFY configChanged)
    Q_PROPERTY(bool speedInvertSize READ speedInvertSize WRITE setSpeedInvertSize NOTIFY configChanged)
    Q_PROPERTY(bool speedInvertOpacity READ speedInvertOpacity WRITE setSpeedInvertOpacity NOTIFY configChanged)
    Q_PROPERTY(bool speedInvertFlow READ speedInvertFlow WRITE setSpeedInvertFlow NOTIFY configChanged)
    Q_PROPERTY(bool speedInvertSpacing READ speedInvertSpacing WRITE setSpeedInvertSpacing NOTIFY configChanged)

    // Diagnostics & device inspection
    Q_PROPERTY(QVariantList detectedDevices READ detectedDevices NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString winTabStatus READ winTabStatus NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool winTabAvailable READ winTabAvailable NOTIFY diagnosticsChanged)
    Q_PROPERTY(QVariantMap liveDiagnostics READ liveDiagnostics NOTIFY diagnosticsChanged)

public:
    explicit TabletInputSettings(QObject* parent = nullptr);

    void attachSettings(Settings* settings);
    void loadFromSettings();
    Q_INVOKABLE bool saveToSettings();

    InputConfig toInputConfig() const noexcept;

    QString windowsTabletMode() const { return m_windowsTabletMode; }
    void setWindowsTabletMode(const QString& mode);

    bool windowsDisableWmPointer() const { return m_windowsDisableWmPointer; }
    void setWindowsDisableWmPointer(bool disable);

    double pressureThreshold() const { return m_pressureThreshold; }
    void setPressureThreshold(double v);

    double pressureMax() const { return m_pressureMax; }
    void setPressureMax(double v);

    double pressureGamma() const { return m_pressureGamma; }
    void setPressureGamma(double v);

    double pressureCurveLow() const { return m_pressureCurveLow; }
    void setPressureCurveLow(double v);

    double pressureCurveMid() const { return m_pressureCurveMid; }
    void setPressureCurveMid(double v);

    double pressureCurveHigh() const { return m_pressureCurveHigh; }
    void setPressureCurveHigh(double v);

    int stabilizationDefault() const { return m_stabilizationDefault; }
    void setStabilizationDefault(int v);

    QString touchMode() const { return m_touchMode; }
    void setTouchMode(const QString& mode);

    bool touchPinchZoom() const { return m_touchPinchZoom; }
    void setTouchPinchZoom(bool enabled);

    bool touchRotate() const { return m_touchRotate; }
    void setTouchRotate(bool enabled);

    bool palmRejection() const { return m_palmRejection; }
    void setPalmRejection(bool enabled);

    QString barrelButton1Action() const { return m_barrelButton1Action; }
    void setBarrelButton1Action(const QString& action);

    QString barrelButton2Action() const { return m_barrelButton2Action; }
    void setBarrelButton2Action(const QString& action);

    bool eraserEndEnabled() const { return m_eraserEndEnabled; }
    void setEraserEndEnabled(bool enabled);

    bool hoverOutlineEnabled() const { return m_hoverOutlineEnabled; }
    void setHoverOutlineEnabled(bool enabled);

    bool tiltCursorEnabled() const { return m_tiltCursorEnabled; }
    void setTiltCursorEnabled(bool enabled);

    // Application-level Speed Dynamics. These are the artist's own settings
    // and never modify the selected brush document.
    int speedDynamics() const { return m_speedDynamics; }
    void setSpeedDynamics(int percent);
    QString speedDynamicsSummary() const;
    bool speedAffectsSize() const { return m_speedAffectsSize; }
    void setSpeedAffectsSize(bool enabled);
    bool speedAffectsOpacity() const { return m_speedAffectsOpacity; }
    void setSpeedAffectsOpacity(bool enabled);
    bool speedAffectsFlow() const { return m_speedAffectsFlow; }
    void setSpeedAffectsFlow(bool enabled);
    bool speedAffectsSpacing() const { return m_speedAffectsSpacing; }
    void setSpeedAffectsSpacing(bool enabled);
    bool speedInvertSize() const { return m_speedInvertSize; }
    void setSpeedInvertSize(bool inverted);
    bool speedInvertOpacity() const { return m_speedInvertOpacity; }
    void setSpeedInvertOpacity(bool inverted);
    bool speedInvertFlow() const { return m_speedInvertFlow; }
    void setSpeedInvertFlow(bool inverted);
    bool speedInvertSpacing() const { return m_speedInvertSpacing; }
    void setSpeedInvertSpacing(bool inverted);

    SpeedDynamicsConfig speedDynamicsConfig() const noexcept;

    Q_INVOKABLE QVariantMap toVariantMap() const;
    Q_INVOKABLE QVariantMap defaultVariantMap() const;
    Q_INVOKABLE bool applyVariantMap(const QVariantMap& map);
    Q_INVOKABLE void restoreDefaults();

    Q_INVOKABLE double evaluatePressureCurve(double rawInput,
                                             double threshold,
                                             double maxCal,
                                             double gamma,
                                             double low,
                                             double mid,
                                             double high) const;

    Q_INVOKABLE void refreshDevices();
    QVariantList detectedDevices() const { return m_detectedDevices; }
    QString winTabStatus() const { return m_winTabStatus; }
    bool winTabAvailable() const { return m_winTabAvailable; }
    QVariantMap liveDiagnostics() const { return m_liveDiagnostics; }

    void updateLiveDiagnostics(const PointerSample& sample,
                               const InputTelemetry& telemetry,
                               bool winTabActive);

signals:
    void configChanged();
    void diagnosticsChanged();

private:
    static WindowsTabletMode parseWindowsMode(const QString& s) noexcept;
    static TouchCanvasMode parseTouchMode(const QString& s) noexcept;
    static BarrelAction parseBarrelAction(const QString& s) noexcept;

    QPointer<Settings> m_settings;

    QString m_windowsTabletMode = QStringLiteral("auto");
    bool m_windowsDisableWmPointer = false;
    double m_pressureThreshold = 0.02;
    double m_pressureMax = 1.0;
    double m_pressureGamma = 1.0;
    double m_pressureCurveLow = 0.25;
    double m_pressureCurveMid = 0.50;
    double m_pressureCurveHigh = 0.75;
    int m_stabilizationDefault = 0;
    QString m_touchMode = QStringLiteral("stylus_nav_only");
    bool m_touchPinchZoom = true;
    bool m_touchRotate = true;
    bool m_palmRejection = true;
    QString m_barrelButton1Action = QStringLiteral("right_click");
    QString m_barrelButton2Action = QStringLiteral("pan");
    bool m_eraserEndEnabled = true;
    bool m_hoverOutlineEnabled = true;
    bool m_tiltCursorEnabled = true;
    int m_speedDynamics = 0;
    bool m_speedAffectsSize = true;
    bool m_speedAffectsOpacity = true;
    bool m_speedAffectsFlow = false;
    bool m_speedAffectsSpacing = false;
    bool m_speedInvertSize = false;
    bool m_speedInvertOpacity = false;
    bool m_speedInvertFlow = false;
    bool m_speedInvertSpacing = false;

    QVariantList m_detectedDevices;
    QString m_winTabStatus;
    bool m_winTabAvailable = false;
    QVariantMap m_liveDiagnostics;
};

} // namespace komapaint::input
