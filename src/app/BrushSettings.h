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
    // 100 = crisp edge, 0 = fades from the centre of the tip to its rim.
    Q_PROPERTY(int hardness READ hardness WRITE setHardness NOTIFY changed)
    // 0 = the line follows the pointer exactly; higher values smooth it.
    Q_PROPERTY(int stabilization READ stabilization WRITE setStabilization NOTIFY changed)
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
    void setTextured(bool v) { if (m_textured != v) { m_textured = v; emit changed(); } }
    void setHardness(int v) { v = qBound(0, v, 100); if (m_hardness != v) { m_hardness = v; emit changed(); } }
    void setStabilization(int v) { v = qBound(0, v, 100); if (m_stabilization != v) { m_stabilization = v; emit changed(); } }
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
