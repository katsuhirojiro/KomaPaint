#pragma once
#include "BrushSettings.h"
#include "BrushStudioModel.h"
#include "CanvasViewport.h"
#include "canvas/CanvasDocument.h"
#include "history/UndoStack.h"

namespace komapaint {

// Same pointer, stroke and compositor path as a normal canvas, with no
// Workspace document or history. Nothing here is registered for recovery.
class BrushStudioPad : public CanvasViewport {
    Q_OBJECT
    Q_PROPERTY(BrushStudioModel* draft READ draft WRITE setDraft NOTIFY draftChanged)
    Q_PROPERTY(QString renderIssue READ renderIssue NOTIFY renderIssueChanged)
    Q_PROPERTY(bool canUndoPad READ canUndoPad NOTIFY padHistoryChanged)
public:
    explicit BrushStudioPad(QQuickItem* parent = nullptr);
    ~BrushStudioPad() override;
    BrushStudioModel* draft() const { return m_draft.data(); }
    void setDraft(BrushStudioModel* draft);
    QString renderIssue() const { return m_padBrush.nativeBrushIssue(); }
    bool canUndoPad() const { return m_padHistory.canUndo(); }
    Q_INVOKABLE void undoPad();
    Q_INVOKABLE bool clearPad();

signals:
    void draftChanged();
    void renderIssueChanged();
    void padHistoryChanged();
    void padNotice(const QString& message);

private:
    void syncDraft();
    CanvasDocument m_pad;
    UndoStack m_padHistory;
    BrushSettings m_padBrush;
    QPointer<BrushStudioModel> m_draft;
};

} // namespace komapaint
