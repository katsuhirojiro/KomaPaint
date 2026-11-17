#pragma once

#include <QColor>
#include "../layers/LayerModel.h"
#include "../material/MaterialEngine.h"
#include "../region/SelectionMask.h"
#include <QObject>
#include <QRectF>
#include <QSize>
#include <QString>
#include <vector>

namespace komapaint {

// Owns document metadata and its layer model. Pixel storage arrives in milestone 2.2.
class CanvasDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool modified READ isModified NOTIFY modifiedChanged)
    Q_PROPERTY(LayerModel* layers READ layers CONSTANT)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundChanged)
    Q_PROPERTY(QString title    READ title    WRITE setTitle    NOTIFY titleChanged)
    Q_PROPERTY(int     width    READ width    WRITE setWidth    NOTIFY sizeChanged)
    Q_PROPERTY(int     height   READ height   WRITE setHeight   NOTIFY sizeChanged)
    Q_PROPERTY(qreal   dpi      READ dpi      WRITE setDpi      NOTIFY dpiChanged)
    Q_PROPERTY(bool    hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QRectF  selectionBounds READ selectionBounds NOTIFY selectionChanged)
    Q_PROPERTY(bool    hasTransparentBackground READ hasTransparentBackground
               WRITE setTransparentBackground NOTIFY backgroundChanged)
    Q_PROPERTY(QString surfaceId READ surfaceId WRITE setSurfaceId NOTIFY surfaceChanged)
    Q_PROPERTY(QString surfaceName READ surfaceName NOTIFY surfaceChanged)
    Q_PROPERTY(int surfaceAbsorbency READ surfaceAbsorbency WRITE setSurfaceAbsorbency NOTIFY surfaceChanged)
    Q_PROPERTY(int surfaceAbsorptionRate READ surfaceAbsorptionRate WRITE setSurfaceAbsorptionRate NOTIFY surfaceChanged)
    Q_PROPERTY(int surfacePorosity READ surfacePorosity WRITE setSurfacePorosity NOTIFY surfaceChanged)
    Q_PROPERTY(int surfaceWaterRetention READ surfaceWaterRetention WRITE setSurfaceWaterRetention NOTIFY surfaceChanged)
    Q_PROPERTY(int surfaceTooth READ surfaceTooth WRITE setSurfaceTooth NOTIFY surfaceChanged)
    Q_PROPERTY(int surfaceRoughness READ surfaceRoughness WRITE setSurfaceRoughness NOTIFY surfaceChanged)
    Q_PROPERTY(int surfacePigmentRetention READ surfacePigmentRetention WRITE setSurfacePigmentRetention NOTIFY surfaceChanged)
    Q_PROPERTY(int surfaceWetness READ surfaceWetness WRITE setSurfaceWetness NOTIFY surfaceChanged)
    Q_PROPERTY(int surfaceGranulation READ surfaceGranulation WRITE setSurfaceGranulation NOTIFY surfaceChanged)
    Q_PROPERTY(int surfaceRewetting READ surfaceRewetting WRITE setSurfaceRewetting NOTIFY surfaceChanged)

public:
    explicit CanvasDocument(QObject* parent = nullptr);

    static constexpr int MaxDimension = 32768;
    // Stable recovery type ids. The workspace uses these to associate a
    // recovery snapshot with the document type that wrote it. New native
    // document types set their own id instead of reusing the illustration id.
    static constexpr const char* IllustrationTypeId = "illustration";
    static bool validSize(const QSize& size);

    static CanvasDocument* createDefault(QObject* parent = nullptr);

    // Document type identity used by the shared recovery system. It is not
    // written into the .koma file, so the file format stays unchanged.
    QString documentType() const { return m_documentType; }
    void setDocumentType(const QString& type) { m_documentType = type; }

    bool isModified() const { return m_modified; }
    // Marks the document as unmodified (after saving) or modified. Not
    // exposed to QML; the UI must not be able to silently clear artwork state.
    void setModified(bool modified) { if (m_modified != modified) { m_modified = modified; emit modifiedChanged(); } }
    LayerModel* layers() const { return m_layers; }
    QColor backgroundColor() const { return m_backgroundColor; }
    QString title() const { return m_title; }
    int     width() const { return m_size.width(); }
    int     height() const { return m_size.height(); }
    QSize   size() const { return m_size; }
    qreal   dpi() const { return m_dpi; }
    bool    hasTransparentBackground() const { return m_transparent; }

    const material::SurfaceProfile& surfaceProfile() const noexcept { return m_surface; }
    void setSurfaceProfile(const material::SurfaceProfile& profile);
    bool hasCustomSurfaceSettings() const noexcept;
    QString surfaceId() const { return QString::fromStdString(m_surface.id); }
    QString surfaceName() const { return QString::fromStdString(m_surface.name); }
    int surfaceAbsorbency() const;
    int surfaceAbsorptionRate() const;
    int surfacePorosity() const;
    int surfaceWaterRetention() const;
    int surfaceTooth() const;
    int surfaceRoughness() const;
    int surfacePigmentRetention() const;
    int surfaceWetness() const;
    int surfaceGranulation() const;
    int surfaceRewetting() const;
    Q_INVOKABLE void resetSurfaceSettings();

    region::SelectionMask& selection() noexcept { return m_selection; }
    const region::SelectionMask& selection() const noexcept { return m_selection; }
    bool hasSelection() const noexcept { return m_selection.hasSelection(); }
    QRectF selectionBounds() const;
    void pushSelectionSnapshot();
    bool canUndoSelection() const noexcept { return !m_selectionUndo.empty(); }
    bool canRedoSelection() const noexcept { return !m_selectionRedo.empty(); }
    Q_INVOKABLE bool undoSelection();
    Q_INVOKABLE bool redoSelection();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deselect();
    Q_INVOKABLE void invertSelection();
    Q_INVOKABLE void expandSelection(int radiusPx);
    Q_INVOKABLE void contractSelection(int radiusPx);
    Q_INVOKABLE void featherSelection(int radiusPx);
    void notifySelectionChanged() { emit selectionChanged(); }

public slots:
    void setBackgroundColor(const QColor& color);
    void setTitle(const QString& title);
    void setWidth(int w);
    void setHeight(int h);
    void setSize(const QSize& size);
    void setDpi(qreal dpi);
    void setTransparentBackground(bool t);
    void setSurfaceId(const QString& id);
    void setSurfaceAbsorbency(int value);
    void setSurfaceAbsorptionRate(int value);
    void setSurfacePorosity(int value);
    void setSurfaceWaterRetention(int value);
    void setSurfaceTooth(int value);
    void setSurfaceRoughness(int value);
    void setSurfacePigmentRetention(int value);
    void setSurfaceWetness(int value);
    void setSurfaceGranulation(int value);
    void setSurfaceRewetting(int value);

signals:
    void modifiedChanged();
    void titleChanged();
    void sizeChanged();
    void dpiChanged();
    void backgroundChanged();
    void surfaceChanged();
    void selectionChanged();

private:
    void connectArtworkSignals();
    bool m_modified = false;
    LayerModel* m_layers;
    region::SelectionMask m_selection{1600, 1200};
    std::vector<region::SelectionMask::Snapshot> m_selectionUndo;
    std::vector<region::SelectionMask::Snapshot> m_selectionRedo;
    QColor m_backgroundColor = Qt::white;
    QString m_title = QStringLiteral("Untitled");
    QSize   m_size  = QSize(1600, 1200);
    qreal   m_dpi   = 300.0;
    bool    m_transparent = true;
    QString m_documentType = QString::fromLatin1(IllustrationTypeId);
    material::SurfaceProfile m_surface = material::coldPressedPaper();
};

}  // namespace komapaint
