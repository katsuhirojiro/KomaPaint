#include "UndoStack.h"

namespace komapaint {

UndoStack::UndoStack(QObject* parent) : QObject(parent) {}

QString UndoStack::undoText() const
{
    if (!canUndo()) return {};
    return m_commands[m_undoCursor - 1].description;
}
QString UndoStack::redoText() const
{
    if (!canRedo()) return {};
    return m_commands[m_undoCursor].description;
}

void UndoStack::prune()
{
    while (m_commands.size() > m_limit) {
        m_commands.removeFirst();
        --m_undoCursor;
    }
}

void UndoStack::notifyAll()
{
    emit canUndoChanged();
    emit canRedoChanged();
    emit stackChanged();
}

void UndoStack::push(const QString& description)
{
    // Drop any redone state when a new command arrives.
    if (m_undoCursor < m_commands.size()) {
        m_commands.erase(m_commands.begin() + m_undoCursor, m_commands.end());
    }
    m_commands.append({ description });
    ++m_undoCursor;
    prune();
    notifyAll();
}

void UndoStack::undo()
{
    if (!canUndo()) return;
    --m_undoCursor;
    notifyAll();
}

void UndoStack::redo()
{
    if (!canRedo()) return;
    ++m_undoCursor;
    notifyAll();
}

void UndoStack::clear()
{
    if (m_commands.isEmpty()) return;
    m_commands.clear();
    m_undoCursor = 0;
    notifyAll();
}

void UndoStack::setUndoLimit(int limit)
{
    limit = qBound(1, limit, 10000);
    if (limit == m_limit) return;
    m_limit = limit;
    prune();
    emit limitChanged();
    notifyAll();
}

}  // namespace komapaint
