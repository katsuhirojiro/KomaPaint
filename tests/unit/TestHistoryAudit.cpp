#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <cstdio>
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif
#include "history/UndoStack.h"
#include "layers/Layer.h"
#include "drawing/BrushEngine.h"

using namespace komapaint;
#if defined(Q_OS_WIN)
struct ProcessMemory { qulonglong privateBytes = 0, workingBytes = 0, peakWorkingBytes = 0; };
static ProcessMemory processMemory()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) return {};
    return {counters.PrivateUsage, counters.WorkingSetSize, counters.PeakWorkingSetSize};
}
#endif

class TestHistoryAudit : public QObject {
    Q_OBJECT
private slots:
    void oneTileHistoryBudget();
};

void TestHistoryAudit::oneTileHistoryBudget()
{
    Layer layer;
    QVERIFY(layer.tiles().resizeEmpty(3000, 2000));
    UndoStack history;
    QCOMPARE(layer.tiles().tileBytes(), std::size_t(262144));
#if defined(Q_OS_WIN)
    const auto baseline = processMemory();
    ProcessMemory first{};
#endif
    QElapsedTimer timer;
    timer.start();
    int accepted = 0;
    for (int i = 0; i < 128; ++i) {
        QVERIFY(history.beginStroke(&layer));
        BrushDefinition def;
        def.size = 4;
        def.color = i % 2 ? 0xffff0000U : 0xff000000U;
        BrushStroke stroke(layer.tiles(), def);
        if (!stroke.begin({64, 64, 1})) {
            QCOMPARE(i, 127);
            QCOMPARE(layer.tiles().setPixel({64, 64}, 0xffff0000U), SparseTileStore::WriteResult::HistoryLimit);
            QVERIFY(history.commitStroke());
            break;
        }
        QVERIFY(stroke.end({64, 64, 1}));
        QVERIFY(history.commitStroke());
        ++accepted;
        if (i == 0) {
            QCOMPARE(history.memoryBytes(), qulonglong(524800));
#if defined(Q_OS_WIN)
            first = processMemory();
#endif
        }
    }
    QCOMPARE(accepted, 127);
    QCOMPARE(history.memoryBytes(), qulonglong(66649600));
    QCOMPARE(layer.tiles().allocatedPixelBytes(), std::size_t(262144));
#if defined(Q_OS_WIN)
    const auto filled = processMemory();
    QVERIFY(baseline.privateBytes && first.privateBytes && filled.privateBytes);
#endif
    const auto finalPixel = layer.tiles().pixel({64, 64}).value();
    for (int i = 0; i < accepted; ++i) { QVERIFY(history.canUndo()); history.undo(); }
    QVERIFY(!history.canUndo());
    QCOMPARE(layer.tiles().allocatedTiles(), std::size_t(0));
    for (int i = 0; i < accepted; ++i) { QVERIFY(history.canRedo()); history.redo(); }
    QVERIFY(!history.canRedo());
    QCOMPARE(layer.tiles().pixel({64, 64}).value(), finalPixel);
    std::printf("::notice title=History audit::127 one-tile brush strokes accepted; stroke 128 returned HistoryLimit; charged history=66649600 bytes; artwork pixels=262144 bytes; 127 undo and redo operations succeeded; elapsed=%lld ms\n", static_cast<long long>(timer.elapsed()));
#if defined(Q_OS_WIN)
    const auto replayed = processMemory();
    std::printf("::notice title=History process memory::Windows private committed bytes baseline=%llu first=%llu after127=%llu afterUndoRedo=%llu; working set after127=%llu; peak working set afterUndoRedo=%llu; headless brush and history only, no viewport cache or file I/O\n",
                baseline.privateBytes, first.privateBytes, filled.privateBytes, replayed.privateBytes,
                filled.workingBytes, replayed.peakWorkingBytes);
#endif
    std::fflush(stdout);
}
QTEST_APPLESS_MAIN(TestHistoryAudit)
#include "TestHistoryAudit.moc"
