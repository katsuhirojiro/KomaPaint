#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <cstdio>
#include <limits>
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
    void fullSnapshotReference();
    void oneTileHistoryBudget();
};

// Force the old fully admitted snapshot fallback for an equivalent workload.
void TestHistoryAudit::fullSnapshotReference()
{
    Layer layer; UndoStack history;
    auto budget = std::make_shared<DocumentMemoryBudget>();
    QVERIFY(layer.tiles().setDocumentBudget(budget));
#if defined(Q_OS_WIN)
    const auto baseline = processMemory();
#endif
    QElapsedTimer timer; timer.start();
    qulonglong reference127 = 0;
    for (int i = 0; i < 130; ++i) {
        QVERIFY(history.beginStroke(&layer));
        QCOMPARE(layer.tiles().setPixel({64, 64}, i % 2 ? 0x80402010U : 0xff123456U), SparseTileStore::WriteResult::Changed);
        // No space to reserve a second representation: optional compaction
        // must fall back to the already-admitted full snapshot.
        QVERIFY(budget->setLimit(budget->usedBytes()));
        QVERIFY(history.commitStroke());
        QVERIFY(budget->setLimit(std::numeric_limits<std::size_t>::max()));
        if (i == 126) reference127 = history.memoryBytes();
    }
    const auto paintMs = timer.elapsed();
    QCOMPARE(reference127, qulonglong(66649600));
    QVERIFY(history.memoryBytes() > qulonglong(64U * 1024U * 1024U));
#if defined(Q_OS_WIN)
    const auto filled = processMemory();
#endif
    timer.restart(); for (int i = 0; i < 130; ++i) history.undo();
    const auto undoMs = timer.elapsed();
    timer.restart(); for (int i = 0; i < 130; ++i) history.redo();
    const auto redoMs = timer.elapsed();
    QCOMPARE(layer.tiles().pixel({64, 64}).value(), 0x80402010U);
    std::printf("::notice title=Full-snapshot reference::130 one-pixel strokes (beyond 64 MiB); charge=%llu bytes tracked=%zu bytes paint=%lld ms undo=%lld ms redo=%lld ms\n",
                history.memoryBytes(), budget->historyBytes(), static_cast<long long>(paintMs), static_cast<long long>(undoMs), static_cast<long long>(redoMs));
#if defined(Q_OS_WIN)
    std::printf("::notice title=Full-snapshot process memory::private baseline=%llu after130=%llu peak working=%llu; same process is reused for compact test\n", baseline.privateBytes, filled.privateBytes, filled.peakWorkingBytes);
#endif
    std::fflush(stdout);
}

void TestHistoryAudit::oneTileHistoryBudget()
{
    Layer layer; UndoStack history;
    auto budget = std::make_shared<DocumentMemoryBudget>();
    QVERIFY(layer.tiles().setDocumentBudget(budget));
    const auto tileBytes = layer.tiles().tileBytes();
    QCOMPARE(tileBytes, std::size_t(262144));
#if defined(Q_OS_WIN)
    const auto baseline = processMemory();
    ProcessMemory after127{};
#endif
    QElapsedTimer timer; timer.start();
    qulonglong compactCharge127 = 0;
    std::size_t compactTracked127 = 0;
    qint64 compactPaint127 = 0, compactUndo127 = 0, compactRedo127 = 0;
    constexpr int count = 10000;
    for (int i = 0; i < count; ++i) {
        QVERIFY(history.beginStroke(&layer));
        const auto color = i % 2 ? 0x80402010U : 0xff123456U;
        QCOMPARE(layer.tiles().setPixel({64, 64}, color), SparseTileStore::WriteResult::Changed);
        QVERIFY(history.commitStroke());
        if (i == 126) {
            compactCharge127 = history.memoryBytes();
            compactTracked127 = budget->historyBytes();
            compactPaint127 = timer.elapsed();
#if defined(Q_OS_WIN)
            after127 = processMemory();
#endif
            QElapsedTimer comparison; comparison.start();
            for (int j = 0; j < 127; ++j) history.undo();
            compactUndo127 = comparison.elapsed();
            comparison.restart();
            for (int j = 0; j < 127; ++j) history.redo();
            compactRedo127 = comparison.elapsed();
            QCOMPARE(layer.tiles().pixel({64, 64}).value(), 0xff123456U);
        }
    }
    const auto paintMs = timer.elapsed() - compactUndo127 - compactRedo127;
    const auto historyCharge = history.memoryBytes();
    const auto tracked = budget->historyBytes();
    QVERIFY(tracked < std::size_t(count) * tileBytes);
    const auto painted = layer.tiles().pixel({64, 64}).value();
#if defined(Q_OS_WIN)
    const auto filled = processMemory();
    QVERIFY(baseline.privateBytes && after127.privateBytes && filled.privateBytes);
#endif
    timer.restart();
    for (int i = 0; i < count; ++i) { QVERIFY(history.canUndo()); history.undo(); }
    const auto undoMs = timer.elapsed();
    QVERIFY(!history.canUndo());
    QCOMPARE(layer.tiles().allocatedTiles(), std::size_t(0));
    timer.restart();
    for (int i = 0; i < count; ++i) { QVERIFY(history.canRedo()); history.redo(); }
    const auto redoMs = timer.elapsed();
    QVERIFY(!history.canRedo());
    QCOMPARE(layer.tiles().pixel({64, 64}).value(), painted);
    std::printf("::notice title=Compact 127-stroke comparison::127 one-pixel strokes; charge=%llu bytes tracked=%zu bytes paint=%lld ms undo=%lld ms redo=%lld ms; same pixel workload as full-snapshot reference (127 versus 130 strokes)\n", compactCharge127, compactTracked127, static_cast<long long>(compactPaint127), static_cast<long long>(compactUndo127), static_cast<long long>(compactRedo127));
    std::printf("::notice title=Production compact history::10000 one-pixel strokes; estimated retained history charge=%llu bytes; tracked compact capacity=%zu bytes; paint=%lld ms undo=%lld ms redo=%lld ms; full history retained\n",
                historyCharge, tracked, static_cast<long long>(paintMs), static_cast<long long>(undoMs), static_cast<long long>(redoMs));
#if defined(Q_OS_WIN)
    const auto replayed = processMemory();
    std::printf("::notice title=Production compact process memory::Windows private baseline=%llu after127=%llu after10000=%llu afterReplay=%llu; peak working afterReplay=%llu; headless one-pixel strokes, no viewport cache or file I/O\n",
                baseline.privateBytes, after127.privateBytes, filled.privateBytes, replayed.privateBytes, replayed.peakWorkingBytes);
#endif
    std::fflush(stdout);
}
QTEST_APPLESS_MAIN(TestHistoryAudit)
#include "TestHistoryAudit.moc"
