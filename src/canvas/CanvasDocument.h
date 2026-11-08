#pragma once

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

namespace komapaint {

// CanvasDocument describes the editable surface that the user works on.
// Phase 1 does not implement painting; this class currently only carries
// the document's dimensions, background and (optional) title, so the UI
// has concrete metadata to bind against.

class CanvasDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title    READ title    WRITE setTitle    NOTIFY titleChanged)
    Q_PROPERTY(int     width    READ width    WRITE setWidth    NOTIFY sizeChanged)
    Q_PROPERTY(int     height   READ height   WRITE setHeight   NOTIFY sizeChanged)
    Q_PROPERTY(qreal   dpi      READ dpi      WRITE setDpi      NOTIFY dpiChanged)
    Q_PROPERTY(bool    hasTransparentBackground READ hasTransparentBackground
               WRITE setTransparentBackground NOTIFY backgroundChanged)

public:
    explicit CanvasDocument(QObject* parent = nullptr);

    static CanvasDocument* createDefault(QObject* parent = nullptr);

    QString title() const { return m_title; }
    int     width() const { return m_size.width(); }
    int     height() const { return m_size.height(); }
    QSize   size() const { return m_size; }
    qreal   dpi() const { return m_dpi; }
    bool    hasTransparentBackground() const { return m_transparent; }

public slots:
    void setTitle(const QString& title);
    void setWidth(int w);
    void setHeight(int h);
    void setSize(const QSize& size);
    void setDpi(qreal dpi);
    void setTransparentBackground(bool t);

signals:
    void titleChanged();
    void sizeChanged();
    void dpiChanged();
    void backgroundChanged();

private:
    QString m_title = QStringLiteral("Untitled");
    QSize   m_size  = QSize(1600, 1200);
    qreal   m_dpi   = 300.0;
    bool    m_transparent = true;
};

}  // namespace komapaint
