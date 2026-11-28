#pragma once

// The one reusable Levels target/controller.
//
// It owns either:
//   * a document layer (optionally clipped to the active selection), or
//   * an extracted brush-tip image held only in memory.
//
// The QML dialog only edits properties; the controller captures originals,
// previews, commits one UndoStack pixel transaction, and restores everything on
// Cancel. The same controller is used from Edit > Adjustments and from Create
// Brush Tip, so there is no Brush Studio-specific Levels implementation.

#include "adjustments/Levels.h"
#include "canvas/SparseTileStore.h"

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

#include <cstdint>
#include <vector>

class QTimer;

namespace komapaint {

class CanvasDocument;
class Layer;
class UndoStack;

class LevelsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ isActive NOTIFY stateChanged)
    Q_PROPERTY(QString targetKind READ targetKind NOTIFY stateChanged)
    Q_PROPERTY(QString targetName READ targetName NOTIFY stateChanged)
    Q_PROPERTY(QString scopeText READ scopeText NOTIFY stateChanged)
    Q_PROPERTY(QVariantList histogram READ histogram NOTIFY histogramChanged)
    Q_PROPERTY(bool showOriginal READ showOriginal WRITE setShowOriginal NOTIFY showOriginalChanged)
    Q_PROPERTY(bool hasImagePreview READ hasImagePreview NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

    Q_PROPERTY(int inputBlack READ inputBlack WRITE setInputBlack NOTIFY parametersChanged)
    Q_PROPERTY(int inputWhite READ inputWhite WRITE setInputWhite NOTIFY parametersChanged)
    Q_PROPERTY(int outputBlack READ outputBlack WRITE setOutputBlack NOTIFY parametersChanged)
    Q_PROPERTY(int outputWhite READ outputWhite WRITE setOutputWhite NOTIFY parametersChanged)
    Q_PROPERTY(double gamma READ gamma WRITE setGamma NOTIFY parametersChanged)
    Q_PROPERTY(int gammaHundredths READ gammaHundredths WRITE setGammaHundredths NOTIFY parametersChanged)
    Q_PROPERTY(int midpointPercent READ midpointPercent WRITE setMidpointPercent NOTIFY parametersChanged)
    Q_PROPERTY(QString gammaText READ gammaText NOTIFY parametersChanged)

public:
    explicit LevelsController(QObject* parent = nullptr);

    bool isActive() const { return m_target != Target::None; }
    QString targetKind() const;
    QString targetName() const { return m_targetName; }
    QString scopeText() const { return m_scopeText; }
    QVariantList histogram() const { return m_histogram; }
    bool showOriginal() const { return m_showOriginal; }
    void setShowOriginal(bool show);
    bool hasImagePreview() const { return m_target == Target::Image; }
    QString error() const { return m_error; }

    int inputBlack() const { return m_parameters.inputBlack; }
    int inputWhite() const { return m_parameters.inputWhite; }
    int outputBlack() const { return m_parameters.outputBlack; }
    int outputWhite() const { return m_parameters.outputWhite; }
    double gamma() const { return m_parameters.gamma; }
    int gammaHundredths() const;
    int midpointPercent() const;
    QString gammaText() const;

    void setInputBlack(int value);
    void setInputWhite(int value);
    void setOutputBlack(int value);
    void setOutputWhite(int value);
    void setGamma(double value);
    void setGammaHundredths(int value);
    void setMidpointPercent(int value);

    // Capture a document target. Never touches pixels until preview().
    bool beginDocument(CanvasDocument* document, Layer* layer, UndoStack* history);
    // Capture an in-memory RGBA PNG (base64, as returned by createBrushTipSource).
    bool beginImage(const QByteArray& png, const QString& name);
    Q_INVOKABLE bool beginImageBase64(const QString& pngBase64, const QString& name);

    Q_INVOKABLE void reset();
    Q_INVOKABLE void previewNow();
    Q_INVOKABLE bool apply();
    Q_INVOKABLE void cancel();

    // Image targets only. The preview item paints these directly.
    QImage beforeImage() const { return m_beforeImage; }
    QImage afterImage() const { return m_afterImage; }
    // Valid after apply() on an image target; empty otherwise.
    QString resultPngBase64() const { return m_resultPng; }
    QString resultName() const { return m_resultName; }

signals:
    void stateChanged();
    void parametersChanged();
    void histogramChanged();
    void previewChanged();
    void showOriginalChanged();
    void errorChanged();

private:
    enum class Target { None, Document, Image };

    struct TileOriginal {
        TilePoint tile;
        std::vector<std::uint32_t> pixels;
    };

    void setError(const QString& message);
    void clearError();
    void schedulePreview();
    void applyPreviewNow();
    void applyDocumentPreview();
    void rebuildImagePreview();
    void rebuildLut();
    void resetToDefaults();
    void finishSession();
    bool restoreDocumentOriginals();
    bool writeDocumentFromOriginals();
    std::uint8_t coverageAt(std::int64_t x, std::int64_t y) const;
    QVariantList buildHistogram(const adjustments::Histogram& histogram) const;
    void calculateDocumentHistogram();
    void calculateImageHistogram();
    QImage adjustedImage() const;

    Target m_target = Target::None;
    QPointer<CanvasDocument> m_document;
    QPointer<Layer> m_layer;
    QPointer<UndoStack> m_history;
    bool m_selectionActive = false;
    bool m_wasModified = false;
    QString m_targetName;
    QString m_scopeText;
    QString m_error;
    QString m_resultPng;
    QString m_resultName;

    std::vector<TileOriginal> m_tiles;
    QImage m_beforeImage;
    QImage m_afterImage;

    adjustments::LevelsParameters m_parameters;
    adjustments::LevelsLut m_lut;
    QVariantList m_histogram;
    bool m_showOriginal = false;
    bool m_lastWriteChanged = false;

    QTimer* m_previewTimer = nullptr;
};

}  // namespace komapaint
