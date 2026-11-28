// Create Brush Tip from Selection reads the same layers and selection mask the
// canvas uses. These checks compare the extracted pixels, the source rectangle
// and the untouched document rather than a second model of what a tip is.
#include <QtTest/QtTest>

#include "app/BrushTipExtraction.h"
#include "app/WorkspaceState.h"
#include "canvas/CanvasDocument.h"
#include "layers/Layer.h"

using namespace komapaint;

namespace {

void put(CanvasDocument& document, int row, int x, int y, std::uint32_t straightArgb)
{
    auto* layer = document.layers()->layerAt(row);
    QVERIFY(layer);
    const auto alpha = (straightArgb >> 24) & 255U;
    const auto red = (straightArgb >> 16) & 255U;
    const auto green = (straightArgb >> 8) & 255U;
    const auto blue = straightArgb & 255U;
    // SparseTileStore expects premultiplied ARGB, like the engines write.
    const auto premultiplied = (alpha << 24) |
        (((red * alpha + 127U) / 255U) << 16) |
        (((green * alpha + 127U) / 255U) << 8) |
        ((blue * alpha + 127U) / 255U);
    QCOMPARE(layer->tiles().setPixel({x, y}, premultiplied),
             SparseTileStore::WriteResult::Changed);
}

QRgb pixel(const QImage& image, int x, int y)
{
    return image.pixel(x, y);
}

} // namespace

class TestBrushTipExtraction : public QObject
{
    Q_OBJECT
private slots:
    void currentLayerWithoutSelectionTrimsTransparency();
    void lassoSelectionRespectsItsBoundary();
    void rectangleSelectionIsSelectableWithoutLosingColour();
    void selectedLayersComposeOnlyTheirArtwork();
    void mergedVisibleHonoursHiddenLayers();
    void alphaAndLuminanceProduceCoverageTips();
    void transparentOrEmptySourceExplainsItself();
    void extractionNeverChangesTheArtwork();
    void availabilityOnlyOffersRealSources();
    void workspaceHandsStudioALosslessPng();
};

void TestBrushTipExtraction::currentLayerWithoutSelectionTrimsTransparency()
{
    CanvasDocument document;
    document.setSize({64, 64});
    put(document, 0, 10, 12, 0xFFFF0000);
    put(document, 0, 11, 12, 0xFF00FF00);

    brushtip::Request request;
    const auto result = brushtip::extract(document, request);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.image.size(), QSize(2, 1));
    QCOMPARE(result.sourceRect, QRect(10, 12, 2, 1));
    QCOMPARE(qRed(pixel(result.image, 0, 0)), 255);
    QCOMPARE(qGreen(pixel(result.image, 0, 0)), 0);
    QCOMPARE(qGreen(pixel(result.image, 1, 0)), 255);
    QCOMPARE(qAlpha(pixel(result.image, 1, 0)), 255);
}

void TestBrushTipExtraction::lassoSelectionRespectsItsBoundary()
{
    CanvasDocument document;
    document.setSize({64, 64});
    put(document, 0, 10, 10, 0xFF3366CC);
    put(document, 0, 20, 20, 0xFFFF0000);
    const std::vector<region::Vec2> lasso{{8, 8}, {18, 8}, {18, 18}, {8, 18}};
    document.selection().applyPolygon(lasso, false, 0, region::SelectionOp::Replace);

    brushtip::Request request;
    request.useSelection = true;
    const auto result = brushtip::extract(document, request);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.sourceRect, QRect(8, 8, 10, 10));
    QCOMPARE(qAlpha(pixel(result.image, 2, 2)), 255);
    QCOMPARE(qAlpha(pixel(result.image, 12, 12)), 0);
}

void TestBrushTipExtraction::rectangleSelectionIsSelectableWithoutLosingColour()
{
    CanvasDocument document;
    document.setSize({64, 64});
    put(document, 0, 4, 4, 0xFF112233);
    document.selection().applyShape(region::SelectionShape::Rectangle, 2, 2, 8, 8,
                                    false, 0, region::SelectionOp::Replace);
    brushtip::Request request;
    request.coverage = brushtip::Coverage::Colour;
    const auto result = brushtip::extract(document, request);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.sourceRect, QRect(2, 2, 6, 6));
    QCOMPARE(qRed(pixel(result.image, 2, 2)), 0x11);
    QCOMPARE(qGreen(pixel(result.image, 2, 2)), 0x22);
    QCOMPARE(qBlue(pixel(result.image, 2, 2)), 0x33);
    QCOMPARE(qAlpha(pixel(result.image, 2, 2)), 255);
    QCOMPARE(qAlpha(pixel(result.image, 0, 0)), 0);
}

void TestBrushTipExtraction::selectedLayersComposeOnlyTheirArtwork()
{
    CanvasDocument document;
    document.setSize({64, 64});
    document.layers()->addLayer();
    put(document, 0, 5, 5, 0xFFFF0000);
    put(document, 1, 6, 5, 0xFF0000FF);
    brushtip::Request request;
    request.source = brushtip::Source::SelectedLayers;
    request.layerRows = {0, 1};
    const auto both = brushtip::extract(document, request);
    QVERIFY2(both.ok(), qPrintable(both.error));
    QCOMPARE(both.image.size(), QSize(2, 1));
    QCOMPARE(qBlue(pixel(both.image, 1, 0)), 255);

    request.layerRows = {0};
    const auto one = brushtip::extract(document, request);
    QVERIFY2(one.ok(), qPrintable(one.error));
    QCOMPARE(one.sourceRect, QRect(5, 5, 1, 1));
}

void TestBrushTipExtraction::mergedVisibleHonoursHiddenLayers()
{
    CanvasDocument document;
    document.setSize({64, 64});
    document.layers()->addLayer();
    put(document, 0, 5, 5, 0xFFFF0000);
    put(document, 1, 10, 5, 0xFF00FF00);
    brushtip::Request request;
    request.source = brushtip::Source::MergedVisible;
    auto merged = brushtip::extract(document, request);
    QVERIFY2(merged.ok(), qPrintable(merged.error));
    QCOMPARE(merged.sourceRect, QRect(5, 5, 6, 1));

    document.layers()->layerAt(1)->setVisible(false);
    merged = brushtip::extract(document, request);
    QVERIFY2(merged.ok(), qPrintable(merged.error));
    QCOMPARE(merged.sourceRect, QRect(5, 5, 1, 1));
}

void TestBrushTipExtraction::alphaAndLuminanceProduceCoverageTips()
{
    CanvasDocument document;
    document.setSize({32, 32});
    put(document, 0, 2, 2, 0x80FF0000); // half transparent red
    put(document, 0, 3, 2, 0xFF000000); // black
    put(document, 0, 4, 2, 0xFFFFFFFF); // white

    brushtip::Request request;
    request.coverage = brushtip::Coverage::Alpha;
    const auto alpha = brushtip::extract(document, request);
    QVERIFY2(alpha.ok(), qPrintable(alpha.error));
    QCOMPARE(qRed(pixel(alpha.image, 0, 0)), 255);
    QCOMPARE(qGreen(pixel(alpha.image, 0, 0)), 255);
    QCOMPARE(qBlue(pixel(alpha.image, 0, 0)), 255);
    QCOMPARE(qAlpha(pixel(alpha.image, 0, 0)), 128);

    request.coverage = brushtip::Coverage::Luminance;
    const auto luminance = brushtip::extract(document, request);
    QVERIFY2(luminance.ok(), qPrintable(luminance.error));
    // Red's luminance is 54; at half alpha that is 27. Opaque black is 0 and
    // opaque white is full coverage, so darkness becomes the tip.
    QCOMPARE(qAlpha(pixel(luminance.image, 0, 0)), 27);
    QCOMPARE(qAlpha(pixel(luminance.image, 1, 0)), 0);
    QCOMPARE(qAlpha(pixel(luminance.image, 2, 0)), 255);
}

void TestBrushTipExtraction::transparentOrEmptySourceExplainsItself()
{
    CanvasDocument document;
    document.setSize({32, 32});
    brushtip::Request request;
    auto result = brushtip::extract(document, request);
    QVERIFY(!result.ok());
    QVERIFY(!result.error.isEmpty());

    document.selection().applyShape(region::SelectionShape::Rectangle, 20, 20, 24, 24,
                                    false, 0, region::SelectionOp::Replace);
    result = brushtip::extract(document, request);
    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("transparent"), Qt::CaseInsensitive));
}

void TestBrushTipExtraction::extractionNeverChangesTheArtwork()
{
    CanvasDocument document;
    document.setSize({48, 48});
    put(document, 0, 7, 7, 0xFF123456);
    const auto before = document.layers()->layerAt(0)->tiles().pixel({7, 7});
    const auto modified = document.isModified();
    document.selection().applyShape(region::SelectionShape::Rectangle, 6, 6, 10, 10,
                                    false, 0, region::SelectionOp::Replace);
    brushtip::Request request;
    QVERIFY(brushtip::extract(document, request).ok());
    QCOMPARE(document.layers()->layerAt(0)->tiles().pixel({7, 7}).value(), before.value());
    QCOMPARE(document.isModified(), modified);
}

void TestBrushTipExtraction::availabilityOnlyOffersRealSources()
{
    CanvasDocument document;
    document.setSize({32, 32});
    auto available = brushtip::availability(document);
    QVERIFY(!available.currentLayer);
    QVERIFY(!available.mergedVisible);
    QVERIFY(!available.reason.isEmpty());

    put(document, 0, 1, 1, 0xFFFF0000);
    available = brushtip::availability(document);
    QVERIFY(available.currentLayer);
    QVERIFY(available.mergedVisible);
    QVERIFY(!available.selectedLayers); // no rows were named
    QVERIFY(!available.hasSelection);

    document.layers()->setSelectedLayerRows({0});
    QVERIFY(brushtip::availability(document, {0}).selectedLayers);
    document.layers()->addLayer();
    put(document, 1, 2, 2, 0xFF00FF00);
    document.layers()->setSelectedLayerRows({0, 1});
    available = brushtip::availability(document, {0, 1});
    QVERIFY(available.selectedLayers);
    QCOMPARE(available.selectedLayerCount, 2);
}

void TestBrushTipExtraction::workspaceHandsStudioALosslessPng()
{
    WorkspaceState workspace;
    QVERIFY(workspace.newDocument(64, 64, true, QColor(Qt::white)));
    auto* document = qobject_cast<CanvasDocument*>(workspace.document());
    QVERIFY(document);
    put(*document, 0, 9, 9, 0xFFFF8000);

    auto info = workspace.brushTipSourceInfo();
    QVERIFY(info.value(QStringLiteral("hasDocument")).toBool());
    QVERIFY(info.value(QStringLiteral("currentLayer")).toBool());
    QVERIFY(info.value(QStringLiteral("mergedVisible")).toBool());

    document->selectAll();
    info = workspace.brushTipSourceInfo();
    QVERIFY(info.value(QStringLiteral("hasSelection")).toBool());
    const auto source = workspace.createBrushTipSource(QStringLiteral("current"), true,
                                                        QStringLiteral("colour"));
    QVERIFY2(source.value(QStringLiteral("ok")).toBool(),
             qPrintable(source.value(QStringLiteral("error")).toString()));
    const auto png = QByteArray::fromBase64(source.value(QStringLiteral("png")).toString().toLatin1());
    QVERIFY(!png.isEmpty());
    const QImage decoded = QImage::fromData(png, "PNG");
    QVERIFY(!decoded.isNull());
    QCOMPARE(decoded.size(), QSize(64, 64));
    QCOMPARE(qRed(decoded.pixel(9, 9)), 255);
    QCOMPARE(qGreen(decoded.pixel(9, 9)), 128);
    QCOMPARE(qAlpha(decoded.pixel(9, 9)), 255);

    const auto missing = workspace.createBrushTipSource(QStringLiteral("not-a-source"), true,
                                                         QStringLiteral("colour"));
    QVERIFY(!missing.value(QStringLiteral("ok")).toBool());
    QVERIFY(!missing.value(QStringLiteral("error")).toString().isEmpty());
}

QTEST_MAIN(TestBrushTipExtraction)
#include "TestBrushTipExtraction.moc"
