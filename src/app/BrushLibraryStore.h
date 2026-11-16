#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace komapaint {

struct BrushPresetValues {
    int size = 16;
    int opacity = 100;
    int flow = 100;
    int spacing = 15;
    int hardness = 100;
    int stabilization = 0;
    bool textured = false;
    QString tipStyle = QStringLiteral("round");
    int pressureSize = 0;
    int pressureOpacity = 100;
    int pressureGamma = 100;
    int minSizeRatio = 15;
    int blendStrength = 0;
    int colorMix = 100;
    bool traditionalMedia = false;
    bool persistentReservoir = false;
    QString mediumId = QStringLiteral("gouache");
    QString materialPresetId = QStringLiteral("gouache-round");
    int paintLoad = 88;
    int pigmentConcentration = 82;
    int waterAmount = 22;
    int paintBody = 78;
    int pickup = 16;
    int aspectRatio = 100;
    int tipAngle = 0;
    bool followDirection = false;
    int depletion = 100;
    int edgeAccumulation = 8;
    int toothResponse = 18;
    int granulation = 5;
    int dryingSpeed = 115;
    int pressureToAmount = 62;
    int pressureToContact = 35;
    QVariantMap materialMixture;
    bool operator==(const BrushPresetValues&) const = default;
};

struct BrushEntryData {
    QString id;
    QString name;
    QString category;
    bool builtIn = false;
    BrushPresetValues defaults;
    BrushPresetValues current;
};

struct BrushTabData {
    QString id;
    QString name;
    QString currentBrushId;
    QList<BrushEntryData> brushes;
};

struct EngineCollectionData {
    QString defaultTabId;
    QString currentTabId;
    QList<BrushTabData> tabs;
};

struct BrushLibraryDocument {
    QStringList knownBuiltInIds;
    EngineCollectionData digital;
    EngineCollectionData realistic;
};

class BrushLibraryStore {
public:
    static bool parseValues(const QJsonObject& object, BrushPresetValues* out);
    static QJsonObject valuesToJson(const BrushPresetValues& values);

    static bool loadBundledCollections(const QJsonObject& root,
                                       BrushLibraryDocument* out,
                                       QString* error = nullptr);
    static bool load(const QString& filePath,
                     const BrushLibraryDocument& bundled,
                     BrushLibraryDocument* out,
                     QString* error = nullptr);
    static bool save(const QString& filePath,
                     const BrushLibraryDocument& doc,
                     QString* error = nullptr);
};

} // namespace komapaint
