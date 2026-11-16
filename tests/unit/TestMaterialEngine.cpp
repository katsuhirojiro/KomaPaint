#include "material/MaterialEngine.h"
#include "material/MaterialTileStore.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

using namespace komapaint;
using namespace komapaint::material;

namespace {
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << " : " #expression << '\n'; ++failures; \
} } while (false)

void pigmentMixingRetainsCompositionAndIsNotRgbAveraging()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    PaintBatch yellow; yellow.mediumId = "watercolour"; yellow.addPigment("PY3", 1.0); yellow.addWater(1.0);
    PaintBatch blue; blue.mediumId = "watercolour"; blue.addPigment("PB15", 1.0); blue.addWater(1.0);
    const PaintBatch green = mix(yellow, blue, "watercolour");
    CHECK(green.pigments.size() == 2);
    CHECK(std::abs(green.pigmentMass() - 2.0) < 1e-12);
    CHECK(std::abs(green.water - 2.0) < 1e-12);
    const auto optical = engine.opticalResult(green, watercolourProfile(), absorbentPaper());
    const unsigned r = (optical.straightArgb >> 16U) & 255U;
    const unsigned g = (optical.straightArgb >> 8U) & 255U;
    const unsigned b = optical.straightArgb & 255U;
    CHECK(g > r);
    CHECK(g > b);
    // Averaging saturated yellow and blue in RGB would produce a grey-blue;
    // this compact spectral/KM path produces a distinct green pigment mixture.
    CHECK(g > (r + b) / 2U + 15U);
    CHECK(optical.alpha > 0.0 && optical.alpha < 1.0);
}

void pigmentConcentrationWaterAndSubstrateChangeOpticalOutput()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    PaintBatch concentrated; concentrated.addPigment("PR122", 1.0); concentrated.addBinder(0.2);
    PaintBatch diluted = concentrated; diluted.addWater(8.0);
    const auto dense = engine.opticalResult(concentrated, watercolourProfile(), absorbentPaper());
    const auto wash = engine.opticalResult(diluted, watercolourProfile(), absorbentPaper());
    CHECK(dense.alpha > wash.alpha);
    CHECK(concentrated.pigmentConcentration() > diluted.pigmentConcentration());
    const auto onPaper = engine.composite(0, concentrated, gouacheProfile(), absorbentPaper(), 1.0, 1.0);
    const auto onPalette = engine.composite(0, concentrated, gouacheProfile(), nonAbsorbentPalette(), 1.0, 1.0);
    CHECK((onPaper >> 24U) != (onPalette >> 24U));
    CHECK(dense.alpha > 0.0);
}

void arbitraryRgbIsKeptAsUnidentifiedResidual()
{
    auto residual = makeResidualPaint(0xff29a4d8U, 0.8, 0.4, 0.1);
    CHECK(residual.valid());
    CHECK(residual.pigments.size() == 1);
    CHECK(residual.pigments.front().pigmentId.empty());
    CHECK(residual.pigments.front().residualArgb == 0xff29a4d8U);
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    const auto result = engine.opticalResult(residual, gouacheProfile(), absorbentPaper());
    CHECK(result.straightArgb != 0U);
}

void reservoirConservesMaterialAndModelsLoadPickupReloadAndCleaning()
{
    auto presets = defaultBrushDefinitions();
    CHECK(presets.size() >= 9);
    const auto gouache = std::find_if(presets.begin(), presets.end(), [](const auto& p) { return p.id == "gouache-round"; });
    const auto tiny = std::find_if(presets.begin(), presets.end(), [](const auto& p) { return p.id == "tiny-gouache"; });
    CHECK(gouache != presets.end() && tiny != presets.end());
    CHECK(gouache->initialPaint.pigments.size() == tiny->initialPaint.pigments.size());
    CHECK(gouache->initialPaint.pigmentMass() == tiny->initialPaint.pigmentMass());
    CHECK(gouache->initialPaint.water == tiny->initialPaint.water);
    CHECK(gouache->initialPaint.binder == tiny->initialPaint.binder);

    BrushReservoir reservoir(gouache->reservoirCapacity);
    CHECK(reservoir.load(gouache->initialPaint, gouache->reservoirCapacity * gouache->initialLoad));
    const double start = reservoir.quantity();
    PaintBatch portion = reservoir.withdraw(start * 0.4);
    CHECK(std::abs(portion.totalMass() + reservoir.quantity() - start) < 1e-9);
    CHECK(reservoir.quantity() < start);
    const double beforePickup = reservoir.quantity();
    const auto liftingIt = std::find_if(presets.begin(), presets.end(), [](const auto& p) { return p.id == "paint-lifting"; });
    CHECK(liftingIt != presets.end());
    CHECK(liftingIt != presets.end() && reservoir.pickupFromSurface(*liftingIt, portion, 1.0));
    CHECK(reservoir.quantity() > beforePickup);
    reservoir.setCapacity(start * 0.5);
    CHECK(reservoir.quantity() <= reservoir.capacity() + 1e-12);

    BrushReservoir reload(gouache->reservoirCapacity);
    auto reloadingBrush = *gouache;
    reloadingBrush.reloadThreshold = 0.10;
    CHECK(reload.load(reloadingBrush.initialPaint, reloadingBrush.reservoirCapacity * 0.08));
    CHECK(reload.reloadIfNeeded(reloadingBrush));
    CHECK(reload.fillRatio() > reloadingBrush.reloadThreshold);
    reload.clean(0.5, "watercolour");
    CHECK(reload.batch().pigmentMass() == 0.0);
    CHECK(reload.batch().water == 0.5);
    CHECK(reload.batch().mediumId == "watercolour");
}

void brushPressureSpeedAndSurfaceDabsAffectRealSparsePixels()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    const auto presets = defaultBrushDefinitions();
    const auto brushIt = std::find_if(presets.begin(), presets.end(), [](const auto& p) { return p.id == "gouache-round"; });
    CHECK(brushIt != presets.end());
    if (brushIt == presets.end()) return;
    BrushDefinition brush = *brushIt;
    CHECK(brush.valid());
    BrushReservoir low(brush.reservoirCapacity), high(brush.reservoirCapacity);
    CHECK(low.load(brush.initialPaint, brush.reservoirCapacity * brush.initialLoad));
    CHECK(high.load(brush.initialPaint, brush.reservoirCapacity * brush.initialLoad));
    BrushInput soft; soft.pressure = 0.2; soft.speed = 0.8;
    BrushInput firm; firm.pressure = 0.9; firm.speed = 0.2;
    const double smallTransfer = low.transferredAmount(brush, soft, 2.0);
    const double largeTransfer = high.transferredAmount(brush, firm, 2.0);
    CHECK(largeTransfer > smallTransfer);

    SparseTileStore drySurface(80, 80);
    SparseTileStore wetSurface(80, 80);
    MaterialDab paperEngine(engine, gouacheProfile(), absorbentPaper());
    brush.toothResponse = 0.0;
    const auto resultA = paperEngine.apply(drySurface, brush, low, 40.0, 40.0, firm);
    CHECK(resultA == SparseTileStore::WriteResult::Changed);
    CHECK(drySurface.pixel({40, 40}).value_or(0) != 0);
    const double afterA = low.quantity();
    CHECK(afterA < brush.reservoirCapacity * brush.initialLoad);

    PaintBatch wetRecipe = brush.initialPaint;
    wetRecipe.addWater(6.0);
    brush.waterSpread = 1.0;
    BrushReservoir wet(brush.reservoirCapacity * 2.0);
    CHECK(wet.load(wetRecipe, wetRecipe.totalMass()));
    const auto resultB = paperEngine.apply(wetSurface, brush, wet, 40.0, 40.0, firm);
    CHECK(resultB == SparseTileStore::WriteResult::Changed);
    CHECK(wetSurface.allocatedTiles() > 0);
    CHECK(wet.quantity() < wetRecipe.totalMass());
    CHECK(drySurface.pixel({34, 40}).value_or(0) != wetSurface.pixel({34, 40}).value_or(0));
}

void materialStrokeSpacesDabsAndStaysInsideSparseCanvas()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    auto presets = defaultBrushDefinitions();
    const auto it = std::find_if(presets.begin(), presets.end(), [](const auto& p) {
        return p.id == "tiny-gouache";
    });
    CHECK(it != presets.end());
    if (it == presets.end()) return;
    BrushDefinition brush = *it;
    brush.size = 3.0;
    brush.spacingRatio = 0.25;
    BrushReservoir reservoir(brush.reservoirCapacity);
    CHECK(reservoir.load(brush.initialPaint, brush.reservoirCapacity * brush.initialLoad));
    SparseTileStore surface(32, 16);
    MaterialStroke stroke(surface, brush, reservoir, engine, gouacheProfile(), absorbentPaper());
    CHECK(stroke.begin({2.0, 8.0, {1.0, 0.1, 0.0, 0.0}}));
    CHECK(stroke.extend({28.0, 8.0, {0.8, 0.3, 0.0, 0.0}}));
    CHECK(stroke.end({35.0, 8.0, {0.7, 0.4, 0.0, 0.0}}));
    CHECK(!stroke.failed());
    CHECK(surface.pixel({2, 8}).value_or(0) != 0);
    CHECK(surface.pixel({28, 8}).value_or(0) != 0);
    CHECK(surface.allocatedTiles() > 0);

    BrushReservoir invalidReservoir(brush.reservoirCapacity);
    CHECK(invalidReservoir.load(brush.initialPaint, brush.initialPaint.totalMass()));
    MaterialStroke invalidStroke(surface, brush, invalidReservoir, engine,
                                 gouacheProfile(), absorbentPaper());
    CHECK(!invalidStroke.begin({NAN, 2.0, {1.0, 0.0, 0.0, 0.0}}));
    CHECK(invalidStroke.failed());
    CHECK(invalidStroke.failureReason() == SparseTileStore::WriteResult::InvalidPixel);
}

void authoritativeSparseMaterialTilesSurviveStrokesAndRendering()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    auto presets = defaultBrushDefinitions();
    const auto found = std::find_if(presets.begin(), presets.end(), [](const auto& item) {
        return item.id == "gouache-round";
    });
    CHECK(found != presets.end());
    if (found == presets.end()) return;

    MaterialTileStore material(2048, 1024);
    const auto baseline = makeResidualPaint(0xffa54b35U, 1.0, 0.2, 0.15, "gouache");
    PaintBatch nearlySame = baseline;
    nearlySame.pigments.front().mass -= 1e-8;
    CHECK(nearlySame.addPigment("PY3", 1e-8));
    const auto baselineOptics = engine.opticalResult(baseline, gouacheProfile(), absorbentPaper());
    const auto nearlySameOptics = engine.opticalResult(nearlySame, gouacheProfile(), absorbentPaper());
    CHECK(baseline.pigments.size() != nearlySame.pigments.size());
    const auto channelDelta = [](std::uint32_t a, std::uint32_t b) {
        int sum = 0;
        for (unsigned shift : {0U, 8U, 16U, 24U})
            sum += std::abs(int((a >> shift) & 255U) - int((b >> shift) & 255U));
        return sum;
    };
    CHECK(channelDelta(baselineOptics.straightArgb, nearlySameOptics.straightArgb) <= 2);
    CHECK(material.setBatch({3, 4}, baseline) == MaterialTileStore::Result::Changed);
    CHECK(material.setBatch({4, 4}, nearlySame) == MaterialTileStore::Result::Changed);
    const auto cellA = material.cellAt({3, 4});
    const auto cellB = material.cellAt({4, 4});
    CHECK(cellA && cellB && cellA->compositionId != cellB->compositionId);
    CHECK(material.allocatedTiles() == 1);
    const auto storedA = material.batchAt({3, 4});
    const auto storedB = material.batchAt({4, 4});
    CHECK(storedA && storedB);
    CHECK(storedA && storedA->pigments.size() == 1);
    CHECK(storedB && storedB->pigments.size() == 2);

    BrushDefinition brush = *found;
    brush.size = 5.0;
    brush.spacingRatio = 0.3;
    BrushDefinition softEdge = brush, hardEdge = brush;
    softEdge.size = hardEdge.size = 10.0;
    softEdge.hardness = 0.0;
    hardEdge.hardness = 1.0;
    MaterialTileStore softSurface(64, 64, 64), hardSurface(64, 64, 64);
    BrushReservoir softReservoir(softEdge.reservoirCapacity), hardReservoir(hardEdge.reservoirCapacity);
    CHECK(softReservoir.load(softEdge.initialPaint, softEdge.reservoirCapacity * softEdge.initialLoad));
    CHECK(hardReservoir.load(hardEdge.initialPaint, hardEdge.reservoirCapacity * hardEdge.initialLoad));
    MaterialDab edgeDab(engine, gouacheProfile(), absorbentPaper());
    CHECK(edgeDab.apply(softSurface, softEdge, softReservoir, 32.0, 32.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    CHECK(edgeDab.apply(hardSurface, hardEdge, hardReservoir, 32.0, 32.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    const auto softRim = softSurface.batchAt({36, 32});
    const auto hardRim = hardSurface.batchAt({36, 32});
    CHECK(softRim && hardRim && hardRim->totalMass() > softRim->totalMass());

    BrushReservoir reservoir(brush.reservoirCapacity);
    CHECK(reservoir.load(brush.initialPaint, brush.reservoirCapacity * brush.initialLoad));
    MaterialStroke stroke(material, brush, reservoir, engine, gouacheProfile(), absorbentPaper());
    CHECK(stroke.begin({30.0, 20.0, {0.8, 0.2, 0.0, 0.0}}));
    CHECK(stroke.extend({38.0, 20.0, {0.7, 0.3, 0.0, 0.0}}));
    CHECK(stroke.end({42.0, 20.0, {0.7, 0.3, 0.0, 0.0}}));
    CHECK(!stroke.failed());
    const auto strokeCell = material.cellAt({30, 20});
    const auto afterStroke = material.batchAt({30, 20});
    CHECK(strokeCell && !strokeCell->empty());
    CHECK(afterStroke && afterStroke->pigmentMass() > 0.0);
    CHECK(afterStroke && afterStroke->mediumId == "gouache");

    const auto beforeRender = material.snapshot({0, 0});
    std::vector<std::uint32_t> digital(256U * 256U, 0U);
    digital[9U * 256U + 9U] = 0xff3470a0U;
    std::vector<std::uint32_t> rendered;
    MaterialRenderer renderer(engine);
    CHECK(renderer.renderTile(material, {0, 0}, digital, gouacheProfile(), absorbentPaper(), &rendered));
    CHECK(rendered.size() == digital.size());
    CHECK(rendered[9U * 256U + 9U] == digital[9U * 256U + 9U]);
    CHECK(rendered[20U * 256U + 30U] != digital[20U * 256U + 30U]);
    CHECK(material.snapshot({0, 0}) == beforeRender);
    const auto afterRender = material.batchAt({30, 20});
    CHECK(afterRender && afterStroke && afterRender->pigments.size() == afterStroke->pigments.size());
    CHECK(material.allocatedTiles() == 1);

    // The material eraser reduces quantities without inventing a new pigment
    // identity or rasterizing the underlying composition into RGB.
    const auto beforeErase = material.batchAt({30, 20});
    CHECK(beforeErase && beforeErase->pigmentMass() > 0.0);
    BrushReservoir emptyReservoir(brush.reservoirCapacity);
    MaterialStroke eraser(material, brush, emptyReservoir, engine,
                          gouacheProfile(), absorbentPaper(), true);
    CHECK(eraser.begin({30.0, 20.0, {1.0, 0.2, 0.0, 0.0}}));
    CHECK(eraser.end({30.0, 20.0, {1.0, 0.2, 0.0, 0.0}}));
    const auto afterErase = material.batchAt({30, 20});
    CHECK(afterErase && afterErase->pigmentMass() < beforeErase->pigmentMass());
    CHECK(afterErase && afterErase->pigments.size() == beforeErase->pigments.size());
    const auto erasedCell = material.cellAt({30, 20});
    CHECK(erasedCell && strokeCell && erasedCell->compositionId == strokeCell->compositionId);

    // Tile snapshots preserve their composition IDs and material amounts;
    // restore is local to the touched tile, not a canvas-wide operation.
    const auto beforeEdit = material.snapshot({0, 0});
    CHECK(material.setBatch({30, 20}, makeResidualPaint(0xff1839a0U, 2.0, 0.0, 0.1))
          == MaterialTileStore::Result::Changed);
    const auto changedComposition = material.cellAt({30, 20});
    CHECK(changedComposition && strokeCell && changedComposition->compositionId != strokeCell->compositionId);
    CHECK(material.restoreTiles(std::span<const MaterialTileSnapshot>(&beforeEdit, 1))
          == MaterialTileStore::Result::Changed);
    const auto restoredComposition = material.cellAt({30, 20});
    CHECK(restoredComposition && strokeCell && restoredComposition->compositionId == strokeCell->compositionId);

    const auto exportedCompositions = material.compositions();
    const auto exportedTile = material.snapshot({0, 0});
    MaterialTileStore reopened(2048, 1024);
    CHECK(reopened.importCompositions(exportedCompositions));
    CHECK(reopened.restoreTiles(std::span<const MaterialTileSnapshot>(&exportedTile, 1))
          == MaterialTileStore::Result::Changed);
    const auto reopenedPaint = reopened.batchAt({30, 20});
    CHECK(reopenedPaint && afterStroke && reopenedPaint->pigments.size() == afterStroke->pigments.size());
    CHECK(reopenedPaint && reopenedPaint->pigmentMass() > 0.0);
    CHECK(reopened.allocatedTiles() == 1);

    PaintBatch invalid = baseline;
    invalid.pigments.front().mass = std::numeric_limits<double>::quiet_NaN();
    CHECK(material.setBatch({1000, 900}, invalid) == MaterialTileStore::Result::InvalidData);
    auto budget = std::make_shared<DocumentMemoryBudget>(1);
    MaterialTileStore exhausted(16, 16, 16, budget);
    CHECK(exhausted.setBatch({1, 1}, baseline) == MaterialTileStore::Result::DocumentMemoryLimit);
    CHECK(exhausted.allocatedTiles() == 0);

    // Local renderer work is proportional to the requested sparse tile; distant
    // untouched regions do not allocate material or trigger full-canvas work.
    CHECK(material.allocatedTilePoints().size() == 1);
    CHECK(material.dirtyRegions().size() == 1);
}

void wetMaterialDriesSettlesSpreadsAndBecomesInactiveLazily()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    MaterialTileStore store(96, 96, 16);
    PaintBatch wash;
    wash.mediumId = "watercolour";
    wash.addPigment("PBr7", 1.0);
    wash.addWater(4.0);
    wash.addBinder(0.1);
    wash.drying = 0.08;
    CHECK(store.setBatch({7, 7}, wash) == MaterialTileStore::Result::Changed);
    PaintBatch dry = makeResidualPaint(0xff3a5268U, 1.0, 0.0, 0.2, "gouache");
    dry.drying = 1.0;
    CHECK(store.setBatch({70, 70}, dry) == MaterialTileStore::Result::Changed);
    CHECK(store.activeTilePoints().size() == 1);

    const auto totalStoredPigment = [&store] {
        double total = 0.0;
        for (const TilePoint tile : store.allocatedTilePoints())
            for (const auto& entry : store.tileEntries(tile)) total += entry.cell.pigmentMass;
        return total;
    };
    const double originalPigment = totalStoredPigment();
    const auto stats = engine.advanceWetMaterial(store, 30.0, roughPaper());
    const auto evolved = store.batchAt({7, 7});
    CHECK(!stats.failed);
    CHECK(stats.activeTiles == 1);
    CHECK(stats.cellsVisited > 0);
    CHECK(stats.cellsChanged > 0);
    CHECK(evolved && evolved->water < wash.water);
    CHECK(evolved && evolved->drying > wash.drying);
    CHECK(evolved && evolved->settling > 0.0);
    CHECK(std::abs(totalStoredPigment() - originalPigment) < 1e-4);
    CHECK(stats.transfers > 0);
    const auto rightSpread = store.batchAt({8, 7});
    const auto lowerSpread = store.batchAt({7, 8});
    CHECK((rightSpread && rightSpread->totalMass() > 0.0)
          || (lowerSpread && lowerSpread->totalMass() > 0.0));

    for (int i = 0; i < 40 && !store.activeTilePoints().empty(); ++i)
        CHECK(!engine.advanceWetMaterial(store, 60.0, roughPaper()).failed);
    const auto dried = store.batchAt({7, 7});
    CHECK(dried && dried->water == 0.0);
    CHECK(dried && dried->drying == 1.0);
    CHECK(store.activeTilePoints().empty());
    CHECK(store.batchAt({70, 70})->drying == 1.0);
}

void activeTileIndexTracksWetWritesDryingAndHistoryRestore()
{
    MaterialTileStore store(64, 64, 16);
    PaintBatch wet = makeResidualPaint(0xff5381a4U, 1.0, 2.0, 0.1, "watercolour");
    CHECK(store.setBatch({20, 20}, wet) == MaterialTileStore::Result::Changed);
    const std::vector<TilePoint> expectedActive{{1, 1}};
    CHECK(store.activeTilePoints() == expectedActive);
    const auto snapshot = store.snapshot({1, 1});
    PaintBatch dry = wet;
    dry.water = 0.0;
    dry.wetness = 0.0;
    dry.drying = 1.0;
    CHECK(store.setBatch({20, 20}, dry) == MaterialTileStore::Result::Changed);
    CHECK(store.activeTilePoints().empty());
    const std::vector<MaterialTileSnapshot> snapshots{snapshot};
    CHECK(store.restoreTiles(snapshots) == MaterialTileStore::Result::Changed);
    CHECK(store.activeTilePoints() == expectedActive);
    CHECK(store.erase({20, 20}) == MaterialTileStore::Result::Changed);
    CHECK(store.activeTilePoints().empty());
}

void paperProfilesChangeDepositedMaterialNotOnlyRenderedTexture()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    auto presets = defaultBrushDefinitions();
    const auto it = std::find_if(presets.begin(), presets.end(), [](const auto& brush) {
        return brush.id == "watercolour-round";
    });
    CHECK(it != presets.end());
    if (it == presets.end()) return;
    BrushDefinition brush = *it;
    brush.size = 12.0;
    brush.releaseRate = 0.6;
    brush.depletionRate = 1.0;
    BrushInput input{1.0, 0.1, 0.0, 0.0};
    MaterialTileStore hot(64, 64, 64), cold(64, 64, 64), rough(64, 64, 64);
    BrushReservoir hotReservoir(20.0), coldReservoir(20.0), roughReservoir(20.0);
    CHECK(hotReservoir.load(brush.initialPaint, brush.initialPaint.totalMass()));
    CHECK(coldReservoir.load(brush.initialPaint, brush.initialPaint.totalMass()));
    CHECK(roughReservoir.load(brush.initialPaint, brush.initialPaint.totalMass()));
    MaterialDab hotDab(engine, watercolourProfile(), hotPressedPaper());
    MaterialDab coldDab(engine, watercolourProfile(), coldPressedPaper());
    MaterialDab roughDab(engine, watercolourProfile(), roughPaper());
    CHECK(hotDab.apply(hot, brush, hotReservoir, 32.0, 32.0, input) == SparseTileStore::WriteResult::Changed);
    CHECK(coldDab.apply(cold, brush, coldReservoir, 32.0, 32.0, input) == SparseTileStore::WriteResult::Changed);
    CHECK(roughDab.apply(rough, brush, roughReservoir, 32.0, 32.0, input) == SparseTileStore::WriteResult::Changed);
    const auto hotBatch = hot.batchAt({32, 32});
    const auto coldBatch = cold.batchAt({32, 32});
    const auto roughBatch = rough.batchAt({32, 32});
    CHECK(hotBatch && coldBatch && roughBatch);
    CHECK(hotBatch && coldBatch && hotBatch->water != coldBatch->water);
    CHECK(coldBatch && roughBatch && coldBatch->pigmentMass() != roughBatch->pigmentMass());
    const auto hotPixel = engine.composite(0U, *hotBatch, watercolourProfile(), hotPressedPaper(), 1.0, 4096.0);
    const auto roughPixel = engine.composite(0U, *roughBatch, watercolourProfile(), roughPaper(), 1.0, 4096.0);
    CHECK(hotPixel != roughPixel);
}

void brushPickupRewettingAndMixerLoadsRetainPigmentProportions()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    PaintBatch surfacePaint;
    surfacePaint.mediumId = "watercolour";
    surfacePaint.addPigment("PY3", 2.0);
    surfacePaint.addPigment("PB15", 1.0);
    surfacePaint.addWater(2.5);
    surfacePaint.addBinder(0.1);
    MaterialTileStore surface(64, 64, 64);
    CHECK(surface.setBatch({32, 32}, surfacePaint) == MaterialTileStore::Result::Changed);

    auto presets = defaultBrushDefinitions();
    const auto preset = std::find_if(presets.begin(), presets.end(), [](const auto& brush) {
        return brush.id == "watercolour-round";
    });
    CHECK(preset != presets.end());
    if (preset == presets.end()) return;
    BrushDefinition brush = *preset;
    brush.size = 18.0;
    brush.reservoirCapacity = 30.0;
    brush.pickupRate = 1.0;
    brush.paintLiftability = 1.0;
    brush.rewetting = 1.0;
    brush.releaseRate = 0.0;
    BrushReservoir reservoir(brush.reservoirCapacity);
    PaintBatch loaded = mix(makeResidualPaint(0xffd9442aU, 1.0, 2.0, 0.2, "watercolour"),
                            surfacePaint, "watercolour");
    CHECK(reservoir.load(loaded, loaded.totalMass()));
    const auto originalComposition = reservoir.batch().pigments;
    const auto before = surface.batchAt({32, 32});
    MaterialDab rework(engine, watercolourProfile(), coldPressedPaper());
    CHECK(rework.apply(surface, brush, reservoir, 32.0, 32.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    const auto after = surface.batchAt({32, 32});
    CHECK(before && after && after->totalMass() < before->totalMass());
    CHECK(reservoir.batch().pigments.size() >= originalComposition.size());
    CHECK(std::any_of(reservoir.batch().pigments.begin(), reservoir.batch().pigments.end(),
        [](const auto& pigment) { return pigment.pigmentId == "PY3"; }));
    CHECK(std::any_of(reservoir.batch().pigments.begin(), reservoir.batch().pigments.end(),
        [](const auto& pigment) { return pigment.pigmentId == "PB15"; }));

    // A dried surface remains stable until water-bearing paint contacts it;
    // rewetting can mobilize only a liftable fraction and does not relabel the
    // original pigment composition as the brush's selected RGB.
    PaintBatch dry = surfacePaint;
    dry.water = 0.0;
    dry.drying = 1.0;
    dry.wetness = 0.0;
    CHECK(surface.setBatch({40, 40}, dry) == MaterialTileStore::Result::Changed);
    BrushReservoir liftingReservoir(30.0);
    PaintBatch wetBrush = makeResidualPaint(0xffe4d8afU, 0.2, 5.0, 0.0, "watercolour");
    CHECK(liftingReservoir.load(wetBrush, wetBrush.totalMass()));
    const double beforeLift = surface.batchAt({40, 40})->totalMass();
    CHECK(rework.apply(surface, brush, liftingReservoir, 40.0, 40.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    CHECK(surface.batchAt({40, 40})->totalMass() < beforeLift);
    CHECK(std::any_of(liftingReservoir.batch().pigments.begin(), liftingReservoir.batch().pigments.end(),
        [](const auto& pigment) { return pigment.pigmentId == "PY3"; }));
}

void namedProfilesAndPhysicalPigmentPropertiesArePresent()
{
    PigmentLibrary library;
    CHECK(library.find("PY3") != nullptr);
    CHECK(library.find("PB15") != nullptr);
    CHECK(library.find("PBr7") != nullptr);
    CHECK(library.find("PBr7")->particleSizeMicrons > library.find("PB15")->particleSizeMicrons);
    CHECK(library.find("PBr7")->physicalDensity != library.find("PB15")->physicalDensity);
    CHECK(gouacheProfile().viscosity > watercolourProfile().viscosity);
    CHECK(watercolourProfile().waterSpread > gouacheProfile().waterSpread);
    CHECK(nonAbsorbentPalette().paletteNonAbsorbency > absorbentPaper().paletteNonAbsorbency);
}

void spectralPigmentsMatchMagentaPurpleAndPreserveTintAlpha()
{
    PigmentLibrary library;
    MaterialEngine engine(library);

    PaintBatch magenta;
    magenta.mediumId = "gouache";
    magenta.addPigment("PR122", 1.0);
    magenta.addBinder(0.2);
    const auto magentaOpt = engine.opticalResult(magenta, gouacheProfile(), absorbentPaper());
    const unsigned mr = (magentaOpt.straightArgb >> 16U) & 255U;
    const unsigned mg = (magentaOpt.straightArgb >> 8U) & 255U;
    const unsigned mb = magentaOpt.straightArgb & 255U;
    CHECK(mr > 180U);
    CHECK(mb > mg + 10U);
    CHECK(mr > mb);

    PaintBatch blue;
    blue.mediumId = "gouache";
    blue.addPigment("PB15", 1.0);
    blue.addBinder(0.2);
    const PaintBatch purple = mix(magenta, blue, "gouache");
    const auto purpleOpt = engine.opticalResult(purple, gouacheProfile(), absorbentPaper());
    const unsigned pr = (purpleOpt.straightArgb >> 16U) & 255U;
    const unsigned pg = (purpleOpt.straightArgb >> 8U) & 255U;
    const unsigned pb = purpleOpt.straightArgb & 255U;
    CHECK(pr > pg + 15U);
    CHECK(pb > pg + 25U);

    // A heavily diluted watercolour wash must remain translucent even after its
    // water evaporates into a dry glaze.
    PaintBatch wetTint;
    wetTint.mediumId = "watercolour";
    wetTint.addPigment("PR122", 0.05);
    wetTint.addWater(5.0);
    wetTint.addBinder(0.02);
    const auto wetOpt = engine.opticalResult(wetTint, watercolourProfile(), absorbentPaper(), 4.0);
    PaintBatch dryTint = wetTint;
    dryTint.water = 0.0;
    dryTint.wetness = 0.0;
    dryTint.drying = 1.0;
    const auto dryOpt = engine.opticalResult(dryTint, watercolourProfile(), absorbentPaper(), 4.0);
    CHECK(wetOpt.alpha < 0.65);
    CHECK(dryOpt.alpha < 0.65);
}

void dryingReclaimsCompositionsAndRespectsUndoPins()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    auto presets = defaultBrushDefinitions();
    const auto it = std::find_if(presets.begin(), presets.end(), [](const auto& b) {
        return b.id == "watercolour-round";
    });
    CHECK(it != presets.end());
    if (it == presets.end()) return;

    BrushDefinition brush = *it;
    brush.size = 14.0;
    MaterialTileStore store(128, 128, 32);
    BrushReservoir reservoir(brush.reservoirCapacity);
    MaterialStroke stroke(store, brush, reservoir, engine, watercolourProfile(), coldPressedPaper());
    CHECK(stroke.begin({20.0, 20.0, {0.8, 0.2, 0.0, 0.0}}));
    CHECK(stroke.extend({60.0, 20.0, {0.8, 0.2, 0.0, 0.0}}));
    CHECK(stroke.end({60.0, 20.0, {0.8, 0.2, 0.0, 0.0}}));

    const std::size_t afterStrokeComps = store.activeCompositionCount();
    CHECK(afterStrokeComps > 0);

    // Pin a snapshot as UndoStack does before drying advances.
    const auto pinnedSnap = store.snapshot({0, 0});
    const std::vector<MaterialTileSnapshot> pinned{pinnedSnap};
    store.pinSnapshots(pinned);

    for (int step = 0; step < 30 && !store.activeTilePoints().empty(); ++step) {
        const auto stats = engine.advanceWetMaterial(store, 0.2, coldPressedPaper());
        CHECK(!stats.failed);
    }

    // Pure evaporation / bounded capillary spread should not explode composition storage.
    const std::size_t afterDryingComps = store.activeCompositionCount();
    CHECK(afterDryingComps < afterStrokeComps * 4 + 32);

    // Unpinning and reclaiming drops any unreferenced historical compositions.
    store.unpinSnapshots(pinned);
    store.reclaimUnreferenced();
    CHECK(store.activeCompositionCount() <= afterDryingComps);
}

void materialStrokeAutoReloadsCleanMixtureAndScalesBySize()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    auto presets = defaultBrushDefinitions();
    const auto mopIt = std::find_if(presets.begin(), presets.end(), [](const auto& b) {
        return b.id == "watercolour-mop";
    });
    const auto roundIt = std::find_if(presets.begin(), presets.end(), [](const auto& b) {
        return b.id == "watercolour-round";
    });
    CHECK(mopIt != presets.end() && roundIt != presets.end());
    if (mopIt == presets.end() || roundIt == presets.end()) return;

    // Seed surface with contrasting PY3 paint to verify pickup does not leak
    // into subsequent auto-reloaded strokes.
    MaterialTileStore store(256, 256, 64);
    PaintBatch yellowSurface;
    yellowSurface.mediumId = "watercolour";
    yellowSurface.addPigment("PY3", 1.0);
    yellowSurface.addWater(2.0);
    yellowSurface.addBinder(0.1);
    for (int y = 28; y <= 36; ++y)
        for (int x = 28; x <= 36; ++x)
            CHECK(store.setBatch({x, y}, yellowSurface) == MaterialTileStore::Result::Changed);

    BrushDefinition brush = *roundIt;
    brush.size = 12.0;
    brush.pickupRate = 0.8;
    brush.rewetting = 0.8;
    brush.initialPaint = PaintBatch{};
    brush.initialPaint.mediumId = "watercolour";
    brush.initialPaint.addPigment("PR122", 1.0);
    brush.initialPaint.addWater(2.0);
    brush.initialPaint.addBinder(0.1);
    brush.autoReloadOnStroke = true;

    BrushReservoir reservoir(brush.reservoirCapacity);
    MaterialStroke firstStroke(store, brush, reservoir, engine, watercolourProfile(), coldPressedPaper());
    CHECK(firstStroke.begin({32.0, 32.0, {0.9, 0.1, 0.0, 0.0}}));
    CHECK(firstStroke.end({34.0, 32.0, {0.9, 0.1, 0.0, 0.0}}));
    CHECK(std::any_of(reservoir.batch().pigments.begin(), reservoir.batch().pigments.end(),
        [](const auto& p) { return p.pigmentId == "PY3"; }));

    // A new stroke on a clean area automatically reloads clean PR122 without
    // carrying PY3 contamination from the previous stroke.
    MaterialStroke secondStroke(store, brush, reservoir, engine, watercolourProfile(), coldPressedPaper());
    CHECK(secondStroke.begin({160.0, 160.0, {0.9, 0.1, 0.0, 0.0}}));
    const auto cleanSpotBeforeEnd = store.batchAt({160, 160});
    CHECK(cleanSpotBeforeEnd && cleanSpotBeforeEnd->pigments.size() == 1);
    CHECK(cleanSpotBeforeEnd && cleanSpotBeforeEnd->pigments.front().pigmentId == "PR122");
    CHECK(secondStroke.end({180.0, 160.0, {0.9, 0.1, 0.0, 0.0}}));

    // A large mop brush must sustain a long stroke without emptying in 4 dabs,
    // and light pressure must remain visible on the rendered canvas.
    BrushDefinition mop = *mopIt;
    mop.autoReloadOnStroke = true;
    BrushReservoir mopReservoir(mop.reservoirCapacity);
    MaterialTileStore mopStore(512, 128, 64);
    MaterialStroke longStroke(mopStore, mop, mopReservoir, engine, watercolourProfile(), coldPressedPaper());
    CHECK(longStroke.begin({40.0, 64.0, {0.8, 0.2, 0.0, 0.0}}));
    CHECK(longStroke.extend({240.0, 64.0, {0.8, 0.2, 0.0, 0.0}}));
    CHECK(longStroke.end({440.0, 64.0, {0.8, 0.2, 0.0, 0.0}}));
    CHECK(mopReservoir.fillRatio() > 0.05);
    const auto endCell = mopStore.batchAt({420, 64});
    CHECK(endCell && endCell->pigmentMass() > 0.0);

    BrushDefinition lightBrush = *roundIt;
    lightBrush.size = 12.0;
    lightBrush.initialPaint = PaintBatch{};
    lightBrush.initialPaint.mediumId = "watercolour";
    lightBrush.initialPaint.addPigment("PR122", 1.0);
    lightBrush.initialPaint.addWater(2.0);
    lightBrush.initialPaint.addBinder(0.1);
    lightBrush.autoReloadOnStroke = true;
    MaterialTileStore lightStore(64, 64, 64);
    BrushReservoir lightReservoir(lightBrush.reservoirCapacity);
    MaterialStroke lightStroke(lightStore, lightBrush, lightReservoir, engine, watercolourProfile(), coldPressedPaper());
    CHECK(lightStroke.begin({32.0, 32.0, {0.08, 0.0, 0.0, 0.0}}));
    CHECK(lightStroke.end({32.0, 32.0, {0.08, 0.0, 0.0, 0.0}}));
    std::vector<std::uint32_t> rendered;
    MaterialRenderer renderer(engine);
    CHECK(renderer.renderTile(lightStore, {0, 0}, {}, watercolourProfile(), coldPressedPaper(), &rendered));
    const std::uint32_t centerPixel = rendered[32 * 64 + 32];
    CHECK((centerPixel >> 24U) >= 15U);
}

void gouacheRoundCoversOpaquelyDepletesSizeIndependentlyAndReworks()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    MaterialRenderer renderer(engine);
    auto presets = defaultBrushDefinitions();
    const auto gouacheIt = std::find_if(presets.begin(), presets.end(), [](const auto& b) {
        return b.id == "gouache-round";
    });
    CHECK(gouacheIt != presets.end());
    if (gouacheIt == presets.end()) return;

    // 1. Gouache Round produces opaque matte coverage over an underlying colour
    //    and auto-reloads cleanly on every new stroke.
    BrushDefinition gouache = *gouacheIt;
    CHECK(gouache.autoReloadOnStroke);
    CHECK(gouache.paintBody > 0.6);
    MaterialTileStore store(512, 128, 64);
    BrushReservoir reservoir(gouache.effectiveReservoirCapacity());
    MaterialStroke stroke(store, gouache, reservoir, engine, gouacheProfile(), coldPressedPaper());
    CHECK(stroke.begin({20.0, 32.0, {1.0, 0.1, 0.0, 0.0}}));
    CHECK(stroke.extend({60.0, 32.0, {1.0, 0.1, 0.0, 0.0}}));
    CHECK(stroke.end({60.0, 32.0, {1.0, 0.1, 0.0, 0.0}}));

    std::vector<std::uint32_t> digitalUnder(64 * 64, 0xff102080U);
    std::vector<std::uint32_t> composited;
    CHECK(renderer.renderTile(store, {0, 0}, digitalUnder, gouacheProfile(), coldPressedPaper(), &composited));
    const std::uint32_t paintedPx = composited[32 * 64 + 40];
    const unsigned r = (paintedPx >> 16U) & 255U;
    const unsigned g = (paintedPx >> 8U) & 255U;
    const unsigned b = paintedPx & 255U;
    CHECK((paintedPx >> 24U) == 255U);
    // Underlying dark blue (#102080) is covered by warm PY3+PW6 gouache.
    CHECK(r > 150U && g > 150U && b < 120U);

    // 2. Brush-size-independent depletion over equal 400px strokes (sizes 8, 18, 54).
    std::vector<double> finalFillRatios;
    for (double testSize : {8.0, 18.0, 54.0}) {
        BrushDefinition sized = *gouacheIt;
        sized.size = testSize;
        MaterialTileStore sizedStore(512, 128, 64);
        BrushReservoir sizedRes(sized.effectiveReservoirCapacity());
        MaterialStroke sizedStroke(sizedStore, sized, sizedRes, engine, gouacheProfile(), coldPressedPaper());
        CHECK(sizedStroke.begin({40.0, 64.0, {0.85, 0.1, 0.0, 0.0}}));
        CHECK(sizedStroke.extend({240.0, 64.0, {0.85, 0.1, 0.0, 0.0}}));
        CHECK(sizedStroke.end({440.0, 64.0, {0.85, 0.1, 0.0, 0.0}}));
        const double fill = sizedRes.fillRatio();
        CHECK(fill > 0.10 && fill < 0.75);
        finalFillRatios.push_back(fill);
        const auto sampleBatch = sizedStore.batchAt({240, 64});
        CHECK(sampleBatch && sampleBatch->pigmentMass() > 0.0);
    }
    const auto [minFill, maxFill] = std::minmax_element(finalFillRatios.begin(), finalFillRatios.end());
    CHECK(*maxFill - *minFill < 0.12);

    // 3. Reworking & pickup on an existing wet/rewettable surface, followed by
    //    automatic clean mixture reload on the next stroke without carry-over.
    MaterialTileStore reworkStore(256, 128, 64);
    PaintBatch blueSurface;
    blueSurface.mediumId = "gouache";
    blueSurface.addPigment("PB15", 1.2);
    blueSurface.addWater(1.2);
    blueSurface.addBinder(0.3);
    for (int y = 26; y <= 38; ++y)
        for (int x = 26; x <= 38; ++x)
            CHECK(reworkStore.setBatch({x, y}, blueSurface) == MaterialTileStore::Result::Changed);
    BrushDefinition pickupGouache = *gouacheIt;
    pickupGouache.pickupRate = 0.65;
    pickupGouache.rewetting = 0.65;
    pickupGouache.paintLiftability = 0.75;
    pickupGouache.autoReloadOnStroke = true;
    BrushReservoir reworkRes(pickupGouache.effectiveReservoirCapacity());
    MaterialStroke pickStroke(reworkStore, pickupGouache, reworkRes, engine, gouacheProfile(), coldPressedPaper());
    CHECK(pickStroke.begin({32.0, 32.0, {0.9, 0.0, 0.0, 0.0}}));
    CHECK(pickStroke.end({34.0, 32.0, {0.9, 0.0, 0.0, 0.0}}));
    CHECK(std::any_of(reworkRes.batch().pigments.begin(), reworkRes.batch().pigments.end(),
        [](const auto& p) { return p.pigmentId == "PB15"; }));

    MaterialStroke cleanStroke(reworkStore, pickupGouache, reworkRes, engine, gouacheProfile(), coldPressedPaper());
    CHECK(cleanStroke.begin({180.0, 64.0, {0.9, 0.0, 0.0, 0.0}}));
    CHECK(cleanStroke.end({180.0, 64.0, {0.9, 0.0, 0.0, 0.0}}));
    const auto reloadedBatch = reworkStore.batchAt({180, 64});
    CHECK(reloadedBatch && !std::any_of(reloadedBatch->pigments.begin(), reloadedBatch->pigments.end(),
        [](const auto& p) { return p.pigmentId == "PB15"; }));
    CHECK(std::abs(reworkRes.fillRatio() - pickupGouache.initialLoad) < 0.08);
}

void brushPropertiesIndependentlyAffectMaterialSimulation()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    auto presets = defaultBrushDefinitions();
    const auto gouacheIt = std::find_if(presets.begin(), presets.end(), [](const auto& b) {
        return b.id == "gouache-round";
    });
    CHECK(gouacheIt != presets.end());
    if (gouacheIt == presets.end()) return;

    // 1. Pressure-to-amount and pressure-to-contact act independently.
    BrushDefinition amountOnly = *gouacheIt;
    amountOnly.size = 20.0;
    amountOnly.hardness = 0.0;
    amountOnly.pressureTransfer = 0.90;
    amountOnly.contactPressureScale = 0.0;
    MaterialTileStore amtLowStore(64, 64, 64), amtHighStore(64, 64, 64);
    BrushReservoir amtLowRes(amountOnly.effectiveReservoirCapacity()), amtHighRes(amountOnly.effectiveReservoirCapacity());
    CHECK(amtLowRes.reload(amountOnly) && amtHighRes.reload(amountOnly));
    MaterialDab dab(engine, gouacheProfile(), coldPressedPaper());
    CHECK(dab.apply(amtLowStore, amountOnly, amtLowRes, 32.0, 32.0, {0.15, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    CHECK(dab.apply(amtHighStore, amountOnly, amtHighRes, 32.0, 32.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    const auto amtLowCenter = amtLowStore.batchAt({32, 32});
    const auto amtHighCenter = amtHighStore.batchAt({32, 32});
    CHECK(amtLowCenter && amtHighCenter && amtHighCenter->pigmentMass() > amtLowCenter->pigmentMass() * 1.3);
    // Same contact footprint when contactPressureScale == 0.
    CHECK(amtLowStore.batchAt({41, 32})->totalMass() > 0.0 && amtHighStore.batchAt({41, 32})->totalMass() > 0.0);

    BrushDefinition contactOnly = *gouacheIt;
    contactOnly.size = 20.0;
    contactOnly.hardness = 0.0;
    contactOnly.pressureTransfer = 0.0;
    contactOnly.contactPressureScale = 1.0;
    MaterialTileStore cntLowStore(64, 64, 64), cntHighStore(64, 64, 64);
    BrushReservoir cntLowRes(contactOnly.effectiveReservoirCapacity()), cntHighRes(contactOnly.effectiveReservoirCapacity());
    CHECK(cntLowRes.reload(contactOnly) && cntHighRes.reload(contactOnly));
    CHECK(dab.apply(cntLowStore, contactOnly, cntLowRes, 32.0, 32.0, {0.10, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    CHECK(dab.apply(cntHighStore, contactOnly, cntHighRes, 32.0, 32.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    // Low pressure shrinks contact radius, leaving outer ring untouched while high pressure covers it.
    CHECK(cntLowStore.batchAt({40, 32})->totalMass() == 0.0);
    CHECK(cntHighStore.batchAt({40, 32})->totalMass() > 0.0);

    // 2. Hardness does not shrink brush radius.
    BrushDefinition hardBrush = *gouacheIt;
    hardBrush.size = 20.0;
    hardBrush.hardness = 0.90;
    MaterialTileStore hardStore(64, 64, 64);
    BrushReservoir hardRes(hardBrush.effectiveReservoirCapacity());
    CHECK(hardRes.reload(hardBrush));
    CHECK(dab.apply(hardStore, hardBrush, hardRes, 32.0, 32.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    CHECK(hardStore.batchAt({40, 32})->totalMass() > 0.0);

    // 3. Follow-direction rotates elliptical tip along stroke axis.
    BrushDefinition dirBrush = *gouacheIt;
    dirBrush.size = 24.0;
    dirBrush.aspect = 2.5;
    dirBrush.followDirection = true;
    MaterialTileStore horizStore(128, 128, 64), vertStore(128, 128, 64);
    BrushReservoir horizRes(dirBrush.effectiveReservoirCapacity()), vertRes(dirBrush.effectiveReservoirCapacity());
    MaterialStroke horizStroke(horizStore, dirBrush, horizRes, engine, gouacheProfile(), coldPressedPaper());
    CHECK(horizStroke.begin({64.0, 64.0, {1.0, 0.0, 0.0, 0.0}}));
    CHECK(horizStroke.end({84.0, 64.0, {1.0, 0.0, 0.0, 0.0}}));
    MaterialStroke vertStroke(vertStore, dirBrush, vertRes, engine, gouacheProfile(), coldPressedPaper());
    CHECK(vertStroke.begin({64.0, 64.0, {1.0, 0.0, 0.0, 0.0}}));
    CHECK(vertStroke.end({64.0, 84.0, {1.0, 0.0, 0.0, 0.0}}));
    // Vertical stroke stretches along Y (`{64, 84}` has wide X reach of ry=4.8 vs horizontal reach rx=12).
    CHECK(horizStore.batchAt({84, 72})->totalMass() == 0.0);
    CHECK(vertStore.batchAt({72, 84})->totalMass() > 0.0);

    // 4. Texture strength, tilt flattening, paintBody spread damping, and dryingRate
    //    each alter actual deposited MaterialTileStore state.
    BrushDefinition textured = *gouacheIt;
    textured.size = 18.0;
    textured.hardness = 1.0;
    textured.textureStrength = 0.9;
    MaterialTileStore texStore(64, 64, 64);
    BrushReservoir texRes(textured.effectiveReservoirCapacity());
    CHECK(texRes.reload(textured));
    CHECK(dab.apply(texStore, textured, texRes, 32.0, 32.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    CHECK(texStore.batchAt({32, 32})->pigmentMass() != texStore.batchAt({33, 32})->pigmentMass());

    BrushDefinition tilted = *gouacheIt;
    tilted.size = 18.0;
    tilted.tiltFlattening = 0.85;
    MaterialTileStore noTiltStore(64, 64, 64), tiltStore(64, 64, 64);
    BrushReservoir noTiltRes(tilted.effectiveReservoirCapacity()), tiltRes(tilted.effectiveReservoirCapacity());
    CHECK(noTiltRes.reload(tilted) && tiltRes.reload(tilted));
    CHECK(dab.apply(noTiltStore, tilted, noTiltRes, 32.0, 32.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    CHECK(dab.apply(tiltStore, tilted, tiltRes, 32.0, 32.0, {1.0, 0.0, 0.9, 0.0})
          == SparseTileStore::WriteResult::Changed);
    CHECK(noTiltStore.batchAt({44, 32})->totalMass() == 0.0);
    CHECK(tiltStore.batchAt({44, 32})->totalMass() > 0.0);

    BrushDefinition fluid = *gouacheIt;
    fluid.mediumId = "watercolour";
    fluid.initialPaint.mediumId = "watercolour";
    fluid.initialPaint.addWater(3.0);
    fluid.waterSpread = 0.9;
    fluid.paintBody = 0.0;
    BrushDefinition stiff = fluid;
    stiff.paintBody = 1.0;
    MaterialTileStore fluidStore(64, 64, 64), stiffStore(64, 64, 64);
    BrushReservoir fluidRes(fluid.effectiveReservoirCapacity()), stiffRes(stiff.effectiveReservoirCapacity());
    CHECK(fluidRes.reload(fluid) && stiffRes.reload(stiff));
    MaterialDab wcDab(engine, watercolourProfile(), coldPressedPaper());
    CHECK(wcDab.apply(fluidStore, fluid, fluidRes, 32.0, 32.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    CHECK(wcDab.apply(stiffStore, stiff, stiffRes, 32.0, 32.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    // Low-body paint spreads further outward than high-body stiff paint.
    CHECK(fluidStore.batchAt({45, 32})->totalMass() > 0.0);
    CHECK(stiffStore.batchAt({45, 32})->totalMass() == 0.0);

    BrushDefinition slowDry = *gouacheIt;
    BrushDefinition fastDry = *gouacheIt;
    fastDry.dryingRate = 2.5;
    MaterialTileStore slowDryStore(64, 64, 64), fastDryStore(64, 64, 64);
    BrushReservoir slowDryRes(slowDry.effectiveReservoirCapacity()), fastDryRes(fastDry.effectiveReservoirCapacity());
    CHECK(slowDryRes.reload(slowDry) && fastDryRes.reload(fastDry));
    CHECK(dab.apply(slowDryStore, slowDry, slowDryRes, 32.0, 32.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    CHECK(dab.apply(fastDryStore, fastDry, fastDryRes, 32.0, 32.0, {1.0, 0.0, 0.0, 0.0})
          == SparseTileStore::WriteResult::Changed);
    CHECK(fastDryStore.batchAt({32, 32})->drying > slowDryStore.batchAt({32, 32})->drying);
}

void stage6SubstrateProfilesAndWetOnWetVsWetOnDry()
{
    const auto profiles = defaultSurfaceProfiles();
    CHECK(profiles.size() >= 5);
    const auto* hot = findSurfaceProfile("hot-press");
    const auto* cold = findSurfaceProfile("cold-press");
    const auto* rough = findSurfaceProfile("rough-paper");
    const auto* smooth = findSurfaceProfile("smooth-illustration");
    const auto* textured = findSurfaceProfile("textured-illustration");
    CHECK(hot && cold && rough && smooth && textured);
    if (!hot || !cold || !rough || !smooth || !textured) return;

    CHECK(hot->tooth < cold->tooth && cold->tooth < rough->tooth);
    CHECK(hot->absorptionRate < cold->absorptionRate && cold->absorptionRate < rough->absorptionRate);
    CHECK(hot->granulationInteraction < cold->granulationInteraction && cold->granulationInteraction < rough->granulationInteraction);
    CHECK(smooth->tooth < textured->tooth);
    CHECK(smooth->roughness < textured->roughness);

    // Heightfield is deterministic, bounded in [0, 1], and varies across tooth peaks/valleys.
    double minTooth = 1.0, maxTooth = 0.0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const double h = surfaceToothHeightAt(x, y, *rough);
            CHECK(h >= 0.0 && h <= 1.0);
            minTooth = std::min(minTooth, h);
            maxTooth = std::max(maxTooth, h);
        }
    }
    CHECK(maxTooth - minTooth > 0.35);

    // Wet-on-wet vs wet-on-dry behaviour via SurfaceProfile::surfaceWetness.
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    auto presets = defaultBrushDefinitions();
    const auto roundIt = std::find_if(presets.begin(), presets.end(), [](const auto& b) { return b.id == "watercolour-round"; });
    CHECK(roundIt != presets.end());
    if (roundIt == presets.end()) return;

    SurfaceProfile drySurface = *cold;
    drySurface.surfaceWetness = 0.0;
    SurfaceProfile preWetSurface = *cold;
    preWetSurface.surfaceWetness = 0.85;

    MaterialTileStore dryStore(128, 128, 64), wetStore(128, 128, 64);
    BrushReservoir dryRes(roundIt->reservoirCapacity), wetRes(roundIt->reservoirCapacity);
    MaterialStroke dryStroke(dryStore, *roundIt, dryRes, engine, watercolourProfile(), drySurface);
    MaterialStroke wetStroke(wetStore, *roundIt, wetRes, engine, watercolourProfile(), preWetSurface);
    CHECK(dryStroke.begin({64.0, 64.0, {0.7, 0.1, 0.0, 0.0}}) && dryStroke.end({64.0, 64.0, {0.7, 0.1, 0.0, 0.0}}));
    CHECK(wetStroke.begin({64.0, 64.0, {0.7, 0.1, 0.0, 0.0}}) && wetStroke.end({64.0, 64.0, {0.7, 0.1, 0.0, 0.0}}));

    for (int step = 0; step < 3; ++step) {
        CHECK(!engine.advanceWetMaterial(dryStore, 0.25, drySurface).failed);
        CHECK(!engine.advanceWetMaterial(wetStore, 0.25, preWetSurface).failed);
    }

    auto outerPigment = [](const MaterialTileStore& store) {
        double sum = 0.0;
        for (int y = 44; y <= 84; ++y) {
            for (int x = 44; x <= 84; ++x) {
                const int dx = x - 64, dy = y - 64;
                if (dx * dx + dy * dy >= 100) {
                    if (const auto batch = store.batchAt({x, y})) sum += batch->pigmentMass();
                }
            }
        }
        return sum;
    };
    CHECK(outerPigment(wetStore) > outerPigment(dryStore) * 1.15);
}

void stage6AllSixWatercolourBrushesBehaveDistinctly()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    const auto presets = defaultBrushDefinitions();
    auto findPreset = [&](const std::string& id) -> const BrushDefinition* {
        auto it = std::find_if(presets.begin(), presets.end(), [&](const auto& b) { return b.id == id; });
        return it == presets.end() ? nullptr : &(*it);
    };
    const auto* roundB = findPreset("watercolour-round");
    const auto* fineB = findPreset("watercolour-fine-round");
    const auto* mopB = findPreset("watercolour-mop");
    const auto* washB = findPreset("watercolour-wash");
    const auto* granB = findPreset("granulating-watercolour");
    const auto* dryB = findPreset("watercolour-dry-brush");
    CHECK(roundB && fineB && mopB && washB && granB && dryB);
    if (!roundB || !fineB || !mopB || !washB || !granB || !dryB) return;

    auto paintLine = [&](const BrushDefinition& def, const SurfaceProfile& surface, MaterialTileStore& store) {
        BrushReservoir res(def.effectiveReservoirCapacity());
        MaterialStroke s(store, def, res, engine, watercolourProfile(), surface);
        CHECK(s.begin({32.0, 64.0, {0.75, 0.1, 0.0, 0.0}}));
        CHECK(s.extend({140.0, 64.0, {0.75, 0.1, 0.0, 0.0}}));
        CHECK(s.end({220.0, 64.0, {0.75, 0.1, 0.0, 0.0}}));
        CHECK(!engine.advanceWetMaterial(store, 0.35, surface).failed);
    };

    MaterialTileStore fineStore(256, 128, 64), mopStore(256, 128, 64), washStore(256, 128, 64);
    paintLine(*fineB, coldPressedPaper(), fineStore);
    paintLine(*mopB, coldPressedPaper(), mopStore);
    paintLine(*washB, coldPressedPaper(), washStore);

    auto measureCrossSpan = [](const MaterialTileStore& store) {
        int minY = 128, maxY = -1;
        for (int y = 0; y < 128; ++y) {
            if (const auto b = store.batchAt({128, y}); b && b->pigmentMass() > 0.0) {
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
        return maxY >= minY ? (maxY - minY + 1) : 0;
    };
    const int fineSpan = measureCrossSpan(fineStore);
    const int mopSpan = measureCrossSpan(mopStore);
    const int washSpan = measureCrossSpan(washStore);
    CHECK(fineSpan > 0 && fineSpan < 14);
    CHECK(mopSpan > fineSpan * 3);
    CHECK(washSpan > fineSpan * 2);

    // Granulating brush concentrates more mass in tooth valleys on Rough paper than Hot Press.
    MaterialTileStore granHot(256, 128, 64), granRough(256, 128, 64);
    paintLine(*granB, hotPressedPaper(), granHot);
    paintLine(*granB, roughPaper(), granRough);
    auto valleyPeakDiff = [&](const MaterialTileStore& store, const SurfaceProfile& surface) {
        double valleyMass = 0.0, peakMass = 0.0;
        int vCount = 0, pCount = 0;
        for (int y = 58; y <= 70; ++y) {
            for (int x = 60; x <= 180; ++x) {
                if (const auto b = store.batchAt({x, y}); b && b->pigmentMass() > 0.0) {
                    const double h = surfaceToothHeightAt(x, y, surface);
                    if (h < 0.35) { valleyMass += b->pigmentMass(); ++vCount; }
                    else if (h > 0.65) { peakMass += b->pigmentMass(); ++pCount; }
                }
            }
        }
        if (vCount == 0 || pCount == 0) return 0.0;
        return (valleyMass / vCount) - (peakMass / pCount);
    };
    CHECK(valleyPeakDiff(granRough, roughPaper()) > valleyPeakDiff(granHot, hotPressedPaper()));

    // Dry brush hits tooth peaks and skips valleys on rough paper.
    MaterialTileStore dryRough(256, 128, 64);
    paintLine(*dryB, roughPaper(), dryRough);
    double dryPeak = 0.0, dryValley = 0.0;
    int dpCount = 0, dvCount = 0, skippedValleys = 0;
    for (int y = 58; y <= 70; ++y) {
        for (int x = 60; x <= 180; ++x) {
            const double h = surfaceToothHeightAt(x, y, roughPaper());
            const auto b = dryRough.batchAt({x, y});
            const double mass = b ? b->pigmentMass() : 0.0;
            if (h > 0.68) { dryPeak += mass; ++dpCount; }
            else if (h < 0.32) {
                dryValley += mass; ++dvCount;
                if (mass == 0.0) ++skippedValleys;
            }
        }
    }
    CHECK(dpCount > 0 && dvCount > 0);
    CHECK((dryPeak / dpCount) > (dryValley / dvCount) * 1.4);
    CHECK(skippedValleys > 0);
}

void stage6CleanWaterRewetsAndRedistributesWithoutPigment()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    const auto presets = defaultBrushDefinitions();
    const auto cleanIt = std::find_if(presets.begin(), presets.end(), [](const auto& b) { return b.id == "clean-water"; });
    CHECK(cleanIt != presets.end());
    if (cleanIt == presets.end()) return;

    // 1. Clean Water on blank paper deposits water ONLY (zero pigment, not opaque white).
    MaterialTileStore blankStore(128, 128, 64);
    BrushReservoir cleanRes(cleanIt->effectiveReservoirCapacity());
    MaterialStroke blankWater(blankStore, *cleanIt, cleanRes, engine, watercolourProfile(), coldPressedPaper());
    CHECK(blankWater.begin({64.0, 64.0, {0.8, 0.0, 0.0, 0.0}}) && blankWater.end({64.0, 64.0, {0.8, 0.0, 0.0, 0.0}}));
    const auto centerWater = blankStore.batchAt({64, 64});
    CHECK(centerWater && centerWater->water > 0.0 && centerWater->pigmentMass() == 0.0);

    MaterialRenderer renderer(engine);
    std::vector<std::uint32_t> blankPixels;
    CHECK(renderer.renderTile(blankStore, {1, 1}, {}, watercolourProfile(), coldPressedPaper(), &blankPixels));
    CHECK(blankPixels[0] == 0U);

    // 2. Clean Water across a dried pigment band rewets it and pushes pigment into clean neighbor cells.
    MaterialTileStore store(128, 128, 64);
    PaintBatch driedBand;
    driedBand.mediumId = "watercolour";
    driedBand.addPigment("PB29", 0.55);
    driedBand.addWater(0.0);
    driedBand.addBinder(0.06);
    driedBand.wetness = 0.0;
    for (int y = 52; y <= 76; ++y) {
        for (int x = 36; x <= 56; ++x) {
            CHECK(store.setBatch({x, y}, driedBand) == MaterialTileStore::Result::Changed);
            CHECK(store.updateCellState({x, y}, 0.0, 0.06, 0.0, 0.92, 0.0) == MaterialTileStore::Result::Changed);
        }
    }

    BrushReservoir rewetRes(cleanIt->effectiveReservoirCapacity());
    MaterialStroke rewetStroke(store, *cleanIt, rewetRes, engine, watercolourProfile(), coldPressedPaper());
    CHECK(rewetStroke.begin({46.0, 64.0, {0.75, 0.0, 0.0, 0.0}}));
    CHECK(rewetStroke.extend({62.0, 64.0, {0.75, 0.0, 0.0, 0.0}}));
    CHECK(rewetStroke.end({74.0, 64.0, {0.75, 0.0, 0.0, 0.0}}));

    const auto rewetted = store.cellAt({48, 64});
    CHECK(rewetted && rewetted->wetness > 0.25F && rewetted->drying < 0.70F);
    const auto pushedPigment = store.batchAt({66, 64});
    CHECK(pushedPigment && pushedPigment->pigmentMass() > 0.0);

    // 3. Hot Press vs Cold Press water absorption rate difference.
    MaterialTileStore hotStore(128, 128, 64), coldStore(128, 128, 64);
    BrushReservoir hotRes(cleanIt->effectiveReservoirCapacity()), coldRes(cleanIt->effectiveReservoirCapacity());
    MaterialStroke hotW(hotStore, *cleanIt, hotRes, engine, watercolourProfile(), hotPressedPaper());
    MaterialStroke coldW(coldStore, *cleanIt, coldRes, engine, watercolourProfile(), coldPressedPaper());
    CHECK(hotW.begin({64.0, 64.0, {0.8, 0.0, 0.0, 0.0}}) && hotW.end({64.0, 64.0, {0.8, 0.0, 0.0, 0.0}}));
    CHECK(coldW.begin({64.0, 64.0, {0.8, 0.0, 0.0, 0.0}}) && coldW.end({64.0, 64.0, {0.8, 0.0, 0.0, 0.0}}));
    CHECK(!engine.advanceWetMaterial(hotStore, 0.5, hotPressedPaper()).failed);
    CHECK(!engine.advanceWetMaterial(coldStore, 0.5, coldPressedPaper()).failed);
    CHECK(hotStore.batchAt({64, 64})->water > coldStore.batchAt({64, 64})->water);
}

void stage6PaintLiftingRespectsWetDrySubstrateAndStaining()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    const auto presets = defaultBrushDefinitions();
    const auto liftIt = std::find_if(presets.begin(), presets.end(), [](const auto& b) { return b.id == "paint-lifting"; });
    CHECK(liftIt != presets.end());
    if (liftIt == presets.end()) return;

    auto runLift = [&](const std::string& pigmentId, bool wetPaint, const SurfaceProfile& surface) {
        MaterialTileStore store(128, 128, 64);
        PaintBatch base;
        base.mediumId = "watercolour";
        base.addPigment(pigmentId, 0.80);
        base.addWater(wetPaint ? 1.8 : 0.0);
        base.addBinder(0.08);
        base.wetness = wetPaint ? 0.90 : 0.0;
        for (int y = 50; y <= 78; ++y) {
            for (int x = 50; x <= 78; ++x) {
                CHECK(store.setBatch({x, y}, base) == MaterialTileStore::Result::Changed);
                CHECK(store.updateCellState({x, y}, wetPaint ? 1.8 : 0.0, 0.08, 0.0,
                                            wetPaint ? 0.05 : 0.95, 0.0) == MaterialTileStore::Result::Changed);
            }
        }
        const double before = store.batchAt({64, 64})->pigmentMass();
        BrushReservoir res(liftIt->effectiveReservoirCapacity());
        MaterialStroke s(store, *liftIt, res, engine, watercolourProfile(), surface);
        CHECK(s.begin({64.0, 64.0, {0.8, 0.0, 0.0, 0.0}}) && s.end({64.0, 64.0, {0.8, 0.0, 0.0, 0.0}}));
        const double after = store.batchAt({64, 64})->pigmentMass();
        return before - after;
    };

    const double liftedWet = runLift("PB29", true, coldPressedPaper());
    const double liftedDry = runLift("PB29", false, coldPressedPaper());
    const double liftedHot = runLift("PB29", false, hotPressedPaper());
    const double liftedRough = runLift("PB29", false, roughPaper());
    const double liftedNonStain = runLift("PB29", true, hotPressedPaper());
    const double liftedStain = runLift("PB15", true, hotPressedPaper());

    CHECK(liftedWet > 0.15);
    CHECK(liftedWet > liftedDry * 1.4);
    CHECK(liftedHot > liftedRough * 1.25);
    CHECK(liftedNonStain > liftedStain * 1.15);
}

void stage6MediumSeparationSparsePerformanceAndMetrics()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    MaterialRenderer renderer(engine);

    // 1. Same PB15 pigment has dramatically different opacity/coverage under Gouache vs Watercolour.
    PaintBatch wcPaint;
    wcPaint.mediumId = "watercolour";
    wcPaint.addPigment("PB15", 0.65);
    wcPaint.addWater(0.35);
    wcPaint.addBinder(0.35);
    PaintBatch gouachePaint = wcPaint;
    gouachePaint.mediumId = "gouache";
    gouachePaint.addFiller(0.18);
    const auto wcOptics = engine.opticalResult(wcPaint, watercolourProfile(), coldPressedPaper(), 4.0);
    const auto goOptics = engine.opticalResult(gouachePaint, gouacheProfile(), coldPressedPaper(), 4.0);
    CHECK(goOptics.alpha > wcOptics.alpha + 0.18);

    // 2. Sparse allocation & bounded wet-region stepping on a 1600x1200 canvas.
    MaterialTileStore bigStore(1600, 1200, 64);
    const auto presets = defaultBrushDefinitions();
    const auto washIt = std::find_if(presets.begin(), presets.end(), [](const auto& b) { return b.id == "watercolour-wash"; });
    const auto liftIt = std::find_if(presets.begin(), presets.end(), [](const auto& b) { return b.id == "paint-lifting"; });
    CHECK(washIt != presets.end() && liftIt != presets.end());
    if (washIt == presets.end() || liftIt == presets.end()) return;

    const auto t0 = std::chrono::steady_clock::now();
    BrushReservoir res(washIt->effectiveReservoirCapacity());
    MaterialStroke stroke(bigStore, *washIt, res, engine, watercolourProfile(), coldPressedPaper());
    CHECK(stroke.begin({120.0, 200.0, {0.8, 0.0, 0.0, 0.0}}));
    CHECK(stroke.extend({420.0, 200.0, {0.8, 0.0, 0.0, 0.0}}));
    CHECK(stroke.end({720.0, 200.0, {0.8, 0.0, 0.0, 0.0}}));
    const auto t1 = std::chrono::steady_clock::now();

    const std::size_t totalCanvasTiles = 25U * 19U;
    const std::size_t allocatedAfterStroke = bigStore.allocatedTiles();
    const std::size_t activeWetTiles = bigStore.activeTilePoints().size();
    CHECK(allocatedAfterStroke > 0 && allocatedAfterStroke < totalCanvasTiles / 4);
    CHECK(activeWetTiles > 0 && activeWetTiles <= allocatedAfterStroke);

    const auto simStart = std::chrono::steady_clock::now();
    CHECK(!engine.advanceWetMaterial(bigStore, 0.2, coldPressedPaper()).failed);
    const auto simEnd = std::chrono::steady_clock::now();

    std::vector<std::uint32_t> rendered;
    const auto renderStart = std::chrono::steady_clock::now();
    for (const auto& tp : bigStore.activeTilePoints()) {
        CHECK(renderer.renderTile(bigStore, tp, {}, watercolourProfile(), coldPressedPaper(), &rendered));
    }
    const auto renderEnd = std::chrono::steady_clock::now();

    // Composition reclamation check after clearing tiles.
    const std::size_t beforeComps = bigStore.activeCompositionCount();
    const std::size_t bytesBeforeClear = bigStore.allocatedBytes();
    for (const auto& tp : bigStore.allocatedTilePoints()) {
        std::vector<MaterialTileSnapshot> empty{MaterialTileSnapshot{tp, {}}};
        CHECK(bigStore.restoreTiles(empty, false) != MaterialTileStore::Result::InvalidData);
    }
    const std::size_t reclaimed = bigStore.reclaimUnreferenced();
    CHECK(beforeComps > 0 && reclaimed == beforeComps);
    CHECK(bigStore.activeCompositionCount() == 0);

    const double strokeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double simMs = std::chrono::duration<double, std::milli>(simEnd - simStart).count();
    const double renderMs = std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();
    std::cout << "STAGE6_METRICS allocated_tiles=" << allocatedAfterStroke
              << "/" << totalCanvasTiles
              << " active_wet_tiles=" << activeWetTiles
              << " compositions_before_clear=" << beforeComps
              << " reclaimed=" << reclaimed
              << " memory_bytes=" << bytesBeforeClear
              << " stroke_ms=" << strokeMs
              << " wet_step_ms=" << simMs
              << " render_active_ms=" << renderMs << "\n";
}

void stage6RenderReproducibleTestSheet()
{
    PigmentLibrary pigments;
    MaterialEngine engine(pigments);
    MaterialRenderer renderer(engine);
    const auto presets = defaultBrushDefinitions();
    auto findPreset = [&](const std::string& id) {
        return *std::find_if(presets.begin(), presets.end(), [&](const auto& b) { return b.id == id; });
    };

    constexpr int W = 768;
    constexpr int H = 512;
    MaterialTileStore sheet(W, H, 64);

    // Helper: draw stroke segment cleanly with white-paper optical compositing later.
    auto drawSeg = [&](BrushDefinition def, const SurfaceProfile& surface, const std::string& medium,
                       std::uint32_t argb, double x0, double y0, double x1, double y1, bool liftOrWater = false) {
        if (!liftOrWater && def.id != "granulating-watercolour") {
            def.initialPaint = makeResidualPaint(argb, medium == "gouache" ? 0.95 : 0.72,
                medium == "gouache" ? 0.35 : 1.55, medium == "gouache" ? 0.32 : 0.08, medium);
            if (medium == "gouache") def.initialPaint.addFiller(0.18);
        }
        def.size = std::min(def.size, 26.0);
        BrushReservoir res(def.effectiveReservoirCapacity());
        const auto* prof = findMediumProfile(medium);
        MaterialStroke s(sheet, def, res, engine, *prof, surface);
        CHECK(s.begin({x0, y0, {0.82, 0.0, 0.0, 0.0}}));
        CHECK(s.extend({(x0 + x1) * 0.5, (y0 + y1) * 0.5, {0.82, 0.0, 0.0, 0.0}}));
        CHECK(s.end({x1, y1, {0.82, 0.0, 0.0, 0.0}}));
    };

    // Row 1 (y=64): All 6 Watercolour brushes on Cold Press.
    const std::vector<std::string> wcBrushes = {
        "watercolour-round", "watercolour-fine-round", "watercolour-mop",
        "watercolour-wash", "granulating-watercolour", "watercolour-dry-brush"
    };
    for (std::size_t i = 0; i < wcBrushes.size(); ++i) {
        const double x0 = 24.0 + double(i) * 124.0;
        drawSeg(findPreset(wcBrushes[i]), coldPressedPaper(), "watercolour", 0xFF1F5DA8U, x0, 64.0, x0 + 96.0, 64.0);
    }

    // Row 2 (y=192): Hot Press, Cold Press, Rough, Wet-on-Wet (surfaceWetness=0.85).
    drawSeg(findPreset("granulating-watercolour"), hotPressedPaper(), "watercolour", 0xFF6B4226U, 24.0, 192.0, 164.0, 192.0);
    drawSeg(findPreset("granulating-watercolour"), coldPressedPaper(), "watercolour", 0xFF6B4226U, 204.0, 192.0, 344.0, 192.0);
    drawSeg(findPreset("granulating-watercolour"), roughPaper(), "watercolour", 0xFF6B4226U, 384.0, 192.0, 524.0, 192.0);
    SurfaceProfile wetPaper = coldPressedPaper();
    wetPaper.surfaceWetness = 0.85;
    drawSeg(findPreset("watercolour-round"), wetPaper, "watercolour", 0xFFC22B68U, 564.0, 192.0, 720.0, 192.0);

    // Row 3 (y=320): Clean Water bloom + Paint Lifting across dried blue band.
    drawSeg(findPreset("watercolour-wash"), coldPressedPaper(), "watercolour", 0xFF2855A0U, 24.0, 320.0, 720.0, 320.0);
    for (int step = 0; step < 3; ++step)
        CHECK(!engine.advanceWetMaterial(sheet, 0.8, coldPressedPaper()).failed);
    drawSeg(findPreset("clean-water"), coldPressedPaper(), "watercolour", 0U, 120.0, 290.0, 120.0, 355.0, true);
    drawSeg(findPreset("paint-lifting"), coldPressedPaper(), "watercolour", 0U, 360.0, 290.0, 360.0, 355.0, true);

    // Row 4 (y=448): Gouache Round under-layer + opaque overstroke on Smooth & Textured illustration paper.
    drawSeg(findPreset("gouache-round"), smoothIllustrationPaper(), "gouache", 0xFF1F2B48U, 24.0, 448.0, 340.0, 448.0);
    drawSeg(findPreset("gouache-round"), smoothIllustrationPaper(), "gouache", 0xFFF2C12EU, 120.0, 420.0, 240.0, 476.0);
    drawSeg(findPreset("gouache-round"), texturedIllustrationPaper(), "gouache", 0xFF1F2B48U, 400.0, 448.0, 720.0, 448.0);
    drawSeg(findPreset("gouache-round"), texturedIllustrationPaper(), "gouache", 0xFFE6533CU, 500.0, 420.0, 620.0, 476.0);

    const auto ppmPath = std::filesystem::temp_directory_path() / "komapaint_stage6_sheet.ppm";
    std::ofstream ppm(ppmPath, std::ios::binary);
    CHECK(ppm.is_open());
    ppm << "P6\n" << W << " " << H << "\n255\n";
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 3U, 248);
    std::vector<std::uint32_t> tilePixels;
    for (int ty = 0; ty < H / 64; ++ty) {
        const SurfaceProfile* rowSurf = &coldPressedPaper();
        if (ty == 2 || ty == 3) rowSurf = &roughPaper();
        else if (ty >= 6) rowSurf = &smoothIllustrationPaper();
        for (int tx = 0; tx < W / 64; ++tx) {
            if (sheet.tileEntries({tx, ty}).empty()) continue;
            CHECK(renderer.renderTile(sheet, {tx, ty}, {}, watercolourProfile(), *rowSurf, &tilePixels));
            for (int ly = 0; ly < 64; ++ly) {
                for (int lx = 0; lx < 64; ++lx) {
                    const std::uint32_t px = tilePixels[static_cast<std::size_t>(ly) * 64 + static_cast<std::size_t>(lx)];
                    const std::uint32_t a = px >> 24U;
                    if (a == 0) continue;
                    const std::uint32_t inv = 255U - a;
                    const std::uint8_t r = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, ((px >> 16U) & 255U) + (248U * inv + 127U) / 255U));
                    const std::uint8_t g = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, ((px >> 8U) & 255U) + (248U * inv + 127U) / 255U));
                    const std::uint8_t b = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, (px & 255U) + (248U * inv + 127U) / 255U));
                    const std::size_t idx = (static_cast<std::size_t>(ty * 64 + ly) * W + static_cast<std::size_t>(tx * 64 + lx)) * 3U;
                    rgb[idx] = r; rgb[idx + 1] = g; rgb[idx + 2] = b;
                }
            }
        }
    }
    ppm.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    CHECK(ppm.good());
}
}

int main()
{
    pigmentMixingRetainsCompositionAndIsNotRgbAveraging();
    pigmentConcentrationWaterAndSubstrateChangeOpticalOutput();
    arbitraryRgbIsKeptAsUnidentifiedResidual();
    reservoirConservesMaterialAndModelsLoadPickupReloadAndCleaning();
    brushPressureSpeedAndSurfaceDabsAffectRealSparsePixels();
    materialStrokeSpacesDabsAndStaysInsideSparseCanvas();
    authoritativeSparseMaterialTilesSurviveStrokesAndRendering();
    wetMaterialDriesSettlesSpreadsAndBecomesInactiveLazily();
    activeTileIndexTracksWetWritesDryingAndHistoryRestore();
    paperProfilesChangeDepositedMaterialNotOnlyRenderedTexture();
    brushPickupRewettingAndMixerLoadsRetainPigmentProportions();
    namedProfilesAndPhysicalPigmentPropertiesArePresent();
    spectralPigmentsMatchMagentaPurpleAndPreserveTintAlpha();
    dryingReclaimsCompositionsAndRespectsUndoPins();
    materialStrokeAutoReloadsCleanMixtureAndScalesBySize();
    gouacheRoundCoversOpaquelyDepletesSizeIndependentlyAndReworks();
    brushPropertiesIndependentlyAffectMaterialSimulation();
    stage6SubstrateProfilesAndWetOnWetVsWetOnDry();
    stage6AllSixWatercolourBrushesBehaveDistinctly();
    stage6CleanWaterRewetsAndRedistributesWithoutPigment();
    stage6PaintLiftingRespectsWetDrySubstrateAndStaining();
    stage6MediumSeparationSparsePerformanceAndMetrics();
    stage6RenderReproducibleTestSheet();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "material engine checks passed\n";
    return EXIT_SUCCESS;
}
