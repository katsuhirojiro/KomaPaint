#include <QtTest/QtTest>

#include "layers/LayerModel.h"
#include "layers/Layer.h"

using namespace komapaint;

class TestLayers : public QObject
{
    Q_OBJECT
private slots:
    void startsWithOneLayer();
    void addLayer();
    void removeLayerPreservesOne();
    void activeLayerClamps();
};

void TestLayers::startsWithOneLayer()
{
    LayerModel model;
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.activeLayerIndex(), 0);
}

void TestLayers::addLayer()
{
    LayerModel model;
    model.addLayer();
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.layerAt(1) != nullptr);
}

void TestLayers::removeLayerPreservesOne()
{
    LayerModel model;
    model.addLayer();
    model.removeLayer(1);
    QCOMPARE(model.rowCount(), 1);
    model.removeLayer(0);  // should refuse, must always have >= 1 layer
    QCOMPARE(model.rowCount(), 1);
}

void TestLayers::activeLayerClamps()
{
    LayerModel model;
    model.addLayer(); model.addLayer();
    model.setActiveLayerIndex(2);
    model.removeLayer(2);
    QCOMPARE(model.activeLayerIndex(), 1);
}

QTEST_APPLESS_MAIN(TestLayers)
#include "TestLayers.moc"
