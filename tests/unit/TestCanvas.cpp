#include <QtTest/QtTest>

#include "canvas/CanvasDocument.h"

using namespace komapaint;

class TestCanvas : public QObject
{
    Q_OBJECT
private slots:
    void defaults();
    void sizeValidates();
    void dpiValidates();
};

void TestCanvas::defaults()
{
    CanvasDocument doc;
    QCOMPARE(doc.title(), QString("Untitled"));
    QCOMPARE(doc.width(), 1600);
    QCOMPARE(doc.height(), 1200);
    QCOMPARE(doc.dpi(), 300.0);
    QVERIFY(doc.hasTransparentBackground());
}

void TestCanvas::sizeValidates()
{
    CanvasDocument doc;
    doc.setWidth(0);
    QCOMPARE(doc.width(), 1600);
    doc.setWidth(3200);
    QCOMPARE(doc.width(), 3200);
    doc.setHeight(-1);
    QCOMPARE(doc.height(), 1200);
}

void TestCanvas::dpiValidates()
{
    CanvasDocument doc;
    doc.setDpi(0);
    QCOMPARE(doc.dpi(), 300.0);
    doc.setDpi(72);
    QCOMPARE(doc.dpi(), 72.0);
}

QTEST_APPLESS_MAIN(TestCanvas)
#include "TestCanvas.moc"
