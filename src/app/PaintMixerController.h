#pragma once

#include "../material/PaintMixerModel.h"
#include "PaintMixerStore.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace komapaint {

class BrushSettings;
class ColorPalette;

class PaintMixerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString activeMediumId READ activeMediumId WRITE setActiveMediumId NOTIFY stateChanged)
    Q_PROPERTY(QVariantList pigmentLibrary READ pigmentLibrary NOTIFY stateChanged)
    Q_PROPERTY(QVariantList deposits READ deposits NOTIFY stateChanged)
    Q_PROPERTY(QString activeDepositId READ activeDepositId NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap activeDeposit READ activeDeposit NOTIFY stateChanged)
    Q_PROPERTY(QVariantList savedMixtures READ savedMixtures NOTIFY savedMixturesChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY stateChanged)
    Q_PROPERTY(QString redoLabel READ redoLabel NOTIFY stateChanged)
    Q_PROPERTY(QString statusNotice READ statusNotice NOTIFY noticeChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(bool brushLoadedFromMixer READ brushLoadedFromMixer NOTIFY brushStateChanged)
    Q_PROPERTY(quint64 surfaceRevision READ surfaceRevision NOTIFY stateChanged)
    Q_PROPERTY(int surfaceWidth READ surfaceWidth CONSTANT)
    Q_PROPERTY(int surfaceHeight READ surfaceHeight CONSTANT)

public:
    explicit PaintMixerController(BrushSettings* brush, ColorPalette* colors, QObject* parent = nullptr);

    int surfaceWidth() const { return material::PaintMixerModel::SurfaceWidth; }
    int surfaceHeight() const { return material::PaintMixerModel::SurfaceHeight; }

    QString activeMediumId() const;
    void setActiveMediumId(const QString& mediumId);

    QVariantList pigmentLibrary() const;
    QVariantList deposits() const;
    QString activeDepositId() const;
    QVariantMap activeDeposit() const;
    QVariantList savedMixtures() const;

    bool canUndo() const { return m_model.canUndo(); }
    bool canRedo() const { return m_model.canRedo(); }
    QString undoLabel() const { return QString::fromStdString(m_model.undoLabel()); }
    QString redoLabel() const { return QString::fromStdString(m_model.redoLabel()); }
    QString statusNotice() const { return m_statusNotice; }
    QString lastError() const { return m_lastError; }
    bool brushLoadedFromMixer() const;
    quint64 surfaceRevision() const { return m_surfaceRevision; }

    const material::PaintMixerModel& model() const noexcept { return m_model; }
    material::PaintMixerModel& model() noexcept { return m_model; }

    Q_INVOKABLE bool initializePersistence(const QString& filePath);
    Q_INVOKABLE QString persistencePath() const { return m_persistencePath; }

    Q_INVOKABLE void selectDeposit(const QString& id);
    Q_INVOKABLE QString depositIdAt(double x, double y) const;
    Q_INVOKABLE QString createEmptyDeposit(const QString& name = {}, double x = -1.0, double y = -1.0);
    Q_INVOKABLE bool addPigmentToActive(const QString& pigmentId, double quantity = 1.0);
    Q_INVOKABLE QString addPigmentAsNewDeposit(const QString& pigmentId, double quantity = 1.0,
                                               double x = -1.0, double y = -1.0);
    Q_INVOKABLE QVariantMap addFromColorWheel(const QColor& color,
                                              bool allowPigmentMatch = true,
                                              bool createNewDeposit = true);
    Q_INVOKABLE bool sendActiveColorToWheel();
    Q_INVOKABLE bool setComponentMass(int index, double mass);
    Q_INVOKABLE bool removeComponent(int index);
    Q_INVOKABLE bool addWater(double delta);
    Q_INVOKABLE bool setWater(double water);
    Q_INVOKABLE bool setBinder(double binder);
    Q_INVOKABLE bool setFiller(double filler);
    Q_INVOKABLE bool addPaintBody(double factorDelta);
    Q_INVOKABLE QString mixDeposits(const QString& sourceId, const QString& targetId,
                                    double sourceShare = 0.5, bool createNew = false);
    Q_INVOKABLE bool moveDeposit(const QString& id, double x, double y);
    Q_INVOKABLE bool renameActiveDeposit(const QString& name);
    Q_INVOKABLE bool resetActiveDeposit();
    Q_INVOKABLE bool removeDeposit(const QString& id);
    Q_INVOKABLE void clearPalette();

    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();

    Q_INVOKABLE bool loadActiveIntoBrush();
    Q_INVOKABLE bool loadSavedIntoBrush(const QString& savedId);

    Q_INVOKABLE QString saveActiveMixture(const QString& name, const QString& notes = {});
    Q_INVOKABLE bool openSavedMixture(const QString& savedId);
    Q_INVOKABLE QString duplicateSavedMixture(const QString& savedId);
    Q_INVOKABLE bool renameSavedMixture(const QString& savedId, const QString& newName);
    Q_INVOKABLE bool deleteSavedMixture(const QString& savedId);

    Q_INVOKABLE QImage renderSurfaceImage() const;

    static QVariantMap batchToBrushMap(const material::PaintBatch& batch);

signals:
    void stateChanged();
    void savedMixturesChanged();
    void noticeChanged();
    void errorChanged();
    void brushStateChanged();

private:
    void notifyMutation(bool persistWorkspace = true);
    void setNotice(const QString& notice);
    void setError(const QString& error);
    bool loadBatchIntoBrush(const material::PaintBatch& batch, const QString& label);
    bool persistLibrary();
    QString argbToHex(std::uint32_t argb) const;
    QString summarizeBatch(const material::PaintBatch& batch) const;

    BrushSettings* m_brush = nullptr;
    ColorPalette* m_colors = nullptr;
    material::PaintMixerModel m_model;
    std::vector<SavedMixture> m_savedMixtures;
    QString m_persistencePath;
    QString m_statusNotice;
    QString m_lastError;
    quint64 m_surfaceRevision = 1;
    mutable QVariantList m_cachedPigmentLibrary;
    mutable std::string m_cachedPigmentMedium;
    mutable QImage m_cachedSurfaceImage;
    mutable quint64 m_cachedSurfaceRevision = 0;
    mutable std::vector<std::uint32_t> m_surfacePixelBuffer;
};

} // namespace komapaint
