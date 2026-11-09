#include <QtTest/QtTest>
#include "app/CanvasViewport.h"
#include "canvas/CanvasDocument.h"
#include "layers/Layer.h"
#include <cmath>

using namespace komapaint;
class TestCanvasViewport : public QObject {
    Q_OBJECT
private slots:
    void navigationAndVisibleTiles();
    void dirtyAndLayers();
};
void TestCanvasViewport::navigationAndVisibleTiles()
{
    CanvasDocument doc;
    doc.setSize(QSize(6000, 4000));
    doc.setTransparentBackground(false);
    CanvasViewport view;
    view.setWidth(512); view.setHeight(400); view.setDocument(&doc);
    view.refreshNow();
    auto first = view.compositeCount();
    QVERIFY(first > 0 && first < 20); // visible tiles only, not the whole document
    QCOMPARE(view.frameSnapshot().size(), QSize(512, 400));
    auto point = view.viewportToDocument(100, 70);
    auto roundtrip = view.documentToViewport(point.x(), point.y());
    QVERIFY(std::abs(roundtrip.x() - 100) < 0.001);
    QVERIFY(std::abs(roundtrip.y() - 70) < 0.001);
    view.zoomAt(100, 70, 1.5);
    auto afterZoom = view.viewportToDocument(100, 70);
    QVERIFY(std::abs(afterZoom.x() - point.x()) < 0.001);
    QVERIFY(std::abs(afterZoom.y() - point.y()) < 0.001);
    view.refreshNow();
    auto count = view.compositeCount();
    view.panBy(5, 5); view.refreshNow();
    QVERIFY(view.compositeCount() - count < 10); // cached tiles survive navigation
    auto origin = view.documentToViewport(0, 0);
    view.panBy(10 - origin.x(), 10 - origin.y()); view.refreshNow();
    auto frame = view.frameSnapshot();
    QCOMPARE(frame.pixelColor(11, 11), QColor(Qt::white));
    QCOMPARE(frame.pixelColor(0, 0), QColor("#4a4d52"));
    view.fit(); view.refreshNow();
    QVERIFY(view.zoom() >= 6.25 && view.zoom() < 10);
    QVERIFY(!view.frameSnapshot().isNull());
}
void TestCanvasViewport::dirtyAndLayers()
{
    CanvasDocument doc;
    doc.setSize(QSize(600, 400));
    doc.setTransparentBackground(false);
    CanvasViewport view;
    view.setWidth(320); view.setHeight(240); view.setDocument(&doc);
    view.refreshNow();
    auto* lower = doc.layers()->layerAt(0);
    auto where = view.documentToViewport(300, 200);
    QCOMPARE(lower->tiles().setPixel({300, 200}, 0xffff0000U), SparseTileStore::WriteResult::Changed);
    auto count = view.compositeCount();
    view.refreshNow();
    QCOMPARE(view.compositeCount() - count, std::size_t(1));
    QCOMPARE(view.frameSnapshot().pixelColor(qRound(where.x()), qRound(where.y())), QColor(Qt::red));
    count = view.compositeCount();
    view.refreshNow(); QCOMPARE(view.compositeCount(), count);
    // The newest row is displayed below row 0, matching the panel order.
    doc.layers()->addLayer();
    auto* upper = doc.layers()->layerAt(0);
    auto* bottom = doc.layers()->layerAt(1);
    QCOMPARE(bottom->tiles().setPixel({300, 200}, 0xff0000ffU), SparseTileStore::WriteResult::Changed);
    view.refreshNow();
    QCOMPARE(view.frameSnapshot().pixelColor(qRound(where.x()), qRound(where.y())), QColor(Qt::red));
    upper->setVisible(false); view.refreshNow();
    QCOMPARE(view.frameSnapshot().pixelColor(qRound(where.x()), qRound(where.y())), QColor(Qt::blue));
    upper->setVisible(true); upper->setOpacity(0.5); view.refreshNow();
    auto mixed = view.frameSnapshot().pixelColor(qRound(where.x()), qRound(where.y()));
    QVERIFY(mixed.red() >= 127 && mixed.blue() >= 126);
    doc.layers()->moveLayer(0, 1); view.refreshNow();
    QCOMPARE(view.frameSnapshot().pixelColor(qRound(where.x()), qRound(where.y())), QColor(Qt::blue));
    doc.setBackgroundColor(Qt::green); view.refreshNow();
    auto blank = view.documentToViewport(280, 180);
    QCOMPARE(view.frameSnapshot().pixelColor(qRound(blank.x()), qRound(blank.y())), QColor(Qt::green));
    // Move the entire first tile offscreen before editing it.
    view.panBy(-140, 0); view.refreshNow();
    // Offscreen edits stay dirty until brought into view.
    QCOMPARE(bottom->tiles().setPixel({0, 0}, 0xff00ff00U), SparseTileStore::WriteResult::Changed);
    view.refreshNow(); QVERIFY(!bottom->tiles().dirtyRegions().empty());
    auto origin = view.documentToViewport(0, 0);
    view.panBy(10 - origin.x(), 10 - origin.y()); view.refreshNow();
    QVERIFY(bottom->tiles().dirtyRegions().empty());
    QCOMPARE(view.frameSnapshot().pixelColor(10, 10), QColor(Qt::green));
}
QTEST_MAIN(TestCanvasViewport)
#include "TestCanvasViewport.moc"
