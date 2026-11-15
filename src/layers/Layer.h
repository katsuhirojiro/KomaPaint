#pragma once

#include "../canvas/SparseTileStore.h"
#include <QObject>
#include <QString>
#include <QColor>
#include <QVariantList>

namespace komapaint {

// A raster layer with document-persisted appearance and label metadata.

class Layer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name        READ name        WRITE setName        NOTIFY nameChanged)
    Q_PROPERTY(bool    visible     READ isVisible   WRITE setVisible     NOTIFY visibilityChanged)
    Q_PROPERTY(bool    locked      READ isLocked    WRITE setLocked      NOTIFY lockedChanged)
    Q_PROPERTY(qreal   opacity     READ opacity     WRITE setOpacity     NOTIFY opacityChanged)
    Q_PROPERTY(int     blendMode   READ blendMode   WRITE setBlendMode   NOTIFY blendModeChanged)
    Q_PROPERTY(int     colorLabel  READ colorLabel  WRITE setColorLabel  NOTIFY colorLabelChanged)
    Q_PROPERTY(int     index       READ index       WRITE setIndex       NOTIFY indexChanged)

public:
    explicit Layer(QObject* parent = nullptr);
    explicit Layer(const QString& name, QObject* parent = nullptr);

    SparseTileStore& tiles() { return m_tiles; }
    const SparseTileStore& tiles() const { return m_tiles; }

    QString name() const { return m_name; }
    bool    isVisible() const { return m_visible; }
    bool    isLocked()  const { return m_locked; }
    qreal   opacity()   const { return m_opacity; }
    int     blendMode() const { return m_blendMode; }
    int     colorLabel() const { return m_colorLabel; }
    int     index()     const { return m_index; }

    // Document-fit pixels for a small, transparent per-layer UI thumbnail.
    // Computed synchronously on the owning thread; no tile span escapes.
    Q_INVOKABLE QVariantList thumbnailPixels(int side = 32) const;

public slots:
    void setName(const QString& name);
    void setVisible(bool visible);
    void setLocked(bool locked);
    void setOpacity(qreal opacity);
    void setBlendMode(int mode);
    void setColorLabel(int label);
    void setIndex(int index);

signals:
    void artworkChanged(qint64 tileX, qint64 tileY);
    void nameChanged();
    void visibilityChanged();
    void lockedChanged();
    void opacityChanged();
    void blendModeChanged();
    void colorLabelChanged();
    void indexChanged();

private:
    SparseTileStore m_tiles{1600, 1200};
    QString m_name    = QStringLiteral("Layer");
    bool    m_visible = true;
    bool    m_locked  = false;
    qreal   m_opacity = 1.0;
    int     m_blendMode = 0;
    int     m_colorLabel = 0;
    int     m_index   = 0;
};

}  // namespace komapaint
