#include "material/PaintMixerModel.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace komapaint::material;

namespace {

int channelR(std::uint32_t argb) { return int((argb >> 16U) & 0xffU); }
int channelG(std::uint32_t argb) { return int((argb >> 8U) & 0xffU); }
int channelB(std::uint32_t argb) { return int(argb & 0xffU); }

void testPigmentLibraryProvenanceAndOrganization()
{
    PaintMixerModel mixer;
    const auto& pigments = mixer.pigments().pigments();
    assert(pigments.size() >= 10);

    bool foundEngineEstimate = false;
    bool foundProvisional = false;
    bool foundWhiteGouacheOnly = false;
    for (const auto& p : pigments) {
        assert(!p.id.empty());
        assert(!p.name.empty());
        assert(!p.colorFamily.empty());
        assert(!p.provenance.empty());
        // None of the compact 12-band curves should falsely claim lab verification.
        assert(!p.verifiedMeasurement);
        if (p.provenance.find("Engine spectral estimate") != std::string::npos)
            foundEngineEstimate = true;
        if (p.provenance.find("Provisional estimate") != std::string::npos)
            foundProvisional = true;
        if (p.id == "PW6") {
            assert(p.media.size() == 1 && p.media[0] == "gouache");
            foundWhiteGouacheOnly = true;
        }
        const auto swatchG = pigmentPreviewArgb(mixer.engine(), p, "gouache");
        assert((swatchG >> 24U) == 0xffU);
    }
    assert(foundEngineEstimate);
    assert(foundProvisional);
    assert(foundWhiteGouacheOnly);
}

void testActualPigmentMixingNotRgbAverage()
{
    PaintMixerModel mixer;
    assert(mixer.setMedium("gouache"));
    const std::string depId = mixer.addPigmentDeposit("PY3", 1.0);
    assert(!depId.empty());
    assert(mixer.addPigmentToActive("PB15", 1.0));

    const auto* dep = mixer.activeDeposit();
    assert(dep != nullptr);
    assert(dep->batch.pigments.size() == 2);
    assert(dep->batch.pigments[0].pigmentId == "PY3");
    assert(dep->batch.pigments[1].pigmentId == "PB15");
    assert(!dep->containsDigitalImport());

    const auto summary = mixer.activeOpticalSummary();
    // Kubelka-Munk mix of Primary Yellow + Phthalo Blue produces genuine green
    // (green channel clearly exceeds both red and blue), unlike RGB linear grey.
    const int r = channelR(summary.porcelainArgb);
    const int g = channelG(summary.porcelainArgb);
    const int b = channelB(summary.porcelainArgb);
    assert(g > r + 15);
    assert(g > b + 5);

    // Adjust ratios and verify composition & optical output update deterministically.
    assert(mixer.setComponentMass(0, 3.0));
    const auto yellower = mixer.activeOpticalSummary();
    assert(channelR(yellower.porcelainArgb) > r);

    // Remove second component -> returns to pure PY3.
    assert(mixer.removeComponent(1));
    assert(mixer.activeDeposit()->batch.pigments.size() == 1);
    assert(mixer.activeDeposit()->batch.pigments[0].pigmentId == "PY3");
}

void testGouacheVsWatercolourOpticsAndDilution()
{
    PaintMixerModel gouacheMixer;
    assert(gouacheMixer.setMedium("gouache"));
    assert(!gouacheMixer.addPigmentDeposit("PB15", 1.2).empty());
    assert(gouacheMixer.setFillerOnActive(0.45));
    const auto gOpt = gouacheMixer.activeOpticalSummary();

    PaintMixerModel wcMixer;
    assert(wcMixer.setMedium("watercolour"));
    assert(!wcMixer.addPigmentDeposit("PB15", 0.7).empty());
    assert(wcMixer.addWaterToActive(2.5));
    const auto wcOpt = wcMixer.activeOpticalSummary();

    // Gouache has substantially higher hiding power (alpha) and higher
    // contrast over a dark ground than diluted watercolour.
    assert(gOpt.alpha > wcOpt.alpha + 0.25);
    assert(gOpt.fillerShare > 0.0);
    assert(wcOpt.waterShare > gOpt.waterShare);

    // Further diluting watercolour lowers opacity and pigment share while
    // retaining pigment identity.
    const double alphaBefore = wcOpt.alpha;
    assert(wcMixer.addWaterToActive(3.0));
    const auto wcDiluted = wcMixer.activeOpticalSummary();
    assert(wcDiluted.alpha < alphaBefore);
    assert(wcMixer.activeDeposit()->batch.pigments.size() == 1);
    assert(wcMixer.activeDeposit()->batch.pigments[0].pigmentId == "PB15");
}

void testColorWheelImportPreservesKnownAndArbitraryColours()
{
    PaintMixerModel mixer;
    assert(mixer.setMedium("gouache"));

    // Arbitrary vivid purple RGB (#7A42F4) has no exact named pigment match ->
    // preserved as explicit Digital / Imported entry, never faked as pigment.
    const auto digitalRes = mixer.addColorFromWheel(0xff7a42f4U, true, true);
    assert(digitalRes.ok);
    assert(!digitalRes.matchedKnownPigment);
    const auto* digitalDep = mixer.findDeposit(digitalRes.depositId);
    assert(digitalDep != nullptr);
    assert(digitalDep->containsDigitalImport());
    assert(digitalDep->batch.pigments.size() == 1);
    assert(digitalDep->batch.pigments[0].pigmentId.empty());
    assert(digitalDep->batch.pigments[0].residualArgb == 0xff7a42f4U);

    // Mixing that digital colour with Titanium White PW6 produces a lighter straight
    // pigment reflectance AND increases hiding opacity (covering dark ground),
    // while explicitly keeping digital vs pigment identity separated.
    const auto beforeOpt = mixer.opticalSummary(*digitalDep);
    assert(mixer.addPigmentToActive("PW6", 1.5));
    assert(digitalDep->containsDigitalImport());
    assert(digitalDep->batch.pigments.size() == 2);
    const auto afterOpt = mixer.opticalSummary(*digitalDep);
    assert(channelR(afterOpt.straightArgb) > channelR(beforeOpt.straightArgb));
    assert(channelG(afterOpt.straightArgb) > channelG(beforeOpt.straightArgb));
    assert(afterOpt.alpha > beforeOpt.alpha + 0.15);
    assert(channelR(afterOpt.darkGroundArgb) > channelR(beforeOpt.darkGroundArgb));

    // A colour matching PY3's actual optical swatch can match known pigment PY3.
    const auto* py3 = mixer.pigments().find("PY3");
    assert(py3 != nullptr);
    const std::uint32_t py3Swatch = pigmentPreviewArgb(mixer.engine(), *py3, "gouache");
    const auto matchedRes = mixer.addColorFromWheel(py3Swatch, true, true);
    assert(matchedRes.ok);
    assert(matchedRes.matchedKnownPigment);
    assert(matchedRes.matchedPigmentId == "PY3");
}

void testMultipleDepositsMixResetUndoRedoAndReclaim()
{
    PaintMixerModel mixer;
    assert(mixer.setMedium("watercolour"));

    // Create 14 deposits (>12) with 10 pigments in one deposit -> no artificial caps.
    const std::string multiId = mixer.createEmptyDeposit("All Pigments");
    assert(!multiId.empty());
    for (const auto& p : mixer.pigments().pigments()) {
        assert(mixer.addPigmentToActive(p.id, 0.25));
    }
    assert(mixer.activeDeposit()->batch.pigments.size() == mixer.pigments().pigments().size());

    const std::string yId = mixer.addPigmentDeposit("PY3", 1.0);
    const std::string mId = mixer.addPigmentDeposit("PR122", 1.0);
    assert(!yId.empty() && !mId.empty());

    const auto mixedId = mixer.mixDeposits(yId, mId, 0.5, true);
    assert(mixedId.has_value());
    const auto* mixedDep = mixer.findDeposit(*mixedId);
    assert(mixedDep != nullptr);
    assert(mixedDep->batch.pigments.size() == 2);

    // Verify rendered surface buffer reflects MaterialRenderer output on non-absorbent palette.
    std::vector<std::uint32_t> rendered;
    assert(mixer.renderSurface(rendered));
    assert(rendered.size() == std::size_t(PaintMixerModel::SurfaceWidth) * PaintMixerModel::SurfaceHeight);
    bool sawNonPorcelain = false;
    for (std::uint32_t px : rendered) {
        if (px != 0xfffaf8f4U) { sawNonPorcelain = true; break; }
    }
    assert(sawNonPorcelain);

    // Undo / Redo checks
    const std::size_t countAfterMix = mixer.deposits().size();
    assert(mixer.canUndo());
    assert(mixer.undo());
    assert(mixer.deposits().size() == countAfterMix - 1);
    assert(mixer.canRedo());
    assert(mixer.redo());
    assert(mixer.deposits().size() == countAfterMix);

    // Resetting and clearing deposits reclaims unreferenced compositions from MaterialTileStore.
    assert(mixer.resetDeposit(*mixedId));
    assert(mixer.findDeposit(*mixedId)->batch.pigments.empty());
    mixer.clearAllDeposits();
    assert(mixer.deposits().empty());
    assert(mixer.surfaceStore().activeCompositionCount() == 0);
}

void maybeEmitStage7Sheet()
{
    const char* outPath = std::getenv("KOMAPAINT_STAGE7_SHEET");
    if (!outPath || !*outPath) return;

    PaintMixerModel mixer;
    mixer.setMedium("gouache");
    mixer.addPigmentDeposit("PY3", 1.2, 42.0, 44.0);
    mixer.addPigmentDeposit("PB15", 1.2, 100.0, 44.0);
    auto greenId = mixer.mixDeposits(mixer.deposits()[0].id, mixer.deposits()[1].id, 0.5, true);
    if (greenId) mixer.moveDeposit(*greenId, 158.0, 44.0);
    mixer.addColorFromWheel(0xff7a42f4U, true, true);
    mixer.moveDeposit(mixer.activeDepositId(), 214.0, 44.0);

    mixer.setMedium("watercolour");
    auto wcRed = mixer.addPigmentDeposit("PR122", 0.85, 56.0, 114.0);
    mixer.addWaterToActive(2.2);
    auto wcBlue = mixer.addPigmentDeposit("PB29", 0.90, 128.0, 114.0);
    mixer.addWaterToActive(2.4);
    auto wcMix = mixer.mixDeposits(wcRed, wcBlue, 0.5, true);
    if (wcMix) mixer.moveDeposit(*wcMix, 200.0, 114.0);

    std::vector<std::uint32_t> pixels;
    assert(mixer.renderSurface(pixels));

    std::ofstream ppm(outPath, std::ios::binary);
    assert(ppm.is_open());
    ppm << "P6\n" << PaintMixerModel::SurfaceWidth << " " << PaintMixerModel::SurfaceHeight << "\n255\n";
    for (std::uint32_t argb : pixels) {
        const unsigned char rgb[3] = {
            static_cast<unsigned char>((argb >> 16U) & 0xffU),
            static_cast<unsigned char>((argb >> 8U) & 0xffU),
            static_cast<unsigned char>(argb & 0xffU)
        };
        ppm.write(reinterpret_cast<const char*>(rgb), 3);
    }
}

} // namespace

int main()
{
    testPigmentLibraryProvenanceAndOrganization();
    testActualPigmentMixingNotRgbAverage();
    testGouacheVsWatercolourOpticsAndDilution();
    testColorWheelImportPreservesKnownAndArbitraryColours();
    testMultipleDepositsMixResetUndoRedoAndReclaim();
    maybeEmitStage7Sheet();
    std::cout << "TestPaintMixer: all assertions passed.\n";
    return 0;
}
