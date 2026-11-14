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
    void sharesDocumentBudget();
    void removeDuringArtworkCallback();
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

void TestLayers::sharesDocumentBudget()
{
    LayerModel model;
    model.addLayer();
    auto budget = model.documentBudget();
    const auto tile = model.layerAt(0)->tiles().tileBytes();
    QVERIFY(budget->setLimit(2 * tile));
    QVERIFY(model.layerAt(0)->tiles().documentBudget() == budget);
    QVERIFY(model.layerAt(1)->tiles().documentBudget() == budget);
    QCOMPARE(model.layerAt(0)->tiles().setPixel({0, 0}, 0xff123456U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(model.layerAt(1)->tiles().setPixel({0, 0}, 0xffabcdefU), SparseTileStore::WriteResult::Changed);
    QCOMPARE(budget->artworkBytes(), 2 * tile);
    QCOMPARE(model.layerAt(1)->tiles().setPixel({256, 0}, 0xff000000U), SparseTileStore::WriteResult::DocumentMemoryLimit);
    model.removeLayer(1);
    QCOMPARE(budget->artworkBytes(), tile);
    QCOMPARE(model.layerAt(0)->tiles().setPixel({256, 0}, 0xff000000U), SparseTileStore::WriteResult::Changed);
}


void TestLayers::removeDuringArtworkCallback()
{
    LayerModel model;
    model.addLayer();
    auto* layer = model.layerAt(1);
    int attempts = 0;
    connect(layer, &Layer::artworkChanged, &model, [&] {
        ++attempts;
        model.removeLayer(1);
    });
    QCOMPARE(layer->tiles().setPixel({1, 1}, 0x80402010U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(attempts, 1);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(layer->tiles().pixel({1, 1}).value(), 0x80402010U);
    layer->tiles().beginBatch();
    QCOMPARE(layer->tiles().setPixel({257, 1}, 0xff123456U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(layer->tiles().setPixel({513, 1}, 0x80800000U), SparseTileStore::WriteResult::Changed);
    layer->tiles().endBatch();
    QCOMPARE(attempts, 3);
    QCOMPARE(model.rowCount(), 2);
    model.removeLayer(1);
    QCOMPARE(model.rowCount(), 1);
}

QTEST_APPLESS_MAIN(TestLayers)
#include "TestLayers.moc"
