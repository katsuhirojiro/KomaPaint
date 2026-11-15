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
    void thumbnailsFollowIndividualLayerPixels();
    void movingAndRemovingPreservesActiveLayer();
    void removingSelectedLayerNotifiesEvenWhenIndexUnchanged();
    void childIndexesHaveNoRows();
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

void TestLayers::movingAndRemovingPreservesActiveLayer()
{
    LayerModel model;
    model.addLayer();
    model.addLayer();
    model.setActiveLayerIndex(1);
    Layer* selected = model.layerAt(1);
    model.moveLayer(1, 2);
    QCOMPARE(model.activeLayerIndex(), 2);
    QCOMPARE(model.layerAt(model.activeLayerIndex()), selected);
    model.moveLayer(0, 2);
    QCOMPARE(model.activeLayerIndex(), 1);
    QCOMPARE(model.layerAt(model.activeLayerIndex()), selected);
    model.removeLayer(0);
    QCOMPARE(model.activeLayerIndex(), 0);
    QCOMPARE(model.layerAt(model.activeLayerIndex()), selected);
    for (int i = 0; i < model.rowCount(); ++i) QCOMPARE(model.layerAt(i)->index(), i);
}

void TestLayers::removingSelectedLayerNotifiesEvenWhenIndexUnchanged()
{
    LayerModel model;
    model.addLayer();
    model.addLayer();
    model.setActiveLayerIndex(1);
    Layer* replacement = model.layerAt(2);
    QSignalSpy changed(&model, &LayerModel::activeLayerIndexChanged);
    model.removeLayer(1);
    QCOMPARE(model.activeLayerIndex(), 1);
    QCOMPARE(model.layerAt(1), replacement);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.takeFirst().at(0).toInt(), 1);
}

void TestLayers::childIndexesHaveNoRows()
{
    LayerModel model;
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    QVERIFY(!model.index(0, 0, model.index(0, 0)).isValid());
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

void TestLayers::thumbnailsFollowIndividualLayerPixels()
{
    LayerModel model;
    model.addLayer();
    auto* first = model.layerAt(0);
    auto* second = model.layerAt(1);
    QVERIFY(first->tiles().resizeEmpty(32, 32));
    QVERIFY(second->tiles().resizeEmpty(32, 32));
    QCOMPARE(first->tiles().setPixel({8, 8}, 0xffff0000U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(second->tiles().setPixel({8, 8}, 0xff0000ffU), SparseTileStore::WriteResult::Changed);
    const auto a = first->thumbnailPixels(32);
    const auto b = second->thumbnailPixels(32);
    QCOMPARE(a.size(), 1024);
    QCOMPARE(b.size(), 1024);
    QCOMPARE(a.at(8 * 32 + 8).toUInt(), 0xffff0000U);
    QCOMPARE(b.at(8 * 32 + 8).toUInt(), 0xff0000ffU);
    QCOMPARE(a.at(0).toUInt(), 0U);
    first->setVisible(false);
    QCOMPARE(first->thumbnailPixels(32), a); // visibility changes presentation, not artwork
    QCOMPARE(first->tiles().setPixel({8, 8}, 0U), SparseTileStore::WriteResult::Changed);
    QCOMPARE(first->thumbnailPixels(32).at(8 * 32 + 8).toUInt(), 0U);
    QCOMPARE(first->thumbnailPixels(-1).size(), 16 * 16);
    QCOMPARE(first->thumbnailPixels(1000).size(), 64 * 64);
}

QTEST_APPLESS_MAIN(TestLayers)
#include "TestLayers.moc"
