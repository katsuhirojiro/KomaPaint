#include <QtTest/QtTest>
#include "app/ToolCycle.h"

using namespace komapaint;

// Clip Studio Paint-style tool families: one key, several tools, repeated
// presses cycle through them. These checks are data-only so they run without a
// window, a document or an event loop.
class TestToolCycle : public QObject {
    Q_OBJECT
private slots:
    void brushKeyCyclesBrushAndAirbrush();
    void figureKeyCyclesShapeTools();
    void selectionKeyCyclesMarqueeTools();
    void unrelatedToolEntersFamilyAtItsFirstMember();
    void penKeyCyclesPenAndPencil();
    void fillFamilyWalksSubTools();
    void keysWithoutFamilyStaySingle();
};

void TestToolCycle::brushKeyCyclesBrushAndAirbrush()
{
    const QStringList group = toolcycle::groupForAction(QStringLiteral("tool.brush"));
    QCOMPARE(group, QStringList({ QStringLiteral("brush"), QStringLiteral("airbrush") }));
    QCOMPARE(toolcycle::nextTool(group, QStringLiteral("brush")), QStringLiteral("airbrush"));
    QCOMPARE(toolcycle::nextTool(group, QStringLiteral("airbrush")), QStringLiteral("brush"));
}

void TestToolCycle::figureKeyCyclesShapeTools()
{
    const QStringList group = toolcycle::groupForAction(QStringLiteral("tool.rectangle"));
    QCOMPARE(group, QStringList({ QStringLiteral("rectangle"), QStringLiteral("circle"),
                                  QStringLiteral("line"), QStringLiteral("curveline") }));
    QCOMPARE(toolcycle::nextTool(group, QStringLiteral("rectangle")), QStringLiteral("circle"));
    QCOMPARE(toolcycle::nextTool(group, QStringLiteral("curveline")), QStringLiteral("rectangle"));
}

void TestToolCycle::selectionKeyCyclesMarqueeTools()
{
    const QStringList group = toolcycle::groupForAction(QStringLiteral("tool.selection"));
    QCOMPARE(group, QStringList({ QStringLiteral("selection"), QStringLiteral("lasso"),
                                  QStringLiteral("wand") }));
    QCOMPARE(toolcycle::nextTool(group, QStringLiteral("lasso")), QStringLiteral("wand"));
    QCOMPARE(toolcycle::nextTool(group, QStringLiteral("wand")), QStringLiteral("selection"));
}

void TestToolCycle::unrelatedToolEntersFamilyAtItsFirstMember()
{
    const QStringList group = toolcycle::groupForAction(QStringLiteral("tool.brush"));
    // Pressing B while holding the eraser selects the brush, not the sibling the
    // artist used last.
    QCOMPARE(toolcycle::nextTool(group, QStringLiteral("eraser")), QStringLiteral("brush"));
    QCOMPARE(toolcycle::nextTool(group, QString()), QStringLiteral("brush"));
}

void TestToolCycle::penKeyCyclesPenAndPencil()
{
    const QStringList group = toolcycle::groupForAction(QStringLiteral("tool.pen"));
    QCOMPARE(group, QStringList({ QStringLiteral("pen"), QStringLiteral("pencil") }));
    QCOMPARE(toolcycle::nextTool(group, QStringLiteral("pen")), QStringLiteral("pencil"));
    QCOMPARE(toolcycle::nextTool(group, QStringLiteral("pencil")), QStringLiteral("pen"));
    QCOMPARE(toolcycle::nextTool(group, QStringLiteral("brush")), QStringLiteral("pen"));
}

void TestToolCycle::fillFamilyWalksSubTools()
{
    // Fill is one palette tool whose family lives in its presets, so the caller
    // walks sub-tools rather than switching tools.
    QCOMPARE(toolcycle::groupForAction(QStringLiteral("tool.fill")),
             QStringList({ QStringLiteral("fill") }));
    QCOMPARE(toolcycle::executablesForAction(QStringLiteral("tool.fill")),
             QStringList({ QStringLiteral("fill"), QStringLiteral("smart_fill") }));
    // Everything else keeps tool-level cycling.
    QCOMPARE(toolcycle::executablesForAction(QStringLiteral("tool.brush")),
             QStringList({ QStringLiteral("brush"), QStringLiteral("airbrush") }));
}

void TestToolCycle::keysWithoutFamilyStaySingle()
{
    QVERIFY(toolcycle::groupForAction(QStringLiteral("tool.eraser")).isEmpty());
    QVERIFY(toolcycle::groupForAction(QStringLiteral("tool.pencil")).isEmpty());
    QVERIFY(toolcycle::groupForAction(QStringLiteral("tool.zoom")).isEmpty());
    QVERIFY(toolcycle::nextTool({}, QStringLiteral("brush")).isEmpty());
}

QTEST_APPLESS_MAIN(TestToolCycle)
#include "TestToolCycle.moc"
