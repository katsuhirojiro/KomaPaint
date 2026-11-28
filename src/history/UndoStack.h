#pragma once
#include "../canvas/SparseTileStore.h"
#include "../material/MaterialTileStore.h"
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
    // label is shown by undoText/redoText; empty keeps the historical
    // "Brush stroke" wording. Pixel edits (adjustments) use the same
    // tile-snapshot transaction as a stroke.
    bool beginStroke(Layer* layer, const QString& label = {});
    bool beginMaterialStroke(Layer* layer);
    bool commitStroke();
    bool cancelStroke();
    bool strokeActive() const { return bool(m_current); }
    void setMaxCommands(int count);
    int maxCommands() const { return static_cast<int>(m_maxCommands); }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clear();
signals:
    void stackChanged();
    void errorOccurred(const QString& message);
    void strokeRolledBack();
    // Emitted once per stroke when old history had to be dropped so that new
    // artwork could still be recorded. Memory pressure, never a fixed quota.
    void historyTrimmed(int discardedCommands);
private:
    struct CompactTile {
        TilePoint tile;
        history_codec::TileRecord record;
    };
    struct StrokeCommand {
        QPointer<Layer> layer;
        std::vector<SparseTileStore::TileSnapshot> before, after;
        std::vector<material::MaterialTileSnapshot> materialBefore, materialAfter;
        std::vector<std::size_t> materialBeforeCharges;
        bool materialStroke = false;
        QString label;
        std::vector<CompactTile> compact;
        std::size_t bytes = 0;
        std::shared_ptr<DocumentMemoryBudget> budget;
        std::size_t historyBytes = 0;
        std::size_t compactBytes = 0;
        ~StrokeCommand();
    };
    static SparseTileStore::CaptureResult capture(void* context, TilePoint tile, bool existed, std::span<const std::uint32_t> pixels) noexcept;
    SparseTileStore::CaptureResult captureTile(TilePoint tile, bool existed, std::span<const std::uint32_t> pixels);
    static bool captureMaterial(void* context, TilePoint tile, std::span<const material::MaterialEntry> entries) noexcept;
    bool captureMaterialTile(TilePoint tile, std::span<const material::MaterialEntry> entries);
    void finishCapture();
    void finishMaterialCapture();
    void tryCompact() noexcept;
    // Admits a history charge, discarding the oldest committed commands when
    // the system is out of memory rather than refusing the new artwork.
    bool admitHistory(DocumentMemoryBudget* budget, std::size_t bytes) noexcept;
    void flushTrimNotice();
    void replay(bool forward);
    void detachCapture() noexcept;
    void fail(const QString& message) { emit errorOccurred(message); }
    std::vector<std::unique_ptr<StrokeCommand>> m_commands;
    std::unique_ptr<StrokeCommand> m_current;
    bool m_replaying = false;
    QPointer<UndoStack> m_self;  // see constructor: keeps liveness guards allocation-free
    TilePoint m_lastCapturedMaterialTile{};
    bool m_hasLastCapturedMaterialTile = false;
    std::size_t m_cursor = 0, m_bytes = 0, m_currentBytes = 0;
    std::size_t m_maxCommands = 0; // 0 = unlimited
    // Old commands dropped during the current stroke because memory ran out.
    // Reported once, after the stroke finishes, so painting is never stalled by
    // a dialog in the middle of a line.
    std::size_t m_pendingDiscarded = 0;
};
} // namespace komapaint
