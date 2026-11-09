#include <QtTest/QtTest>

#include "history/UndoStack.h"

using namespace komapaint;

class TestUndoStack : public QObject
{
    Q_OBJECT
private slots:
    void startsEmpty();
    void pushEnablesUndo();
    void undoRedoCycle();
    void newCommandTruncatesRedo();
    void clearResets();
};

void TestUndoStack::startsEmpty()
{
    UndoStack stack;
    QVERIFY(!stack.canUndo());
    QVERIFY(!stack.canRedo());
    QCOMPARE(stack.undoText(), QString());
    QCOMPARE(stack.redoText(), QString());
}

void TestUndoStack::pushEnablesUndo()
{
    UndoStack stack;
    stack.push("Draw stroke");
    QVERIFY(stack.canUndo());
    QCOMPARE(stack.undoText(), QString("Draw stroke"));
}

void TestUndoStack::undoRedoCycle()
{
    UndoStack stack;
    stack.push("A");
    stack.push("B");
    QCOMPARE(stack.undoText(), QString("B"));
    stack.undo();
    QCOMPARE(stack.undoText(), QString("A"));
    QCOMPARE(stack.redoText(), QString("B"));
    QVERIFY(stack.canRedo());
    stack.redo();
    QCOMPARE(stack.undoText(), QString("B"));
    QVERIFY(!stack.canRedo());
}

void TestUndoStack::newCommandTruncatesRedo()
{
    UndoStack stack;
    stack.push("A"); stack.push("B"); stack.undo();
    QVERIFY(stack.canRedo());
    stack.push("C");
    QVERIFY(!stack.canRedo());
    QCOMPARE(stack.undoText(), QString("C"));
}

void TestUndoStack::clearResets()
{
    UndoStack stack;
    stack.push("A"); stack.push("B");
    stack.clear();
    QVERIFY(!stack.canUndo());
    QVERIFY(!stack.canRedo());
}

QTEST_APPLESS_MAIN(TestUndoStack)
#include "TestUndoStack.moc"
