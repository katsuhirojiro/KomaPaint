#pragma once

#include <QColor>
#include "../layers/LayerModel.h"
#include <QObject>
#include <QSize>
#include <QString>

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
    Q_PROPERTY(bool    hasTransparentBackground READ hasTransparentBackground
               WRITE setTransparentBackground NOTIFY backgroundChanged)

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

public slots:
    void setBackgroundColor(const QColor& color);
    void setTitle(const QString& title);
    void setWidth(int w);
    void setHeight(int h);
    void setSize(const QSize& size);
    void setDpi(qreal dpi);
    void setTransparentBackground(bool t);

signals:
    void modifiedChanged();
    void titleChanged();
    void sizeChanged();
    void dpiChanged();
    void backgroundChanged();

private:
    void connectArtworkSignals();
    bool m_modified = false;
    LayerModel* m_layers;
    QColor m_backgroundColor = Qt::white;
    QString m_title = QStringLiteral("Untitled");
    QSize   m_size  = QSize(1600, 1200);
    qreal   m_dpi   = 300.0;
    bool    m_transparent = true;
    QString m_documentType = QString::fromLatin1(IllustrationTypeId);
};

}  // namespace komapaint
