#include <QtTest/QtTest>
#include "app/CommandLine.h"

using namespace komapaint;

class TestCommandLine : public QObject
{
    Q_OBJECT
private slots:
    void empty();
    void launchWithFile();
    void launchWithUrl();
    void associateHeadless();
    void unassociateHeadless();
    void helpHeadless();
    void unknownOptionRejected();
    void multiplePositionalsRejected();
    void headlessWithPositionalRejected();
    void headlessOptionsCombinedRejected();
};

void TestCommandLine::empty()
{
    const CommandLineRequest req = CommandLine::parse({});
    QVERIFY(req.error.isEmpty());
    QCOMPARE(req.mode, CommandLineRequest::Mode::Launch);
    QVERIFY(req.fileToOpen.isEmpty());
}

void TestCommandLine::launchWithFile()
{
    const CommandLineRequest req = CommandLine::parse({QStringLiteral("artwork.koma")});
    QVERIFY(req.error.isEmpty());
    QCOMPARE(req.mode, CommandLineRequest::Mode::Launch);
    QCOMPARE(req.fileToOpen, QStringLiteral("artwork.koma"));
}

void TestCommandLine::launchWithUrl()
{
    const CommandLineRequest req = CommandLine::parse({QStringLiteral("file:///home/user/Piece.koma")});
    QVERIFY(req.error.isEmpty());
    QCOMPARE(req.mode, CommandLineRequest::Mode::Launch);
    QCOMPARE(req.fileToOpen, QStringLiteral("file:///home/user/Piece.koma"));
}

void TestCommandLine::associateHeadless()
{
    const CommandLineRequest req = CommandLine::parse({QStringLiteral("--associate")});
    QVERIFY(req.error.isEmpty());
    QCOMPARE(req.mode, CommandLineRequest::Mode::Associate);
    QVERIFY(req.fileToOpen.isEmpty());
}

void TestCommandLine::unassociateHeadless()
{
    const CommandLineRequest req = CommandLine::parse({QStringLiteral("--unassociate")});
    QVERIFY(req.error.isEmpty());
    QCOMPARE(req.mode, CommandLineRequest::Mode::Unassociate);
    QVERIFY(req.fileToOpen.isEmpty());
}

void TestCommandLine::helpHeadless()
{
    for (const QString flag : {QStringLiteral("--help"), QStringLiteral("-h"), QStringLiteral("-help")}) {
        const CommandLineRequest req = CommandLine::parse({flag});
        QVERIFY2(req.error.isEmpty(), qPrintable(flag));
        QCOMPARE(req.mode, CommandLineRequest::Mode::Help);
    }
}

void TestCommandLine::unknownOptionRejected()
{
    const CommandLineRequest req = CommandLine::parse({QStringLiteral("--frobnicate")});
    QVERIFY(!req.error.isEmpty());
    const CommandLineRequest singleDash = CommandLine::parse({QStringLiteral("-x")});
    QVERIFY(!singleDash.error.isEmpty());
}

void TestCommandLine::multiplePositionalsRejected()
{
    const CommandLineRequest req = CommandLine::parse({QStringLiteral("a.koma"), QStringLiteral("b.koma")});
    QVERIFY(!req.error.isEmpty());
    QCOMPARE(req.mode, CommandLineRequest::Mode::Launch); // mode stays Launch; caller shows the error
}

void TestCommandLine::headlessWithPositionalRejected()
{
    const CommandLineRequest req = CommandLine::parse({QStringLiteral("--associate"), QStringLiteral("artwork.koma")});
    QVERIFY(!req.error.isEmpty());
}

void TestCommandLine::headlessOptionsCombinedRejected()
{
    const CommandLineRequest req = CommandLine::parse({QStringLiteral("--associate"), QStringLiteral("--unassociate")});
    QVERIFY(!req.error.isEmpty());
}

QTEST_APPLESS_MAIN(TestCommandLine)
#include "TestCommandLine.moc"
