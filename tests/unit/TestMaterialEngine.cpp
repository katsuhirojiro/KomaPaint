#include "material/MaterialEngine.h"
#include "material/MaterialTileStore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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
    const auto hotPixel = engine.composite(0U, *hotBatch, watercolourProfile(), hotPressedPaper(), 1.0);
    const auto roughPixel = engine.composite(0U, *roughBatch, watercolourProfile(), roughPaper(), 1.0);
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
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "material engine checks passed\n";
    return EXIT_SUCCESS;
}
