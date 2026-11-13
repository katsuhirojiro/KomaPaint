#pragma once
#include "../canvas/SparseTileStore.h"
#include "HistoryTileCodec.h"
#include <QObject>
#include <QPointer>
#include <QString>
#include <memory>
#include <vector>

namespace komapaint {
class Layer;
// Stroke commands retain only touched tile states, never full canvases.
class UndoStack : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stackChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stackChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY stackChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY stackChanged)
    Q_PROPERTY(qulonglong memoryBytes READ memoryBytes NOTIFY stackChanged)
public:
    explicit UndoStack(QObject* parent = nullptr);
    ~UndoStack() override;
    bool canUndo() const { return !m_replaying && !m_current && m_cursor > 0; }
    bool canRedo() const { return !m_replaying && !m_current && m_cursor < m_commands.size(); }
    QString undoText() const;
    QString redoText() const;
    qulonglong memoryBytes() const { return m_bytes + m_currentBytes; }
    bool beginStroke(Layer* layer);
    bool commitStroke();
    bool cancelStroke();
    bool strokeActive() const { return bool(m_current); }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clear();
signals:
    void stackChanged();
    void errorOccurred(const QString& message);
    void strokeRolledBack();
private:
    struct CompactTile {
        TilePoint tile;
        history_codec::TileRecord record;
    };
    struct StrokeCommand {
        QPointer<Layer> layer;
        std::vector<SparseTileStore::TileSnapshot> before, after;
        std::vector<CompactTile> compact;
        std::size_t bytes = 0;
        std::shared_ptr<DocumentMemoryBudget> budget;
        std::size_t historyBytes = 0;
        std::size_t compactBytes = 0;
        ~StrokeCommand() { if (budget) budget->release(DocumentMemoryBudget::Category::History, historyBytes); }
    };
    static SparseTileStore::CaptureResult capture(void* context, TilePoint tile, bool existed, std::span<const std::uint32_t> pixels) noexcept;
    SparseTileStore::CaptureResult captureTile(TilePoint tile, bool existed, std::span<const std::uint32_t> pixels);
    void finishCapture();
    void tryCompact() noexcept;
    void replay(bool forward);
    void detachCapture() noexcept;
    void fail(const QString& message) { emit errorOccurred(message); }
    std::vector<std::unique_ptr<StrokeCommand>> m_commands;
    std::unique_ptr<StrokeCommand> m_current;
    bool m_replaying = false;
    QPointer<UndoStack> m_self;  // see constructor: keeps liveness guards allocation-free
    std::size_t m_cursor = 0, m_bytes = 0, m_currentBytes = 0;
};
} // namespace komapaint
