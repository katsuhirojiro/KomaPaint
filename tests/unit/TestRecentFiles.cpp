#include <QtTest/QtTest>
#include <QCoreApplication>

#include "core/Settings.h"
#include "files/RecentFiles.h"

using namespace komapaint;

class TestRecentFiles : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void addMovesToTop();
    void clear();
private:
    Settings* m_settings = nullptr;
};

void TestRecentFiles::initTestCase()
{
    // Use a unique org/app name so the test doesn't touch user settings.
    QCoreApplication::setOrganizationName("KomaPaintTest");
    QCoreApplication::setApplicationName("UnitTests");
    m_settings = new Settings("KomaPaintTest", "UnitTests", this);
}

void TestRecentFiles::addMovesToTop()
{
    RecentFiles rf(m_settings);
    rf.clear();
    rf.add("/a.kmp");
    rf.add("/b.kmp");
    QCOMPARE(rf.files().first(), QString("/b.kmp"));
    rf.add("/a.kmp");
    QCOMPARE(rf.files().first(), QString("/a.kmp"));
    QCOMPARE(rf.files().size(), 2);
}

void TestRecentFiles::clear()
{
    RecentFiles rf(m_settings);
    rf.add("/x.kmp");
    rf.clear();
    QCOMPARE(rf.files().size(), 0);
}

QTEST_GUILESS_MAIN(TestRecentFiles)
#include "TestRecentFiles.moc"
