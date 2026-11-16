#include "MaterialEngine.h"

#include <utility>

namespace komapaint::material {
namespace {

BrushDefinition brush(std::string id, std::string name, TipGeometry tip,
                      double size, double capacity, double load,
                      const PaintBatch& paint, const std::string& medium)
{
    BrushDefinition result;
    result.id = std::move(id);
    result.name = std::move(name);
    result.tip = tip;
    result.size = size;
    result.nominalSize = size;
    result.reservoirCapacity = capacity;
    result.initialLoad = load;
    result.initialPaint = paint;
    result.mediumId = medium;
    return result;
}

PaintBatch gouacheRecipe()
{
    PaintBatch paint;
    paint.mediumId = "gouache";
    paint.addPigment("PY3", 1.25);
    paint.addPigment("PW6", 0.32);
    paint.addWater(0.62);
    paint.addBinder(0.48);
    paint.addFiller(0.14);
    return paint;
}

PaintBatch watercolourRecipe(const char* pigmentId, double pigment, double water,
                             double binder)
{
    PaintBatch paint;
    paint.mediumId = "watercolour";
    paint.addPigment(pigmentId, pigment);
    paint.addWater(water);
    paint.addBinder(binder);
    return paint;
}

} // namespace

std::vector<BrushDefinition> defaultBrushDefinitions()
{
    const PaintBatch gouache = gouacheRecipe();
    PaintBatch dryGouache = gouache;
    dryGouache.water = 0.16;
    dryGouache.filler = 0.20;
    dryGouache.wetness = 0.08;
    dryGouache.consistency = 0.92;

    const PaintBatch watercolor = watercolourRecipe("PB15", 0.62, 2.65, 0.12);
    const PaintBatch mopPaint = watercolourRecipe("PB15", 0.55, 5.2, 0.10);
    const PaintBatch granulating = watercolourRecipe("PBr7", 0.70, 2.9, 0.12);
    const PaintBatch dryWatercolor = watercolourRecipe("PB15", 0.52, 0.62, 0.12);
    PaintBatch cleanWater;
    cleanWater.mediumId = "watercolour";
    cleanWater.water = 4.0;
    cleanWater.wetness = 1.0;
    cleanWater.consistency = 0.0;
    PaintBatch liftingWater = cleanWater;
    liftingWater.water = 0.55;
    liftingWater.wetness = 1.0;
    PaintBatch spatulaPaint = gouache;

    std::vector<BrushDefinition> result;
    result.reserve(9);
    auto round = brush("gouache-round", "Gouache Round", TipGeometry::Round,
                       18, 8.5, 0.88, gouache, "gouache");
    round.hardness = 0.72;
    round.releaseRate = 0.055;
    round.pressureTransfer = 0.62;
    round.contactPressureScale = 0.35;
    round.waterSpread = 0.14;
    round.pigmentDiffusion = 0.08;
    round.paintBody = 0.78;
    round.dryingRate = 1.15;
    round.pickupRate = 0.14;
    round.paintLiftability = 0.28;
    round.rewetting = 0.24;
    round.staining = 0.35;
    round.toothResponse = 0.18;
    round.edgeAccumulation = 0.08;
    round.textureStrength = 0.08;
    round.autoReloadOnStroke = true;
    result.push_back(round);

    auto dry = brush("dry-gouache", "Dry Gouache", TipGeometry::Bristle,
                     24, 6.4, 0.68, dryGouache, "gouache");
    dry.bristleCount = 13;
    dry.bristleSpread = 0.72;
    dry.followDirection = true;
    dry.textureStrength = 0.45;
    dry.paintBody = 0.92;
    dry.dryingRate = 1.35;
    dry.releaseRate = 0.042;
    dry.pressureTransfer = 0.78;
    dry.waterSpread = 0.04;
    dry.pickupRate = 0.025;
    dry.toothResponse = 0.88;
    dry.edgeAccumulation = 0.12;
    result.push_back(dry);

    auto tiny = brush("tiny-gouache", "Tiny Gouache", TipGeometry::Round,
                      4, 1.8, 0.80, gouache, "gouache");
    tiny.hardness = 0.85;
    tiny.paintBody = 0.75;
    tiny.releaseRate = 0.045;
    tiny.pressureTransfer = 0.82;
    tiny.waterSpread = 0.08;
    tiny.pickupRate = 0.05;
    tiny.paintLiftability = 0.2;
    tiny.rewetting = 0.12;
    tiny.toothResponse = 0.05;
    result.push_back(tiny);

    auto watercolorRound = brush("watercolour-round", "Watercolour Round", TipGeometry::Round,
                                 14, 7.5, 0.88, watercolor, "watercolour");
    watercolorRound.releaseRate = 0.11;
    watercolorRound.pressureTransfer = 0.68;
    watercolorRound.contactPressureScale = 0.45;
    watercolorRound.paintBody = 0.18;
    watercolorRound.dryingRate = 0.85;
    watercolorRound.waterSpread = 0.82;
    watercolorRound.pigmentDiffusion = 0.74;
    watercolorRound.pickupRate = 0.18;
    watercolorRound.paintLiftability = 0.42;
    watercolorRound.rewetting = 0.36;
    watercolorRound.edgeAccumulation = 0.25;
    watercolorRound.granulationResponse = 0.15;
    result.push_back(watercolorRound);

    auto mop = brush("watercolour-mop", "Watercolour Mop", TipGeometry::Mop,
                     72, 32.0, 0.95, mopPaint, "watercolour");
    mop.aspect = 1.32;
    mop.releaseRate = 0.10;
    mop.pressureTransfer = 0.55;
    mop.contactPressureScale = 0.50;
    mop.paintBody = 0.12;
    mop.dryingRate = 0.75;
    mop.waterSpread = 0.95;
    mop.pigmentDiffusion = 0.90;
    mop.pickupRate = 0.18;
    mop.paintLiftability = 0.38;
    mop.rewetting = 0.28;
    result.push_back(mop);

    auto granulatingBrush = brush("granulating-watercolour", "Granulating Watercolour",
                                  TipGeometry::Round, 18, 9.0, 0.9, granulating, "watercolour");
    granulatingBrush.releaseRate = 0.095;
    granulatingBrush.paintBody = 0.16;
    granulatingBrush.dryingRate = 0.80;
    granulatingBrush.waterSpread = 0.88;
    granulatingBrush.pigmentDiffusion = 0.82;
    granulatingBrush.granulationResponse = 0.92;
    granulatingBrush.pickupRate = 0.08;
    granulatingBrush.paintLiftability = 0.18;
    granulatingBrush.rewetting = 0.12;
    granulatingBrush.toothResponse = 0.74;
    result.push_back(granulatingBrush);

    auto dryWatercolourBrush = brush("watercolour-dry-brush", "Watercolour Dry Brush",
                                     TipGeometry::Bristle, 28, 4.5, 0.62,
                                     dryWatercolor, "watercolour");
    dryWatercolourBrush.bristleCount = 15;
    dryWatercolourBrush.bristleSpread = 0.80;
    dryWatercolourBrush.followDirection = true;
    dryWatercolourBrush.textureStrength = 0.55;
    dryWatercolourBrush.paintBody = 0.45;
    dryWatercolourBrush.releaseRate = 0.045;
    dryWatercolourBrush.pressureTransfer = 0.88;
    dryWatercolourBrush.waterSpread = 0.04;
    dryWatercolourBrush.toothResponse = 0.94;
    dryWatercolourBrush.granulationResponse = 0.35;
    result.push_back(dryWatercolourBrush);

    auto clean = brush("clean-water", "Clean Water Brush", TipGeometry::Round,
                       24, 9.0, 0.8, cleanWater, "watercolour");
    clean.paintBody = 0.0;
    clean.dryingRate = 0.70;
    clean.releaseRate = 0.10;
    clean.waterSpread = 0.95;
    clean.pigmentDiffusion = 0.90;
    clean.initialLoad = 0.8;
    result.push_back(clean);

    auto lift = brush("paint-lifting", "Paint Lifting Brush", TipGeometry::Round,
                      20, 7.5, 0.7, liftingWater, "watercolour");
    lift.paintBody = 0.05;
    lift.releaseRate = 0.02;
    lift.pickupRate = 0.65;
    lift.paintLiftability = 0.92;
    lift.rewetting = 0.92;
    lift.staining = 0.10;
    lift.waterSpread = 0.72;
    result.push_back(lift);

    auto spatula = brush("paint-mixing-spatula", "Paint Mixing Brush / Spatula",
                         TipGeometry::Spatula, 30, 12.0, 0.6, spatulaPaint, "gouache");
    spatula.aspect = 1.8;
    spatula.followDirection = true;
    spatula.paintBody = 0.88;
    spatula.releaseRate = 0.015;
    spatula.pickupRate = 0.95;
    spatula.paintLiftability = 0.8;
    spatula.staining = 0.25;
    spatula.contactTransfer = 0.9;
    result.push_back(spatula);
    return result;
}

} // namespace komapaint::material
