#include "region/RegionEngine.h"
#include "region/SelectionMask.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace komapaint;
using namespace komapaint::region;

namespace {

std::uint32_t premul(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    const auto pr = static_cast<std::uint32_t>((static_cast<int>(r) * a + 127) / 255);
    const auto pg = static_cast<std::uint32_t>((static_cast<int>(g) * a + 127) / 255);
    const auto pb = static_cast<std::uint32_t>((static_cast<int>(b) * a + 127) / 255);
    return (static_cast<std::uint32_t>(a) << 24) | (pr << 16) | (pg << 8) | pb;
}

void drawThickLine(SparseTileStore& store, int x0, int y0, int x1, int y1, int radius, std::uint32_t color)
{
    const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0)) * 2 + 1;
    const auto w = store.geometry().width();
    const auto h = store.geometry().height();
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const int cx = static_cast<int>(std::lround(x0 + (x1 - x0) * t));
        const int cy = static_cast<int>(std::lround(y0 + (y1 - y0) * t));
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy <= radius * radius) {
                    const int px = cx + dx;
                    const int py = cy + dy;
                    if (px >= 0 && py >= 0 && px < w && py < h) {
                        store.setPixel(TilePoint{px, py}, color);
                    }
                }
            }
        }
    }
}

void drawRectStroke(SparseTileStore& store, int x0, int y0, int x1, int y1, int radius,
                    std::uint32_t color, int gapTopLen = 0)
{
    const int midX = (x0 + x1) / 2;
    if (gapTopLen <= 0) {
        drawThickLine(store, x0, y0, x1, y0, radius, color);
    } else {
        const int halfGap = gapTopLen / 2;
        drawThickLine(store, x0, y0, midX - halfGap - radius, y0, radius, color);
        drawThickLine(store, midX + halfGap + radius, y0, x1, y0, radius, color);
    }
    drawThickLine(store, x1, y0, x1, y1, radius, color);
    drawThickLine(store, x1, y1, x0, y1, radius, color);
    drawThickLine(store, x0, y1, x0, y0, radius, color);
}

void testSelectionMaskOperations()
{
    SelectionMask mask(8192, 8192);
    assert(mask.empty());

    mask.applyShape(SelectionShape::Rectangle, 20.0, 20.0, 60.0, 60.0, false, 0, SelectionOp::Replace);
    assert(!mask.empty());
    assert(mask.at(40, 40) == 255);
    assert(mask.at(10, 10) == 0);
    // Sparse allocation: 8192x8192 only allocated tiles touching [20, 60] -> 1 tile!
    assert(mask.allocatedTiles() <= 2);

    // Add ellipse
    mask.applyShape(SelectionShape::Ellipse, 50.0, 20.0, 90.0, 60.0, true, 0, SelectionOp::Add);
    assert(mask.at(40, 40) == 255);
    assert(mask.at(70, 40) == 255);

    // Subtract inner rect
    mask.applyShape(SelectionShape::Rectangle, 30.0, 30.0, 45.0, 45.0, false, 0, SelectionOp::Subtract);
    assert(mask.at(35, 35) == 0);
    assert(mask.at(25, 25) == 255);

    // Invert on 8192x8192 stays sparse!
    mask.invert();
    assert(mask.at(35, 35) == 255);
    assert(mask.at(25, 25) == 0);
    assert(mask.at(5000, 5000) == 255);
    assert(mask.allocatedTiles() <= 4);

    // Restore snapshot
    mask.invert();
    auto snap = mask.snapshot();
    mask.clear();
    assert(mask.empty());
    mask.restore(snap);
    assert(mask.at(25, 25) == 255);

    // Polygon lasso with AA + expand/contract/feather
    std::vector<Vec2> tri = {{100.0, 100.0}, {160.0, 100.0}, {130.0, 150.0}};
    mask.applyPolygon(tri, true, 0, SelectionOp::Replace);
    assert(mask.at(130, 120) == 255);
    assert(mask.at(90, 90) == 0);
    mask.expand(4);
    assert(mask.at(130, 97) > 0);
    mask.contract(4);
    mask.feather(2);
    assert(mask.at(130, 120) > 200);
}

void testConventionalVsSmartFill()
{
    const std::int64_t W = 192, H = 192;
    SparseTileStore lineArt(W, H, 64);
    const std::uint32_t black = premul(15, 15, 18, 255);

    // 1. Box with 6px accidental gap at top center: [40..140, 40..140]
    drawRectStroke(lineArt, 40, 40, 140, 140, 2, black, 6);

    ReferenceSet refs;
    refs.docWidth = W;
    refs.docHeight = H;
    refs.transparentBackground = true;
    refs.mode = ReferenceMode::AllVisible;
    refs.layersTopFirst.push_back(
        ReferenceLayerDesc{&lineArt, &lineArt, {}, lineArt.dirtyVersion(), 1.0, BlendMode::Normal, true, true, true});

    RegionEngine engine;

    // Conventional Fill with gapClosing=0 MUST leak outside the 6px gap!
    FillParams convParams;
    convParams.tolerance = 28;
    convParams.gapClosing = 0;
    convParams.areaScaling = 0;
    const RegionResult convRes = engine.computeConventionalFill(refs, 90, 90, convParams);
    assert(!convRes.empty());
    assert(convRes.coverageAt(10, 10) > 0); // Leaked outside!

    // Smart Fill with gapTolerance=12 bridges the 6px gap and stays inside!
    SmartFillParams smartParams;
    smartParams.gapTolerance = 12;
    smartParams.boundarySensitivity = 70;
    smartParams.areaScaling = 0;
    const RegionResult smartRes = engine.computeSmartFill(refs, 90, 90, smartParams);
    assert(!smartRes.empty());
    assert(smartRes.status == RegionStatus::PartiallyEnclosed);
    assert(smartRes.uncertainPixels > 0);
    assert(smartRes.coverageAt(90, 90) == 255);
    assert(smartRes.coverageAt(10, 10) == 0); // Contained!

    // 2. Large intentional 46px opening: Smart Fill (gapTolerance=10) must NOT fabricate a huge fake wall!
    SparseTileStore openArt(W, H, 64);
    drawRectStroke(openArt, 40, 40, 140, 140, 2, black, 46);
    refs.layersTopFirst[0].digital = &openArt;
    refs.layersTopFirst[0].layerKey = &openArt;
    refs.layersTopFirst[0].revision = openArt.dirtyVersion();
    const RegionResult openRes = engine.computeSmartFill(refs, 90, 90, smartParams);
    assert(openRes.status == RegionStatus::AmbiguousLeak);
    assert(openRes.confidence < 0.5);
}

void testColoredAndMultiLayerAndCacheInvalidation()
{
    const std::int64_t W = 192, H = 192;
    SparseTileStore bgPaint(W, H, 64);
    SparseTileStore lineLayer(W, H, 64);

    // Dark blue background on layer 0, warm gold line art on layer 1
    const std::uint32_t darkBg = premul(24, 30, 52, 255);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            bgPaint.setPixel(TilePoint{x, y}, darkBg);
        }
    }
    const std::uint32_t goldInk = premul(240, 200, 90, 255);
    drawRectStroke(lineLayer, 30, 30, 110, 110, 2, goldInk, 0);

    ReferenceSet refs;
    refs.docWidth = W;
    refs.docHeight = H;
    refs.transparentBackground = false;
    refs.mode = ReferenceMode::AllVisible;
    refs.layersTopFirst.push_back(
        ReferenceLayerDesc{&lineLayer, &lineLayer, {}, lineLayer.dirtyVersion(), 1.0, BlendMode::Normal, true, false, false});
    refs.layersTopFirst.push_back(
        ReferenceLayerDesc{&bgPaint, &bgPaint, {}, bgPaint.dirtyVersion(), 1.0, BlendMode::Normal, true, true, true});

    RegionEngine engine;
    SmartFillParams p;
    p.boundarySensitivity = 70;
    const RegionResult r1 = engine.computeSmartFill(refs, 70, 70, p);
    assert(!r1.empty());
    assert(r1.coverageAt(70, 70) == 255);
    assert(r1.coverageAt(150, 150) == 0); // Stopped by light-on-dark gold contour!

    // Verify CurrentLayer mode ignores top lineLayer!
    refs.mode = ReferenceMode::CurrentLayer;
    const RegionResult rCur = engine.computeConventionalFill(refs, 70, 70, FillParams{});
    assert(rCur.coverageAt(150, 150) == 255);

    // Verify dirty-tile cache invalidation when bgPaint changes in one tile
    refs.mode = ReferenceMode::AllVisible;
    bgPaint.setPixel(TilePoint{160, 160}, premul(255, 40, 40, 255));
    refs.layersTopFirst[1].revision = bgPaint.dirtyVersion();
    MagicWandParams wp;
    wp.tolerance = 18;
    wp.contiguous = true;
    const RegionResult wandSpot = engine.computeMagicWand(refs, 160, 160, wp);
    assert(wandSpot.filledPixels == 1);
}

void testProtectedRegionAndMagneticLasso()
{
    const std::int64_t W = 192, H = 192;
    SparseTileStore lineArt(W, H, 64);
    const std::uint32_t black = premul(10, 10, 10, 255);
    drawRectStroke(lineArt, 50, 50, 130, 130, 2, black, 4); // 4px small gap

    ReferenceSet refs;
    refs.docWidth = W;
    refs.docHeight = H;
    refs.transparentBackground = true;
    refs.mode = ReferenceMode::AllVisible;
    refs.layersTopFirst.push_back(
        ReferenceLayerDesc{&lineArt, &lineArt, {}, lineArt.dirtyVersion(), 1.0, BlendMode::Normal, true, true, true});

    RegionEngine engine;
    ProtectedRegionParams pp;
    pp.gapTolerance = 10;
    pp.conservativeBoundary = true;
    const RegionResult prot = engine.computeProtectedRegion(refs, 90, 90, pp);
    assert(!prot.empty());
    assert(prot.coverageAt(90, 90) == 255); // Interior protected!
    assert(prot.coverageAt(20, 20) == 0);   // Exterior unprotected -> safe to erase!

    // Magnetic lasso along top edge (50, 50) -> (130, 50) starting slightly offset in Y
    const auto snapped = engine.snapMagneticPath(refs, {50.0, 55.0}, {130.0, 55.0}, 90);
    assert(snapped.size() > 10);
    // Midpoint should pull toward y=50..52 (the high-contrast horizontal ink edge), not stay at y=55!
    const Vec2& mid = snapped[snapped.size() / 2];
    assert(mid.y < 54.0);
}

void testDefault256TileStoreSmartFill()
{
    SparseTileStore store(600, 600, 256);
    for (int x = 120; x <= 320; ++x) {
        for (int t = -2; t <= 2; ++t) {
            store.setPixel({x, 120 + t}, 0xFF141414U);
            store.setPixel({x, 320 + t}, 0xFF141414U);
        }
    }
    for (int y = 120; y <= 320; ++y) {
        for (int t = -2; t <= 2; ++t) {
            store.setPixel({120 + t, y}, 0xFF141414U);
            store.setPixel({320 + t, y}, 0xFF141414U);
        }
    }
    ReferenceSet refs;
    refs.docWidth = 600;
    refs.docHeight = 600;
    refs.transparentBackground = false;
    refs.backgroundArgb = 0xFFFFFFFFU;
    refs.mode = ReferenceMode::AllVisible;
    refs.layersTopFirst.push_back({&store, &store, {}, store.dirtyVersion(), 1.0, BlendMode::Normal, true, true, true});

    RegionEngine engine;
    SmartFillParams sp;
    sp.tolerance = 34;
    sp.boundarySensitivity = 65;
    sp.gapTolerance = 10;
    sp.areaScaling = 1;
    const auto res = engine.computeSmartFill(refs, 220, 220, sp, nullptr);
    assert(res.status == RegionStatus::Enclosed);
    assert(res.coverageAt(220, 220) > 200);
    assert(res.coverageAt(420, 420) == 0);
}

} // namespace

int main()
{
    testSelectionMaskOperations();
    testConventionalVsSmartFill();
    testColoredAndMultiLayerAndCacheInvalidation();
    testProtectedRegionAndMagneticLasso();
    testDefault256TileStoreSmartFill();
    std::cout << "TestRegionEngine PASSED\n";
    return 0;
}
