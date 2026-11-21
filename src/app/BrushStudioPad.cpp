#include "BrushStudioPad.h"

#include <new>
#include <stdexcept>

namespace komapaint {

BrushStudioPad::BrushStudioPad(QQuickItem* parent) : CanvasViewport(parent)
{
    // Stable document coordinates across shelf collapse and window resize.
    // Sparse storage: this does not allocate pixels outside the painted area.
    m_pad.setSize(QSize(4096, 2048));
    m_pad.setTransparentBackground(true);
    setDocument(&m_pad);
    setHistory(&m_padHistory);
    setBrushSettings(&m_padBrush);
    setActiveTool(QStringLiteral("brush"));
    setBrushColor(Qt::black);
    connect(&m_padHistory, &UndoStack::stackChanged, this, &BrushStudioPad::padHistoryChanged);
    connect(&m_padHistory, &UndoStack::errorOccurred, this, &BrushStudioPad::padNotice);
    connect(&m_padHistory, &UndoStack::historyTrimmed, this, [this](int discarded) {
        emit padNotice(tr("The pad dropped %n older undo step(s) to free memory.", nullptr, discarded));
    });
    syncDraft();
}

BrushStudioPad::~BrushStudioPad()
{
    // Disconnect the base from members before they are destroyed.
    setHistory(nullptr);
    setBrushSettings(nullptr);
    setDocument(nullptr);
}

void BrushStudioPad::setDraft(BrushStudioModel* draft)
{
    if (m_draft == draft) return;
    if (m_draft) disconnect(m_draft, nullptr, this, nullptr);
    m_draft = draft;
    if (m_draft) {
        connect(m_draft, &BrushStudioModel::documentChanged, this, &BrushStudioPad::syncDraft);
        connect(m_draft, &QObject::destroyed, this, [this] {
            m_draft = nullptr;
            syncDraft();
            emit draftChanged();
        });
    }
    syncDraft();
    emit draftChanged();
}

void BrushStudioPad::syncDraft()
{
    QString issue;
    std::shared_ptr<const brush::BrushDocument> snapshot;
    try {
        if (!m_draft) {
            issue = tr("There is no draft to preview.");
        } else {
            const auto& brush = m_draft->document();
            issue = QString::fromStdString(drawing::digitalBrushIssue(brush));
            // Copy the authored metadata, not its immutable resource pixels.
            if (issue.isEmpty()) snapshot = std::make_shared<const brush::BrushDocument>(brush);
        }
    } catch (const std::bad_alloc&) {
        issue = tr("Not enough memory to preview this brush.");
    } catch (const std::length_error&) {
        issue = tr("This brush is too large to preview on this system.");
    }
    const bool notify = issue != m_padBrush.nativeBrushIssue();
    m_padBrush.setNativeBrush(std::move(snapshot), issue);
    if (notify) emit renderIssueChanged();
}

void BrushStudioPad::undoPad()
{
    m_padHistory.undo();
}

bool BrushStudioPad::clearPad() try
{
    cancelStroke();
    auto& tiles = m_pad.layers()->layerAt(0)->tiles();
    std::vector<SparseTileStore::TileSnapshot> cleared;
    for (const auto point : tiles.allocatedTilePoints()) cleared.push_back({point, {}});
    if (!cleared.empty() && tiles.restoreTiles(cleared) != SparseTileStore::WriteResult::Changed) {
        emit padNotice(tr("The pad could not be cleared. Its strokes were kept."));
        return false;
    }
    m_padHistory.clear();
    return true;
} catch (const std::bad_alloc&) {
    emit padNotice(tr("Not enough memory to clear the pad. Its strokes were kept."));
    return false;
} catch (const std::length_error&) {
    emit padNotice(tr("The pad could not be cleared. Its strokes were kept."));
    return false;
}

} // namespace komapaint
