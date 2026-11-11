#include <QtTest/QtTest>
#include <QFile>
#include <QTemporaryDir>
#include "history/HistoryJournal.h"
using namespace komapaint;

class TestHistoryJournal : public QObject {
    Q_OBJECT
private slots:
    void roundTripAndIdentity();
    void interruptedTail();
    void truncatedFrames();
    void rejectedOperationsPreserveOutputs();
    void corruptRecord();
    void invalidPathAndExistingFile();
};
void TestHistoryJournal::roundTripAndIdentity()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("history.bin"));
    const auto id = QUuid::createUuid(); QString error;
    const QByteArray first("A\0B\xff", 4), second(512 * 1024, 'q');
    qint64 a = -1, b = -1;
    {
        HistoryJournal journal;
        QVERIFY2(journal.create(path, id, &error), qPrintable(error));
        QVERIFY2(journal.append(first, &a, &error), qPrintable(error));
        QVERIFY2(journal.append(second, &b, &error), qPrintable(error));
        QVERIFY(b > a);
        QByteArray read;
        QVERIFY2(journal.read(b, &read, &error), qPrintable(error)); QCOMPARE(read, second);
        QVERIFY2(journal.read(a, &read, &error), qPrintable(error)); QCOMPARE(read, first);
        std::vector<qint64> offsets;
        QVERIFY2(journal.verify(&offsets, &error), qPrintable(error));
        QCOMPARE(offsets.size(), size_t(2)); QCOMPARE(offsets[0], a); QCOMPARE(offsets[1], b);
    }
    {
        HistoryJournal wrong;
        QVERIFY(!wrong.open(path, QUuid::createUuid(), &error));
        QVERIFY(error.contains(QStringLiteral("identity")));
    }
    {
        HistoryJournal reopened;
        QVERIFY2(reopened.open(path, id, &error), qPrintable(error));
        QByteArray read;
        QVERIFY(reopened.read(a, &read, &error)); QCOMPARE(read, first);
        QVERIFY(reopened.read(b, &read, &error)); QCOMPARE(read, second);
    }
}
void TestHistoryJournal::interruptedTail()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("history.bin"));
    const auto id = QUuid::createUuid(); QString error; qint64 first = -1;
    {
        HistoryJournal journal;
        QVERIFY(journal.create(path, id, &error));
        QVERIFY(journal.append(QByteArray("intact"), &first, &error));
    }
    {
        QFile file(path); QVERIFY(file.open(QIODevice::Append)); QCOMPARE(file.write("REC1\x01", 5), qint64(5));
    }
    const auto size = QFile(path).size();
    HistoryJournal reopened;
    QVERIFY(!reopened.open(path, id, &error));
    QVERIFY(error.contains(QStringLiteral("incomplete")));
    QCOMPARE(QFile(path).size(), size); // never silently truncates the tail
}
void TestHistoryJournal::truncatedFrames()
{
    for (int tailBytes : {1, 4, 10, 43, 44, 47, 50}) {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto path = dir.filePath(QStringLiteral("history.bin"));
        const auto id = QUuid::createUuid(); QString error;
        qint64 first = -1, second = -1;
        {
            HistoryJournal journal;
            QVERIFY(journal.create(path, id, &error));
            QVERIFY(journal.append(QByteArray("intact"), &first, &error));
            QVERIFY(journal.append(QByteArray("1234567"), &second, &error));
        }
        QFile file(path); QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(second + tailBytes));
        const auto expected = file.size(); file.close();
        HistoryJournal damaged;
        QVERIFY(!damaged.open(path, id, &error));
        QCOMPARE(QFile(path).size(), expected);
    }
}
void TestHistoryJournal::rejectedOperationsPreserveOutputs()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto id = QUuid::createUuid(); QString error;
    qint64 offset = 123;
    HistoryJournal unopened;
    QVERIFY(!unopened.append(QByteArray("x"), &offset, &error));
    QCOMPARE(offset, qint64(123));
    HistoryJournal journal;
    QVERIFY(journal.create(dir.filePath(QStringLiteral("h")), id, &error));
    QVERIFY(journal.append(QByteArray("first"), &offset, &error));
    QByteArray output("untouched");
    QVERIFY(!journal.read(offset + 1, &output, &error));
    QCOMPARE(output, QByteArray("untouched"));
    QVERIFY(journal.read(offset, &output, &error));
    QCOMPARE(output, QByteArray("first"));
}
void TestHistoryJournal::corruptRecord()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("history.bin"));
    const auto id = QUuid::createUuid(); QString error; qint64 offset = -1;
    {
        HistoryJournal journal;
        QVERIFY(journal.create(path, id, &error));
        QVERIFY(journal.append(QByteArray("original"), &offset, &error));
    }
    {
        QFile file(path); QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.seek(offset + 44)); QCOMPARE(file.write("X", 1), qint64(1));
    }
    HistoryJournal corrupted;
    QVERIFY(!corrupted.open(path, id, &error));
    QVERIFY(error.contains(QStringLiteral("checksum")));
}
void TestHistoryJournal::invalidPathAndExistingFile()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto id = QUuid::createUuid(); QString error;
    HistoryJournal missing;
    QVERIFY(!missing.create(dir.filePath(QStringLiteral("missing/file.bin")), id, &error));
    QVERIFY(error.contains(QStringLiteral("create")));
    const auto path = dir.filePath(QStringLiteral("existing"));
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("keep"), qint64(4)); file.close();
    HistoryJournal duplicate;
    QVERIFY(!duplicate.create(path, id, &error));
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("keep"));
}
QTEST_APPLESS_MAIN(TestHistoryJournal)
#include "TestHistoryJournal.moc"
