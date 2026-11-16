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

    const PaintBatch watercolor = watercolourRecipe("PB15", 0.68, 1.65, 0.10);
    const PaintBatch fineWatercolor = watercolourRecipe("PB15", 0.78, 1.35, 0.12);
    const PaintBatch mopPaint = watercolourRecipe("PB15", 0.62, 2.80, 0.08);
    const PaintBatch washPaint = watercolourRecipe("PB15", 0.56, 2.45, 0.08);
    const PaintBatch granulating = watercolourRecipe("PBr7", 0.76, 1.85, 0.10);
    const PaintBatch dryWatercolor = watercolourRecipe("PB15", 0.68, 0.24, 0.12);
    PaintBatch cleanWater;
    cleanWater.mediumId = "watercolour";
    cleanWater.water = 4.0;
    cleanWater.wetness = 1.0;
    cleanWater.consistency = 0.0;
    PaintBatch liftingWater = cleanWater;
    liftingWater.water = 0.75;
    liftingWater.wetness = 1.0;
    PaintBatch spatulaPaint = gouache;

    std::vector<BrushDefinition> result;
    result.reserve(12);
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
    watercolorRound.hardness = 0.35;
    watercolorRound.releaseRate = 0.16;
    watercolorRound.pressureTransfer = 0.68;
    watercolorRound.contactPressureScale = 0.45;
    watercolorRound.paintBody = 0.18;
    watercolorRound.dryingRate = 0.85;
    watercolorRound.waterSpread = 0.82;
    watercolorRound.pigmentDiffusion = 0.74;
    watercolorRound.pickupRate = 0.18;
    watercolorRound.paintLiftability = 0.42;
    watercolorRound.rewetting = 0.36;
    watercolorRound.edgeAccumulation = 0.28;
    watercolorRound.granulationResponse = 0.15;
    result.push_back(watercolorRound);

    auto fineRound = brush("watercolour-fine-round", "Watercolour Fine Round", TipGeometry::Round,
                           5, 2.4, 0.84, fineWatercolor, "watercolour");
    fineRound.hardness = 0.58;
    fineRound.spacingRatio = 0.12;
    fineRound.releaseRate = 0.115;
    fineRound.pressureTransfer = 0.72;
    fineRound.contactPressureScale = 0.52;
    fineRound.pressureCurve = SensorCurve{{0.18, 0.55, 1.0}};
    fineRound.paintBody = 0.22;
    fineRound.dryingRate = 0.95;
    fineRound.waterSpread = 0.36;
    fineRound.pigmentDiffusion = 0.32;
    fineRound.pickupRate = 0.08;
    fineRound.paintLiftability = 0.28;
    fineRound.rewetting = 0.22;
    fineRound.edgeAccumulation = 0.18;
    fineRound.toothResponse = 0.12;
    result.push_back(fineRound);

    auto mop = brush("watercolour-mop", "Watercolour Mop", TipGeometry::Mop,
                     72, 34.0, 0.95, mopPaint, "watercolour");
    mop.aspect = 1.32;
    mop.hardness = 0.12;
    mop.releaseRate = 0.155;
    mop.pressureTransfer = 0.55;
    mop.contactPressureScale = 0.50;
    mop.paintBody = 0.12;
    mop.dryingRate = 0.75;
    mop.waterSpread = 0.95;
    mop.pigmentDiffusion = 0.90;
    mop.pickupRate = 0.18;
    mop.paintLiftability = 0.38;
    mop.rewetting = 0.28;
    mop.edgeAccumulation = 0.18;
    result.push_back(mop);

    auto wash = brush("watercolour-wash", "Watercolour Wash", TipGeometry::Flat,
                      48, 24.0, 0.92, washPaint, "watercolour");
    wash.aspect = 2.2;
    wash.followDirection = true;
    wash.hardness = 0.42;
    wash.spacingRatio = 0.12;
    wash.releaseRate = 0.14;
    wash.pressureTransfer = 0.38;
    wash.contactPressureScale = 0.25;
    wash.paintBody = 0.10;
    wash.dryingRate = 0.78;
    wash.waterSpread = 0.88;
    wash.pigmentDiffusion = 0.84;
    wash.pickupRate = 0.12;
    wash.paintLiftability = 0.30;
    wash.rewetting = 0.25;
    wash.edgeAccumulation = 0.15;
    result.push_back(wash);

    auto granulatingBrush = brush("granulating-watercolour", "Watercolour Granulating",
                                  TipGeometry::Round, 18, 9.0, 0.90, granulating, "watercolour");
    granulatingBrush.hardness = 0.28;
    granulatingBrush.releaseRate = 0.145;
    granulatingBrush.paintBody = 0.16;
    granulatingBrush.dryingRate = 0.80;
    granulatingBrush.waterSpread = 0.88;
    granulatingBrush.pigmentDiffusion = 0.82;
    granulatingBrush.granulationResponse = 0.94;
    granulatingBrush.pickupRate = 0.08;
    granulatingBrush.paintLiftability = 0.18;
    granulatingBrush.rewetting = 0.12;
    granulatingBrush.toothResponse = 0.78;
    granulatingBrush.edgeAccumulation = 0.34;
    result.push_back(granulatingBrush);

    auto dryWatercolourBrush = brush("watercolour-dry-brush", "Watercolour Dry Brush",
                                     TipGeometry::Bristle, 28, 4.8, 0.58,
                                     dryWatercolor, "watercolour");
    dryWatercolourBrush.bristleCount = 15;
    dryWatercolourBrush.bristleSpread = 0.80;
    dryWatercolourBrush.followDirection = true;
    dryWatercolourBrush.textureStrength = 0.62;
    dryWatercolourBrush.paintBody = 0.45;
    dryWatercolourBrush.releaseRate = 0.045;
    dryWatercolourBrush.pressureTransfer = 0.88;
    dryWatercolourBrush.waterSpread = 0.04;
    dryWatercolourBrush.toothResponse = 0.96;
    dryWatercolourBrush.granulationResponse = 0.35;
    result.push_back(dryWatercolourBrush);

    auto clean = brush("clean-water", "Clean Water", TipGeometry::Round,
                       24, 9.0, 0.82, cleanWater, "watercolour");
    clean.hardness = 0.25;
    clean.paintBody = 0.0;
    clean.dryingRate = 0.70;
    clean.releaseRate = 0.14;
    clean.waterSpread = 0.95;
    clean.pigmentDiffusion = 0.90;
    clean.rewetting = 0.94;
    clean.pickupRate = 0.0;
    clean.initialLoad = 0.82;
    result.push_back(clean);

    auto lift = brush("paint-lifting", "Paint Lifting", TipGeometry::Round,
                      20, 7.5, 0.70, liftingWater, "watercolour");
    lift.hardness = 0.45;
    lift.paintBody = 0.05;
    lift.releaseRate = 0.015;
    lift.pickupRate = 0.72;
    lift.paintLiftability = 0.94;
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
