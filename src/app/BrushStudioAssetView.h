#pragma once

#include "BrushStudioModel.h"

#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>

#include <optional>

namespace komapaint {

// Read-only display of a selected stored resource. This neither evaluates a
// brush nor edits a resource; tiling only repeats the raw texture sample.
class BrushStudioAssetView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(BrushStudioModel* draft READ draft WRITE setDraft NOTIFY draftChanged)
    Q_PROPERTY(bool tiled MEMBER m_tiled NOTIFY viewChanged)
    Q_PROPERTY(qreal magnification MEMBER m_magnification NOTIFY viewChanged)
    Q_PROPERTY(QColor checkerLight MEMBER m_checkerLight NOTIFY viewChanged)
    Q_PROPERTY(QColor checkerDark MEMBER m_checkerDark NOTIFY viewChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
public:
    explicit BrushStudioAssetView(QQuickItem* parent = nullptr);
    BrushStudioModel* draft() const { return m_draft; }
    void setDraft(BrushStudioModel* model);
    bool hasImage() const { return !m_image.isNull(); }
    void paint(QPainter* painter) override;

signals:
    void draftChanged();
    void viewChanged();
    void imageChanged();

private:
    void refreshImage();
    QPointer<BrushStudioModel> m_draft;
    // Shares only the selected immutable pixel buffer, so we can detect
    // replacements without holding a full image copy or touching the renderer.
    std::optional<brush::BrushResource> m_source;
    QImage m_image;
    bool m_tiled = false;
    qreal m_magnification = 1;
    QColor m_checkerLight = Qt::lightGray;
    QColor m_checkerDark = Qt::gray;
};

} // namespace komapaint
