#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>

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
    Q_PROPERTY(QString tipStyle READ tipStyle WRITE setTipStyle NOTIFY changed)
    // 100 = crisp edge, 0 = fades from the centre of the tip to its rim.
    Q_PROPERTY(int hardness READ hardness WRITE setHardness NOTIFY changed)
    // 0 = the line follows the pointer exactly; higher values smooth it.
    Q_PROPERTY(int stabilization READ stabilization WRITE setStabilization NOTIFY changed)
    // Digital brush pressure dynamics & blending (used only by the Digital engine).
    Q_PROPERTY(int pressureSize READ pressureSize WRITE setPressureSize NOTIFY changed)
    Q_PROPERTY(int pressureOpacity READ pressureOpacity WRITE setPressureOpacity NOTIFY changed)
    Q_PROPERTY(int pressureFlow READ pressureFlow WRITE setPressureFlow NOTIFY changed)
    Q_PROPERTY(int pressureGamma READ pressureGamma WRITE setPressureGamma NOTIFY changed)
    Q_PROPERTY(int minSizeRatio READ minSizeRatio WRITE setMinSizeRatio NOTIFY changed)
    Q_PROPERTY(int tiltSize READ tiltSize WRITE setTiltSize NOTIFY changed)
    Q_PROPERTY(bool tiltAngle READ tiltAngle WRITE setTiltAngle NOTIFY changed)
    Q_PROPERTY(bool stylusRotation READ stylusRotation WRITE setStylusRotation NOTIFY changed)
    Q_PROPERTY(int blendStrength READ blendStrength WRITE setBlendStrength NOTIFY changed)
    Q_PROPERTY(int colorMix READ colorMix WRITE setColorMix NOTIFY changed)
    // True while an eraser is selected: strokes remove paint from the layer.
    Q_PROPERTY(bool erase READ erase WRITE setErase NOTIFY changed)
    Q_PROPERTY(QString brushEngine READ brushEngine WRITE setBrushEngine NOTIFY brushEngineChanged)
    Q_PROPERTY(bool realisticEngine READ isRealisticEngine NOTIFY brushEngineChanged)
    Q_PROPERTY(bool traditionalMedia READ traditionalMedia WRITE setTraditionalMedia NOTIFY changed)
    Q_PROPERTY(bool presetRequiresRealistic READ presetRequiresRealistic NOTIFY presetAvailabilityChanged)
    Q_PROPERTY(bool blockedByDigitalEngine READ isBlockedByDigitalEngine NOTIFY presetAvailabilityChanged)
    // False by default: every new stroke starts with a clean, full reservoir
    // of the configured colour/mixture. True keeps remaining paint across
    // strokes as an optional advanced workflow.
    Q_PROPERTY(bool persistentReservoir READ persistentReservoir WRITE setPersistentReservoir NOTIFY changed)
    // Traditional material brush properties (also stored per preset in SubToolLibrary).
    Q_PROPERTY(QString mediumId READ mediumId WRITE setMediumId NOTIFY changed)
    Q_PROPERTY(QString materialPresetId READ materialPresetId WRITE setMaterialPresetId NOTIFY changed)
    Q_PROPERTY(int paintLoad READ paintLoad WRITE setPaintLoad NOTIFY changed)
    Q_PROPERTY(int pigmentConcentration READ pigmentConcentration WRITE setPigmentConcentration NOTIFY changed)
    Q_PROPERTY(int waterAmount READ waterAmount WRITE setWaterAmount NOTIFY changed)
    Q_PROPERTY(int paintBody READ paintBody WRITE setPaintBody NOTIFY changed)
    Q_PROPERTY(int pickup READ pickup WRITE setPickup NOTIFY changed)
    Q_PROPERTY(int aspectRatio READ aspectRatio WRITE setAspectRatio NOTIFY changed)
    Q_PROPERTY(int tipAngle READ tipAngle WRITE setTipAngle NOTIFY changed)
    Q_PROPERTY(bool followDirection READ followDirection WRITE setFollowDirection NOTIFY changed)
    Q_PROPERTY(int depletion READ depletion WRITE setDepletion NOTIFY changed)
    Q_PROPERTY(int edgeAccumulation READ edgeAccumulation WRITE setEdgeAccumulation NOTIFY changed)
    Q_PROPERTY(int toothResponse READ toothResponse WRITE setToothResponse NOTIFY changed)
    Q_PROPERTY(int granulation READ granulation WRITE setGranulation NOTIFY changed)
    Q_PROPERTY(int dryingSpeed READ dryingSpeed WRITE setDryingSpeed NOTIFY changed)
    Q_PROPERTY(int pressureToAmount READ pressureToAmount WRITE setPressureToAmount NOTIFY changed)
    Q_PROPERTY(int pressureToContact READ pressureToContact WRITE setPressureToContact NOTIFY changed)
    // Full material batch supplied by the mixer. The component masses and
    // residual identities are kept verbatim until a material stroke loads it.
    Q_PROPERTY(QVariantMap materialMixture READ materialMixture NOTIFY materialMixtureChanged)
    Q_PROPERTY(bool hasMaterialMixture READ hasMaterialMixture NOTIFY materialMixtureChanged)
    Q_PROPERTY(int minimumSize READ minimumSize CONSTANT)
    Q_PROPERTY(int maximumSize READ maximumSize CONSTANT)
    Q_PROPERTY(int minimumSpacing READ minimumSpacing CONSTANT)
    Q_PROPERTY(int maximumSpacing READ maximumSpacing CONSTANT)
public:
    enum class BrushEngineMode { Digital, Realistic };
    Q_ENUM(BrushEngineMode)
    static QString normalizeBrushEngine(const QString& engine) {
        return engine.trimmed().compare(QStringLiteral("realistic"), Qt::CaseInsensitive) == 0
            ? QStringLiteral("realistic") : QStringLiteral("digital");
    }
    static QString normalizeTipStyle(const QString& style) {
        const QString s = style.trimmed().toLower();
        if (s == QStringLiteral("textured") || s == QStringLiteral("bristle")
            || s == QStringLiteral("graphite") || s == QStringLiteral("stipple"))
            return s;
        return QStringLiteral("round");
    }
    using QObject::QObject;
    // Limits of the brush engine (see BrushRasterizer::dab and BrushStroke).
    static constexpr int MinSize = 1, MaxSize = 256, MinSpacing = 5, MaxSpacing = 50;
    int size() const { return m_size; }
    int opacity() const { return m_opacity; }
    int flow() const { return m_flow; }
    int spacing() const { return m_spacing; }
    bool textured() const { return m_textured; }
    QString tipStyle() const { return m_tipStyle; }
    int hardness() const { return m_hardness; }
    int stabilization() const { return m_stabilization; }
    int pressureSize() const { return m_pressureSize; }
    int pressureOpacity() const { return m_pressureOpacity; }
    int pressureFlow() const { return m_pressureFlow; }
    int pressureGamma() const { return m_pressureGamma; }
    int minSizeRatio() const { return m_minSizeRatio; }
    int tiltSize() const { return m_tiltSize; }
    bool tiltAngle() const { return m_tiltAngle; }
    bool stylusRotation() const { return m_stylusRotation; }
    int blendStrength() const { return m_blendStrength; }
    int colorMix() const { return m_colorMix; }
    bool erase() const { return m_erase; }
    QString brushEngine() const { return m_brushEngine; }
    bool isRealisticEngine() const { return m_brushEngine == QStringLiteral("realistic"); }
    BrushEngineMode engineMode() const {
        return isRealisticEngine() ? BrushEngineMode::Realistic : BrushEngineMode::Digital;
    }
    bool traditionalMedia() const { return m_traditionalMedia; }
    bool presetRequiresRealistic() const { return m_traditionalMedia; }
    bool isBlockedByDigitalEngine() const { return !isRealisticEngine() && m_traditionalMedia; }
    bool persistentReservoir() const { return m_persistentReservoir; }
    QString mediumId() const { return m_mediumId; }
    QString materialPresetId() const { return m_materialPresetId; }
    int paintLoad() const { return m_paintLoad; }
    int pigmentConcentration() const { return m_pigmentConcentration; }
    int waterAmount() const { return m_waterAmount; }
    int paintBody() const { return m_paintBody; }
    int pickup() const { return m_pickup; }
    int aspectRatio() const { return m_aspectRatio; }
    int tipAngle() const { return m_tipAngle; }
    bool followDirection() const { return m_followDirection; }
    int depletion() const { return m_depletion; }
    int edgeAccumulation() const { return m_edgeAccumulation; }
    int toothResponse() const { return m_toothResponse; }
    int granulation() const { return m_granulation; }
    int dryingSpeed() const { return m_dryingSpeed; }
    int pressureToAmount() const { return m_pressureToAmount; }
    int pressureToContact() const { return m_pressureToContact; }
    QVariantMap materialMixture() const { return m_materialMixture; }
    bool hasMaterialMixture() const { return !m_materialMixture.isEmpty(); }
    int minimumSize() const { return MinSize; }
    int maximumSize() const { return MaxSize; }
    int minimumSpacing() const { return MinSpacing; }
    int maximumSpacing() const { return MaxSpacing; }
    void setSize(int v) { v = qBound(MinSize, v, MaxSize); if (m_size != v) { m_size = v; emit changed(); } }
    void setOpacity(int v) { v = qBound(0, v, 100); if (m_opacity != v) { m_opacity = v; emit changed(); } }
    void setFlow(int v) { v = qBound(0, v, 100); if (m_flow != v) { m_flow = v; emit changed(); } }
    void setSpacing(int v) { v = qBound(MinSpacing, v, MaxSpacing); if (m_spacing != v) { m_spacing = v; emit changed(); } }
    void setTextured(bool v) {
        const QString nextStyle = v
            ? (m_tipStyle == QStringLiteral("round") ? QStringLiteral("textured") : m_tipStyle)
            : QStringLiteral("round");
        if (m_textured == v && m_tipStyle == nextStyle) return;
        m_textured = v;
        m_tipStyle = nextStyle;
        emit changed();
    }
    void setTipStyle(const QString& style) {
        const QString normalized = normalizeTipStyle(style);
        const bool nextTextured = normalized != QStringLiteral("round");
        if (m_tipStyle == normalized && m_textured == nextTextured) return;
        m_tipStyle = normalized;
        m_textured = nextTextured;
        emit changed();
    }
    void setHardness(int v) { v = qBound(0, v, 100); if (m_hardness != v) { m_hardness = v; emit changed(); } }
    void setStabilization(int v) { v = qBound(0, v, 100); if (m_stabilization != v) { m_stabilization = v; emit changed(); } }
    void setPressureSize(int v) { v = qBound(0, v, 100); if (m_pressureSize != v) { m_pressureSize = v; emit changed(); } }
    void setPressureOpacity(int v) { v = qBound(0, v, 100); if (m_pressureOpacity != v) { m_pressureOpacity = v; emit changed(); } }
    void setPressureFlow(int v) { v = qBound(0, v, 100); if (m_pressureFlow != v) { m_pressureFlow = v; emit changed(); } }
    void setPressureGamma(int v) { v = qBound(40, v, 200); if (m_pressureGamma != v) { m_pressureGamma = v; emit changed(); } }
    void setMinSizeRatio(int v) { v = qBound(5, v, 100); if (m_minSizeRatio != v) { m_minSizeRatio = v; emit changed(); } }
    void setTiltSize(int v) { v = qBound(0, v, 100); if (m_tiltSize != v) { m_tiltSize = v; emit changed(); } }
    void setTiltAngle(bool v) { if (m_tiltAngle != v) { m_tiltAngle = v; emit changed(); } }
    void setStylusRotation(bool v) { if (m_stylusRotation != v) { m_stylusRotation = v; emit changed(); } }
    void setBlendStrength(int v) { v = qBound(0, v, 100); if (m_blendStrength != v) { m_blendStrength = v; emit changed(); } }
    void setColorMix(int v) { v = qBound(0, v, 100); if (m_colorMix != v) { m_colorMix = v; emit changed(); } }
    void setErase(bool v) { if (m_erase != v) { m_erase = v; emit changed(); } }
    void setBrushEngine(const QString& engine) {
        const QString normalized = normalizeBrushEngine(engine);
        if (m_brushEngine == normalized) return;
        m_brushEngine = normalized;
        emit brushEngineChanged();
        emit presetAvailabilityChanged();
    }
    void setTraditionalMedia(bool v) {
        if (m_traditionalMedia != v) {
            m_traditionalMedia = v;
            emit changed();
            emit presetAvailabilityChanged();
        }
    }
    void setPersistentReservoir(bool v) { if (m_persistentReservoir != v) { m_persistentReservoir = v; emit changed(); } }
    void setMediumId(const QString& v) {
        const QString normalized = v.trimmed() == QStringLiteral("watercolour")
            ? QStringLiteral("watercolour") : QStringLiteral("gouache");
        if (m_mediumId == normalized) return;
        m_mediumId = normalized;
        if (normalized == QStringLiteral("watercolour")) {
            if (m_materialPresetId == QStringLiteral("gouache-round")
                || m_materialPresetId == QStringLiteral("dry-gouache")
                || m_materialPresetId == QStringLiteral("tiny-gouache"))
                m_materialPresetId = QStringLiteral("watercolour-round");
        } else {
            if (m_materialPresetId.startsWith(QStringLiteral("watercolour-"))
                || m_materialPresetId == QStringLiteral("granulating-watercolour")
                || m_materialPresetId == QStringLiteral("clean-water")
                || m_materialPresetId == QStringLiteral("paint-lifting"))
                m_materialPresetId = QStringLiteral("gouache-round");
        }
        emit changed();
    }
    void setMaterialPresetId(const QString& v) {
        const QString normalized = v.trimmed().isEmpty() ? QStringLiteral("gouache-round") : v.trimmed();
        if (m_materialPresetId != normalized) { m_materialPresetId = normalized; emit changed(); }
    }
    void setPaintLoad(int v) { v = qBound(10, v, 100); if (m_paintLoad != v) { m_paintLoad = v; emit changed(); } }
    void setPigmentConcentration(int v) { v = qBound(10, v, 100); if (m_pigmentConcentration != v) { m_pigmentConcentration = v; emit changed(); } }
    void setWaterAmount(int v) { v = qBound(0, v, 100); if (m_waterAmount != v) { m_waterAmount = v; emit changed(); } }
    void setPaintBody(int v) { v = qBound(0, v, 100); if (m_paintBody != v) { m_paintBody = v; emit changed(); } }
    void setPickup(int v) { v = qBound(0, v, 100); if (m_pickup != v) { m_pickup = v; emit changed(); } }
    void setAspectRatio(int v) { v = qBound(100, v, 400); if (m_aspectRatio != v) { m_aspectRatio = v; emit changed(); } }
    void setTipAngle(int v) { v = qBound(-180, v, 180); if (m_tipAngle != v) { m_tipAngle = v; emit changed(); } }
    void setFollowDirection(bool v) { if (m_followDirection != v) { m_followDirection = v; emit changed(); } }
    void setDepletion(int v) { v = qBound(0, v, 200); if (m_depletion != v) { m_depletion = v; emit changed(); } }
    void setEdgeAccumulation(int v) { v = qBound(0, v, 100); if (m_edgeAccumulation != v) { m_edgeAccumulation = v; emit changed(); } }
    void setToothResponse(int v) { v = qBound(0, v, 100); if (m_toothResponse != v) { m_toothResponse = v; emit changed(); } }
    void setGranulation(int v) { v = qBound(0, v, 100); if (m_granulation != v) { m_granulation = v; emit changed(); } }
    void setDryingSpeed(int v) { v = qBound(25, v, 200); if (m_dryingSpeed != v) { m_dryingSpeed = v; emit changed(); } }
    void setPressureToAmount(int v) { v = qBound(0, v, 100); if (m_pressureToAmount != v) { m_pressureToAmount = v; emit changed(); } }
    void setPressureToContact(int v) { v = qBound(0, v, 100); if (m_pressureToContact != v) { m_pressureToContact = v; emit changed(); } }
    Q_INVOKABLE bool loadMaterialMixture(const QVariantMap& mixture) {
        if (mixture.isEmpty()) return false;
        m_materialMixture = mixture;
        ++m_mixtureRevision;
        emit materialMixtureChanged();
        return true;
    }
    Q_INVOKABLE void clearMaterialMixture() {
        if (m_materialMixture.isEmpty()) return;
        m_materialMixture.clear();
        ++m_mixtureRevision;
        emit materialMixtureChanged();
    }
    quint64 mixtureRevision() const { return m_mixtureRevision; }
signals:
    void changed();
    void brushEngineChanged();
    void presetAvailabilityChanged();
    void materialMixtureChanged();
private:
    int m_size = 16, m_opacity = 100, m_flow = 100, m_spacing = 15;
    int m_hardness = 100, m_stabilization = 0;
    bool m_textured = false;
    QString m_tipStyle = QStringLiteral("round");
    int m_pressureSize = 0;
    int m_pressureOpacity = 100;
    int m_pressureFlow = 0;
    int m_pressureGamma = 100;
    int m_minSizeRatio = 15;
    int m_tiltSize = 0;
    bool m_tiltAngle = false;
    bool m_stylusRotation = false;
    int m_blendStrength = 0;
    int m_colorMix = 100;
    bool m_erase = false;
    QString m_brushEngine = QStringLiteral("digital");
    bool m_traditionalMedia = false;
    bool m_persistentReservoir = false;
    QString m_mediumId = QStringLiteral("gouache");
    QString m_materialPresetId = QStringLiteral("gouache-round");
    int m_paintLoad = 88;
    int m_pigmentConcentration = 82;
    int m_waterAmount = 22;
    int m_paintBody = 78;
    int m_pickup = 16;
    int m_aspectRatio = 100;
    int m_tipAngle = 0;
    bool m_followDirection = false;
    int m_depletion = 100;
    int m_edgeAccumulation = 8;
    int m_toothResponse = 18;
    int m_granulation = 5;
    int m_dryingSpeed = 115;
    int m_pressureToAmount = 62;
    int m_pressureToContact = 35;
    QVariantMap m_materialMixture;
    quint64 m_mixtureRevision = 0;
};
}
