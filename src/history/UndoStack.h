#pragma once

#include <QObject>
#include <QString>

namespace komapaint {

// Minimal undo/redo command description. Full command pattern arrives in a
// later phase. The shell currently only needs a "can undo/redo" signal to
// drive toolbar button enabled state and a text label for menu entries.

struct Command {
    QString description;
};

class UndoStack : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool    canUndo       READ canUndo       NOTIFY canUndoChanged)
    Q_PROPERTY(bool    canRedo       READ canRedo       NOTIFY canRedoChanged)
    Q_PROPERTY(QString undoText      READ undoText      NOTIFY stackChanged)
    Q_PROPERTY(QString redoText      READ redoText      NOTIFY stackChanged)
    Q_PROPERTY(int     undoLimit     READ undoLimit     WRITE setUndoLimit NOTIFY limitChanged)

public:
    explicit UndoStack(QObject* parent = nullptr);

    bool    canUndo()   const { return m_undoCursor > 0; }
    bool    canRedo()   const { return m_undoCursor < m_commands.size(); }
    QString undoText()  const;
    QString redoText()  const;
    int     undoLimit() const { return m_limit; }

    Q_INVOKABLE void push(const QString& description);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clear();

public slots:
    void setUndoLimit(int limit);

signals:
    void canUndoChanged();
    void canRedoChanged();
    void stackChanged();
    void limitChanged();

private:
    void prune();
    void notifyAll();

    QList<Command> m_commands;
    int            m_undoCursor = 0;
    int            m_limit      = 200;
};

}  // namespace komapaint
