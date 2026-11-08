#include <QtTest/QtTest>

#include "animation/TimelineState.h"

using namespace komapaint;

class TestTimeline : public QObject
{
    Q_OBJECT
private slots:
    void defaultState();
    void frameClamps();
    void stopResets();
};

void TestTimeline::defaultState()
{
    TimelineState t;
    QCOMPARE(t.frameCount(), 24);
    QCOMPARE(t.currentFrame(), 0);
    QCOMPARE(t.fps(), 24);
    QVERIFY(!t.isPlaying());
}

void TestTimeline::frameClamps()
{
    TimelineState t;
    t.setCurrentFrame(100);
    QCOMPARE(t.currentFrame(), t.frameCount()-1);
    t.setCurrentFrame(-5);
    QCOMPARE(t.currentFrame(), 0);
}

void TestTimeline::stopResets()
{
    TimelineState t;
    t.setCurrentFrame(10);
    t.play();
    t.stop();
    QCOMPARE(t.currentFrame(), 0);
    QVERIFY(!t.isPlaying());
}

QTEST_APPLESS_MAIN(TestTimeline)
#include "TestTimeline.moc"
