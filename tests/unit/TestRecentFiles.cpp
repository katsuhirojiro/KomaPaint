#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

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
    QString m_a;
    QString m_b;
    QString m_x;
};

void TestRecentFiles::initTestCase()
{
    // Use a unique org/app name so the test doesn't touch user settings.
    QCoreApplication::setOrganizationName("KomaPaintTest");
    QCoreApplication::setApplicationName("UnitTests");
    m_settings = new Settings("KomaPaintTest", "UnitTests", this);

    // Build absolute paths portably: on Windows QFileInfo prepends the current
    // drive letter to Unix-style absolute paths, so we cannot hard-code "/a.kmp"
    // and compare. Use QDir::temp() so the resolved paths are absolute on every
    // platform.
    m_a = QFileInfo(QDir::tempPath(), "a.kmp").absoluteFilePath();
    m_b = QFileInfo(QDir::tempPath(), "b.kmp").absoluteFilePath();
    m_x = QFileInfo(QDir::tempPath(), "x.kmp").absoluteFilePath();
}

void TestRecentFiles::addMovesToTop()
{
    RecentFiles rf(m_settings);
    rf.clear();
    rf.add(m_a);
    rf.add(m_b);
    QCOMPARE(rf.files().first(), m_b);
    rf.add(m_a);
    QCOMPARE(rf.files().first(), m_a);
    QCOMPARE(rf.files().size(), 2);
}

void TestRecentFiles::clear()
{
    RecentFiles rf(m_settings);
    rf.add(m_x);
    rf.clear();
    QCOMPARE(rf.files().size(), 0);
}

QTEST_GUILESS_MAIN(TestRecentFiles)
#include "TestRecentFiles.moc"
