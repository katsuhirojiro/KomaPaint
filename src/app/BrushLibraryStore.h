#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace komapaint {
namespace brush { class BrushDocument; }

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
    int pressureFlow = 0;
    int pressureGamma = 100;
    int minSizeRatio = 15;
    int tiltSize = 0;
    bool tiltAngle = false;
    bool stylusRotation = false;
    int blendStrength = 0;
    int colorMix = 100;
    bool erase = false;
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
    // Universal sub-tool fields: executableTool decouples collection placement
    // from underlying tool behavior; toolParams stores non-brush / intelligent
    // tool configuration (Fill, Smart Fill, Wand, Lasso, Boundary-Aware Eraser, etc.).
    QString executableTool;
    QVariantMap toolParams;
    bool operator==(const BrushPresetValues&) const = default;
};

struct BrushEntryData {
    QString id;
    QString name;
    QString category;
    bool builtIn = false;
    BrushPresetValues defaults;
    BrushPresetValues current;
    // Native entries point at immutable, library-owned .kb bytes. The update
    // identity belongs to the installed original, not to duplicate aliases.
    QString nativeAssetKey;
    QString nativeUpdateId;
    // Per-install adjustments made in the Sub-Tool panel. These never edit the
    // immutable .kb asset: Apply overlays them on a runtime copy.
    QMap<QString, double> nativeOverrides;
    bool isNative() const { return !nativeAssetKey.isEmpty(); }
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
    QMap<QString, EngineCollectionData> toolCollections;
};

class BrushLibraryStore {
public:
    static bool readNativeFile(const QString& path, const QString& expectedKey,
                               brush::BrushDocument* out, QByteArray* original,
                               QString* error);
    static QString installNativeFile(const QString& libraryPath, const QByteArray& original,
                                     QString* error);
    static QString nativeFilePath(const QString& libraryPath, const QString& key);

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
