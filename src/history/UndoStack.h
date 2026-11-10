#pragma once
#include "../canvas/SparseTileStore.h"
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
    bool canUndo() const { return !m_current && m_cursor > 0; }
    bool canRedo() const { return !m_current && m_cursor < m_commands.size(); }
    QString undoText() const;
    QString redoText() const;
    qulonglong memoryBytes() const { return m_bytes + m_currentBytes; }
    std::size_t byteLimit() const { return m_limit; }
    // Set only while history is empty, for predictable accounting and tests.
    bool setByteLimit(std::size_t bytes);
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
    struct StrokeCommand {
        QPointer<Layer> layer;
        std::vector<SparseTileStore::TileSnapshot> before, after;
        std::size_t bytes = 0;
        std::shared_ptr<DocumentMemoryBudget> budget;
        std::size_t historyBytes = 0;
        ~StrokeCommand() { if (budget) budget->release(DocumentMemoryBudget::Category::History, historyBytes); }
    };
    static SparseTileStore::CaptureResult capture(void* context, TilePoint tile, bool existed, std::span<const std::uint32_t> pixels) noexcept;
    SparseTileStore::CaptureResult captureTile(TilePoint tile, bool existed, std::span<const std::uint32_t> pixels);
    void finishCapture();
    void detachCapture() noexcept;
    void fail(const QString& message) { emit errorOccurred(message); }
    std::vector<std::unique_ptr<StrokeCommand>> m_commands;
    std::unique_ptr<StrokeCommand> m_current;
    std::size_t m_cursor = 0, m_bytes = 0, m_currentBytes = 0;
    std::size_t m_limit = 64U * 1024U * 1024U;
};
} // namespace komapaint
