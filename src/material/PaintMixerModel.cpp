#include "PaintMixerModel.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace komapaint::material {
namespace {

constexpr std::uint32_t PorcelainArgb = 0xfffaf8f4U;
constexpr std::uint32_t DarkGroundArgb = 0xff282a2eU;
constexpr std::size_t MaxMixerUndoSteps = 200;

std::uint32_t blendOverOpaque(std::uint32_t dstOpaque, std::uint32_t srcStraight,
                              double alpha) noexcept
{
    const double a = std::clamp(alpha * double((srcStraight >> 24U) & 0xffU) / 255.0, 0.0, 1.0);
    const double inv = 1.0 - a;
    std::uint32_t out = 0xff000000U;
    for (int shift : {16, 8, 0}) {
        const double srcC = double((srcStraight >> shift) & 0xffU);
        const double dstC = double((dstOpaque >> shift) & 0xffU);
        const auto ch = static_cast<std::uint32_t>(std::clamp(std::lround(srcC * a + dstC * inv), 0L, 255L));
        out |= (ch << shift);
    }
    return out;
}

std::string hexStringOf(std::uint32_t argb)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out = "#";
    for (int shift = 20; shift >= 0; shift -= 4)
        out.push_back(kHex[(argb >> shift) & 0xfU]);
    return out;
}

void syncDerivedBatchState(PaintBatch& batch) noexcept
{
    const double total = batch.totalMass();
    if (total <= 1e-12) {
        batch.wetness = 0.0;
        batch.consistency = 0.0;
        return;
    }
    batch.wetness = std::clamp(batch.water / total, 0.0, 1.0);
    batch.consistency = std::clamp((batch.pigmentMass() + batch.binder + batch.filler) / total, 0.0, 1.0);
}

} // namespace

bool MixerDeposit::containsDigitalImport() const noexcept
{
    for (const auto& c : batch.pigments) {
        if (c.pigmentId.empty() && c.residualArgb != 0) return true;
    }
    return false;
}

PaintMixerModel::PaintMixerModel()
    : m_engine(m_pigments),
      m_surfaceStore(SurfaceWidth, SurfaceHeight, SurfaceTileSize)
{
    m_history.push_back(HistorySnapshot{"Initial palette", m_mediumId, {}, {}});
    m_undoIndex = 0;
}

void PaintMixerModel::defaultWaterBinderFiller(const std::string& mediumId,
                                               double& water, double& binder,
                                               double& filler) const noexcept
{
    if (mediumId == "watercolour") {
        water = 1.85;
        binder = 0.12;
        filler = 0.0;
    } else {
        water = 0.55;
        binder = 0.44;
        filler = 0.10;
    }
}

PaintBatch PaintMixerModel::defaultBatchForMedium(const std::string& mediumId) const
{
    PaintBatch batch;
    batch.mediumId = (mediumId == "watercolour") ? "watercolour" : "gouache";
    defaultWaterBinderFiller(batch.mediumId, batch.water, batch.binder, batch.filler);
    syncDerivedBatchState(batch);
    return batch;
}

std::string PaintMixerModel::nextDepositId()
{
    std::ostringstream oss;
    oss << "deposit-" << m_nextId++;
    return oss.str();
}

std::pair<double, double> PaintMixerModel::nextAvailableSpot() const noexcept
{
    static constexpr double kSlots[][2] = {
        {48.0, 44.0}, {112.0, 44.0}, {176.0, 44.0}, {224.0, 44.0},
        {48.0, 108.0}, {112.0, 108.0}, {176.0, 108.0}, {224.0, 108.0}
    };
    for (const auto& slot : kSlots) {
        bool occupied = false;
        for (const auto& d : m_deposits) {
            const double dx = d.x - slot[0];
            const double dy = d.y - slot[1];
            if (dx * dx + dy * dy < 30.0 * 30.0) {
                occupied = true;
                break;
            }
        }
        if (!occupied) return {slot[0], slot[1]};
    }
    const std::size_t idx = m_deposits.size();
    const double x = 36.0 + double((idx * 43) % (SurfaceWidth - 72));
    const double y = 36.0 + double(((idx * 31) / 3) % (SurfaceHeight - 72));
    return {x, y};
}

MixerDeposit* PaintMixerModel::mutableDeposit(const std::string& id) noexcept
{
    for (auto& d : m_deposits) {
        if (d.id == id) return &d;
    }
    return nullptr;
}

const MixerDeposit* PaintMixerModel::findDeposit(const std::string& id) const noexcept
{
    for (const auto& d : m_deposits) {
        if (d.id == id) return &d;
    }
    return nullptr;
}

const MixerDeposit* PaintMixerModel::activeDeposit() const noexcept
{
    return findDeposit(m_activeDepositId);
}

bool PaintMixerModel::selectDeposit(const std::string& id) noexcept
{
    if (!findDeposit(id)) return false;
    m_activeDepositId = id;
    return true;
}

std::optional<std::string> PaintMixerModel::depositAt(double x, double y) const noexcept
{
    for (auto it = m_deposits.rbegin(); it != m_deposits.rend(); ++it) {
        const double dx = x - it->x;
        const double dy = y - it->y;
        const double hitR = std::max(14.0, it->radius + 4.0);
        if (dx * dx + dy * dy <= hitR * hitR) return it->id;
    }
    return std::nullopt;
}

bool PaintMixerModel::setMedium(const std::string& mediumId)
{
    const std::string normalized = (mediumId == "watercolour") ? "watercolour" : "gouache";
    if (normalized != mediumId && mediumId != "gouache" && mediumId != "watercolour")
        return false;
    bool changed = (m_mediumId != normalized);
    if (auto* active = mutableDeposit(m_activeDepositId)) {
        if (active->batch.mediumId != normalized) {
            active->batch.mediumId = normalized;
            if (normalized == "watercolour") {
                if (active->batch.water < 0.8) active->batch.water = 1.6;
                active->batch.filler = 0.0;
            } else {
                if (active->batch.binder < 0.25) active->batch.binder = 0.42;
            }
            syncDerivedBatchState(active->batch);
            changed = true;
        }
    }
    m_mediumId = normalized;
    if (changed) {
        pushHistory(normalized == "watercolour" ? "Switch to Watercolour" : "Switch to Gouache");
        rebuildSurfaceTiles();
    }
    return true;
}

std::string PaintMixerModel::createDeposit(const std::string& name, const PaintBatch& batch,
                                           double x, double y)
{
    if (!batch.valid() || !findMediumProfile(batch.mediumId)) return {};
    MixerDeposit d;
    d.id = nextDepositId();
    d.name = name.empty() ? ("Mixture " + std::to_string(m_deposits.size() + 1)) : name;
    if (x < 0.0 || y < 0.0) {
        const auto spot = nextAvailableSpot();
        d.x = spot.first;
        d.y = spot.second;
    } else {
        d.x = std::clamp(x, 18.0, double(SurfaceWidth - 18));
        d.y = std::clamp(y, 18.0, double(SurfaceHeight - 18));
    }
    d.batch = batch;
    syncDerivedBatchState(d.batch);
    m_mediumId = d.batch.mediumId;
    m_deposits.push_back(d);
    m_activeDepositId = d.id;
    pushHistory("Add deposit");
    rebuildSurfaceTiles();
    return d.id;
}

std::string PaintMixerModel::createEmptyDeposit(const std::string& name, double x, double y)
{
    return createDeposit(name.empty() ? ("Well " + std::to_string(m_deposits.size() + 1)) : name,
                         defaultBatchForMedium(m_mediumId), x, y);
}

bool PaintMixerModel::replaceDepositBatch(const std::string& id, const PaintBatch& batch,
                                          const std::string& newName, const std::string& undoLabel)
{
    auto* target = mutableDeposit(id);
    if (!target || !batch.valid() || !findMediumProfile(batch.mediumId)) return false;
    target->batch = batch;
    syncDerivedBatchState(target->batch);
    if (!newName.empty()) target->name = newName;
    m_mediumId = target->batch.mediumId;
    pushHistory(undoLabel);
    rebuildSurfaceTiles();
    return true;
}

bool PaintMixerModel::renameDeposit(const std::string& id, const std::string& name)
{
    auto* target = mutableDeposit(id);
    if (!target || name.empty() || target->name == name) return false;
    target->name = name;
    pushHistory("Rename deposit");
    return true;
}

bool PaintMixerModel::moveDeposit(const std::string& id, double x, double y)
{
    auto* target = mutableDeposit(id);
    if (!target || !std::isfinite(x) || !std::isfinite(y)) return false;
    const double nx = std::clamp(x, 16.0, double(SurfaceWidth - 16));
    const double ny = std::clamp(y, 16.0, double(SurfaceHeight - 16));
    if (std::abs(target->x - nx) < 0.25 && std::abs(target->y - ny) < 0.25) return false;
    target->x = nx;
    target->y = ny;
    pushHistory("Move deposit");
    rebuildSurfaceTiles();
    return true;
}

bool PaintMixerModel::resetDeposit(const std::string& id)
{
    auto* target = mutableDeposit(id);
    if (!target) return false;
    target->batch = defaultBatchForMedium(target->batch.mediumId);
    pushHistory("Reset deposit");
    rebuildSurfaceTiles();
    return true;
}

bool PaintMixerModel::removeDeposit(const std::string& id)
{
    const auto it = std::find_if(m_deposits.begin(), m_deposits.end(),
                                 [&id](const auto& d) { return d.id == id; });
    if (it == m_deposits.end()) return false;
    const std::size_t removedIndex = static_cast<std::size_t>(std::distance(m_deposits.begin(), it));
    m_deposits.erase(it);
    if (m_activeDepositId == id) {
        if (m_deposits.empty()) m_activeDepositId.clear();
        else m_activeDepositId = m_deposits[std::min(removedIndex, m_deposits.size() - 1)].id;
    }
    pushHistory("Remove deposit");
    rebuildSurfaceTiles();
    return true;
}

void PaintMixerModel::clearAllDeposits()
{
    if (m_deposits.empty()) return;
    m_deposits.clear();
    m_activeDepositId.clear();
    pushHistory("Clear palette");
    rebuildSurfaceTiles();
}

bool PaintMixerModel::addPigmentToActive(const std::string& pigmentId, double quantity,
                                         bool createIfNone)
{
    if (!std::isfinite(quantity) || quantity <= 0.0) return false;
    const PigmentDefinition* pigment = m_pigments.find(pigmentId);
    if (!pigment) return false;

    auto* active = mutableDeposit(m_activeDepositId);
    if (!active) {
        if (!createIfNone) return false;
        return !addPigmentDeposit(pigmentId, quantity).empty();
    }
    if (!active->batch.addPigment(pigmentId, quantity)) return false;
    syncDerivedBatchState(active->batch);
    pushHistory("Add " + pigment->id);
    rebuildSurfaceTiles();
    return true;
}

std::string PaintMixerModel::addPigmentDeposit(const std::string& pigmentId, double quantity,
                                               double x, double y)
{
    if (!std::isfinite(quantity) || quantity <= 0.0) return {};
    const PigmentDefinition* pigment = m_pigments.find(pigmentId);
    if (!pigment) return {};
    PaintBatch batch = defaultBatchForMedium(m_mediumId);
    if (!batch.addPigment(pigmentId, quantity)) return {};
    return createDeposit(pigment->name, batch, x, y);
}

PaintMixerModel::ColorImportResult PaintMixerModel::addColorFromWheel(std::uint32_t straightArgb,
                                                                      bool allowPigmentMatch,
                                                                      bool createNewDeposit,
                                                                      double quantity)
{
    ColorImportResult result;
    if (!std::isfinite(quantity) || quantity <= 0.0) return result;
    const std::uint32_t opaqueArgb = (straightArgb & 0x00ffffffU) | 0xff000000U;
    std::optional<std::string> matched;
    if (allowPigmentMatch)
        matched = matchKnownPigmentForRgb(m_engine, opaqueArgb, m_mediumId, 10.0);

    if (matched) {
        result.matchedKnownPigment = true;
        result.matchedPigmentId = *matched;
        if (createNewDeposit || !mutableDeposit(m_activeDepositId)) {
            result.depositId = addPigmentDeposit(*matched, quantity);
            result.ok = !result.depositId.empty();
        } else {
            result.ok = addPigmentToActive(*matched, quantity, true);
            result.depositId = m_activeDepositId;
        }
        return result;
    }

    if (createNewDeposit || !mutableDeposit(m_activeDepositId)) {
        PaintBatch batch = defaultBatchForMedium(m_mediumId);
        if (!batch.addResidual(opaqueArgb, quantity)) return result;
        syncDerivedBatchState(batch);
        result.depositId = createDeposit("Digital " + hexStringOf(opaqueArgb), batch);
        result.ok = !result.depositId.empty();
        return result;
    }

    if (!addResidualToActive(opaqueArgb, quantity)) return result;
    result.ok = true;
    result.depositId = m_activeDepositId;
    return result;
}

bool PaintMixerModel::addResidualToActive(std::uint32_t straightArgb, double quantity)
{
    if (!std::isfinite(quantity) || quantity <= 0.0) return false;
    const std::uint32_t opaqueArgb = (straightArgb & 0x00ffffffU) | 0xff000000U;
    auto* active = mutableDeposit(m_activeDepositId);
    if (!active) {
        PaintBatch batch = defaultBatchForMedium(m_mediumId);
        if (!batch.addResidual(opaqueArgb, quantity)) return false;
        return !createDeposit("Digital " + hexStringOf(opaqueArgb), batch).empty();
    }
    if (!active->batch.addResidual(opaqueArgb, quantity)) return false;
    syncDerivedBatchState(active->batch);
    pushHistory("Add digital colour");
    rebuildSurfaceTiles();
    return true;
}

bool PaintMixerModel::setComponentMass(std::size_t index, double mass)
{
    auto* active = mutableDeposit(m_activeDepositId);
    if (!active || index >= active->batch.pigments.size() || !std::isfinite(mass)) return false;
    if (mass <= 1e-6) return removeComponent(index);
    active->batch.pigments[index].mass = mass;
    syncDerivedBatchState(active->batch);
    pushHistory("Adjust pigment amount");
    rebuildSurfaceTiles();
    return true;
}

bool PaintMixerModel::removeComponent(std::size_t index)
{
    auto* active = mutableDeposit(m_activeDepositId);
    if (!active || index >= active->batch.pigments.size()) return false;
    active->batch.pigments.erase(active->batch.pigments.begin() + static_cast<std::ptrdiff_t>(index));
    syncDerivedBatchState(active->batch);
    pushHistory("Remove pigment");
    rebuildSurfaceTiles();
    return true;
}

bool PaintMixerModel::addWaterToActive(double delta)
{
    auto* active = mutableDeposit(m_activeDepositId);
    if (!active || !std::isfinite(delta)) return false;
    const double next = std::max(0.0, active->batch.water + delta);
    if (std::abs(next - active->batch.water) < 1e-9) return false;
    if (delta > 0.0) active->batch.addWater(delta);
    else {
        active->batch.water = next;
        syncDerivedBatchState(active->batch);
    }
    pushHistory(delta >= 0.0 ? "Add water" : "Reduce water");
    rebuildSurfaceTiles();
    return true;
}

bool PaintMixerModel::setWaterOnActive(double water)
{
    auto* active = mutableDeposit(m_activeDepositId);
    if (!active || !std::isfinite(water) || water < 0.0) return false;
    if (std::abs(water - active->batch.water) < 1e-9) return false;
    active->batch.water = water;
    syncDerivedBatchState(active->batch);
    pushHistory("Adjust water");
    rebuildSurfaceTiles();
    return true;
}

bool PaintMixerModel::setBinderOnActive(double binder)
{
    auto* active = mutableDeposit(m_activeDepositId);
    if (!active || !std::isfinite(binder) || binder < 0.0) return false;
    if (std::abs(binder - active->batch.binder) < 1e-9) return false;
    active->batch.binder = binder;
    syncDerivedBatchState(active->batch);
    pushHistory("Adjust binder");
    rebuildSurfaceTiles();
    return true;
}

bool PaintMixerModel::setFillerOnActive(double filler)
{
    auto* active = mutableDeposit(m_activeDepositId);
    if (!active || !std::isfinite(filler) || filler < 0.0) return false;
    if (std::abs(filler - active->batch.filler) < 1e-9) return false;
    active->batch.filler = filler;
    syncDerivedBatchState(active->batch);
    pushHistory("Adjust body/filler");
    rebuildSurfaceTiles();
    return true;
}

bool PaintMixerModel::addPaintBodyToActive(double factorDelta)
{
    auto* active = mutableDeposit(m_activeDepositId);
    if (!active || !std::isfinite(factorDelta) || factorDelta <= -1.0) return false;
    const double scaleFactor = 1.0 + factorDelta;
    if (scaleFactor <= 0.0) return false;
    for (auto& c : active->batch.pigments) c.mass *= scaleFactor;
    active->batch.binder *= scaleFactor;
    active->batch.filler *= scaleFactor;
    syncDerivedBatchState(active->batch);
    pushHistory("Add paint body");
    rebuildSurfaceTiles();
    return true;
}

std::optional<std::string> PaintMixerModel::mixDeposits(const std::string& sourceId,
                                                        const std::string& targetId,
                                                        double sourceShare,
                                                        bool createNew)
{
    if (sourceId == targetId || !std::isfinite(sourceShare)) return std::nullopt;
    const auto* src = findDeposit(sourceId);
    const auto* dst = findDeposit(targetId);
    if (!src || !dst) return std::nullopt;

    const double clampedShare = std::clamp(sourceShare, 0.05, 0.95);
    PaintBatch a = dst->batch;
    PaintBatch b = src->batch;
    const double aMass = std::max(1e-6, a.totalMass());
    const double bMass = std::max(1e-6, b.totalMass());
    const double targetTotal = std::max(aMass, bMass);
    a.scale(((1.0 - clampedShare) * targetTotal) / aMass);
    b.scale((clampedShare * targetTotal) / bMass);
    PaintBatch combined = mix(a, b, m_mediumId);

    if (createNew) {
        const std::string mixedName = dst->name + " + " + src->name;
        const double mx = 0.5 * (src->x + dst->x);
        const double my = 0.5 * (src->y + dst->y);
        const std::string id = createDeposit(mixedName, combined, mx, my);
        return id.empty() ? std::nullopt : std::optional<std::string>(id);
    }

    auto* targetMut = mutableDeposit(targetId);
    if (!targetMut) return std::nullopt;
    targetMut->batch = std::move(combined);
    syncDerivedBatchState(targetMut->batch);
    m_activeDepositId = targetMut->id;
    pushHistory("Mix " + src->name + " into " + targetMut->name);
    rebuildSurfaceTiles();
    return targetMut->id;
}

DepositOpticalSummary PaintMixerModel::opticalSummary(const MixerDeposit& deposit,
                                                      const SurfaceProfile* surface) const noexcept
{
    DepositOpticalSummary summary;
    const MediumProfile* medium = findMediumProfile(deposit.batch.mediumId);
    if (!medium) medium = findMediumProfile(m_mediumId);
    if (!medium) medium = &gouacheProfile();
    const SurfaceProfile& substrate = surface ? *surface : nonAbsorbentPalette();

    const OpticalResult optical = m_engine.opticalResult(deposit.batch, *medium, substrate, 1.0);
    summary.straightArgb = optical.straightArgb;
    summary.alpha = optical.alpha;
    summary.granulation = optical.granulation;
    summary.porcelainArgb = optical.alpha > 0.0
        ? blendOverOpaque(PorcelainArgb, optical.straightArgb, optical.alpha)
        : PorcelainArgb;
    summary.darkGroundArgb = optical.alpha > 0.0
        ? blendOverOpaque(DarkGroundArgb, optical.straightArgb, optical.alpha)
        : DarkGroundArgb;

    const double total = deposit.batch.totalMass();
    if (total > 1e-12) {
        summary.pigmentShare = deposit.batch.pigmentMass() / total;
        summary.waterShare = deposit.batch.water / total;
        summary.binderShare = deposit.batch.binder / total;
        summary.fillerShare = deposit.batch.filler / total;
    }
    return summary;
}

DepositOpticalSummary PaintMixerModel::activeOpticalSummary(const SurfaceProfile* surface) const noexcept
{
    if (const auto* active = activeDeposit())
        return opticalSummary(*active, surface);
    return DepositOpticalSummary{};
}

void PaintMixerModel::rebuildSurfaceTiles()
{
    // Clear existing tiles cleanly and restamp every deposit via MaterialTileStore,
    // then reclaim unreferenced composition records so storage stays deduplicated.
    const auto allocated = m_surfaceStore.allocatedTilePoints();
    if (!allocated.empty()) {
        std::vector<MaterialTileSnapshot> emptySnapshots;
        emptySnapshots.reserve(allocated.size());
        for (const TilePoint tile : allocated)
            emptySnapshots.push_back(MaterialTileSnapshot{tile, {}});
        m_surfaceStore.restoreTiles(emptySnapshots, false);
    }

    m_surfaceStore.beginBatch();
    for (const auto& deposit : m_deposits) {
        if (deposit.batch.totalMass() <= 1e-9) continue;
        const int r = static_cast<int>(std::ceil(deposit.radius));
        const int x0 = std::max(0, static_cast<int>(std::floor(deposit.x)) - r);
        const int y0 = std::max(0, static_cast<int>(std::floor(deposit.y)) - r);
        const int x1 = std::min(SurfaceWidth - 1, static_cast<int>(std::ceil(deposit.x)) + r);
        const int y1 = std::min(SurfaceHeight - 1, static_cast<int>(std::ceil(deposit.y)) + r);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const double dx = (double(x) + 0.5) - deposit.x;
                const double dy = (double(y) + 0.5) - deposit.y;
                const double distNorm = std::sqrt(dx * dx + dy * dy) / std::max(4.0, deposit.radius);
                if (distNorm > 1.0) continue;
                // Smooth domed puddle profile: fuller center, soft feathered rim.
                // MaterialRenderer::renderTile scales per-pixel cell quantities
                // by ReferenceBatchPixels (16384.0) and applies non-absorbent
                // palette retention (~0.42x), so scale the physical puddle dome
                // thickness accordingly for crisp, readable palette visualization.
                constexpr double kReferenceBatchPixels = 16384.0;
                constexpr double kPalettePuddleThickness = 2.8;
                const double profile = 0.32 + 0.68 * std::pow(1.0 - distNorm * distNorm, 0.55);
                PaintBatch cellBatch = deposit.batch;
                cellBatch.scale((profile * kPalettePuddleThickness) / kReferenceBatchPixels);
                m_surfaceStore.deposit({x, y}, cellBatch);
            }
        }
    }
    m_surfaceStore.endBatch();
    m_surfaceStore.reclaimUnreferenced();
}

bool PaintMixerModel::renderSurface(std::vector<std::uint32_t>& outPremultipliedArgb) const
{
    outPremultipliedArgb.assign(static_cast<std::size_t>(SurfaceWidth) * static_cast<std::size_t>(SurfaceHeight),
                                PorcelainArgb);
    const MaterialRenderer renderer(m_engine);
    const auto allocated = m_surfaceStore.allocatedTilePoints();
    const MediumProfile* activeMedium = findMediumProfile(m_mediumId);
    if (!activeMedium) activeMedium = &gouacheProfile();

    for (const TilePoint tile : allocated) {
        std::vector<std::uint32_t> tilePixels;
        if (!renderer.renderTile(m_surfaceStore, tile, {}, *activeMedium,
                                 nonAbsorbentPalette(), &tilePixels)) {
            return false;
        }
        const int tileX0 = static_cast<int>(tile.x) * SurfaceTileSize;
        const int tileY0 = static_cast<int>(tile.y) * SurfaceTileSize;
        for (int ly = 0; ly < SurfaceTileSize; ++ly) {
            const int py = tileY0 + ly;
            if (py < 0 || py >= SurfaceHeight) continue;
            for (int lx = 0; lx < SurfaceTileSize; ++lx) {
                const int px = tileX0 + lx;
                if (px < 0 || px >= SurfaceWidth) continue;
                const std::uint32_t src = tilePixels[static_cast<std::size_t>(ly) * SurfaceTileSize + static_cast<std::size_t>(lx)];
                if ((src >> 24U) == 0) continue;
                // src is premultiplied ARGB over transparent; composite over porcelain palette
                const std::uint32_t a = (src >> 24U) & 0xffU;
                const double inv = 1.0 - double(a) / 255.0;
                std::uint32_t out = 0xff000000U;
                for (int shift : {16, 8, 0}) {
                    const double srcPremul = double((src >> shift) & 0xffU);
                    const double dstC = double((PorcelainArgb >> shift) & 0xffU);
                    const auto ch = static_cast<std::uint32_t>(std::clamp(std::lround(srcPremul + dstC * inv), 0L, 255L));
                    out |= (ch << shift);
                }
                outPremultipliedArgb[static_cast<std::size_t>(py) * SurfaceWidth + static_cast<std::size_t>(px)] = out;
            }
        }
    }
    return true;
}

void PaintMixerModel::pushHistory(std::string label)
{
    if (m_undoIndex + 1 < m_history.size())
        m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(m_undoIndex + 1), m_history.end());
    m_history.push_back(HistorySnapshot{std::move(label), m_mediumId, m_activeDepositId, m_deposits});
    if (m_history.size() > MaxMixerUndoSteps) {
        const std::size_t overflow = m_history.size() - MaxMixerUndoSteps;
        m_history.erase(m_history.begin(), m_history.begin() + static_cast<std::ptrdiff_t>(overflow));
    }
    m_undoIndex = m_history.size() - 1;
}

std::string PaintMixerModel::undoLabel() const
{
    return canUndo() ? m_history[m_undoIndex].label : std::string{};
}

std::string PaintMixerModel::redoLabel() const
{
    return canRedo() ? m_history[m_undoIndex + 1].label : std::string{};
}

bool PaintMixerModel::undo()
{
    if (!canUndo()) return false;
    --m_undoIndex;
    const auto& snap = m_history[m_undoIndex];
    m_mediumId = snap.mediumId;
    m_activeDepositId = snap.activeDepositId;
    m_deposits = snap.deposits;
    rebuildSurfaceTiles();
    return true;
}

bool PaintMixerModel::redo()
{
    if (!canRedo()) return false;
    ++m_undoIndex;
    const auto& snap = m_history[m_undoIndex];
    m_mediumId = snap.mediumId;
    m_activeDepositId = snap.activeDepositId;
    m_deposits = snap.deposits;
    rebuildSurfaceTiles();
    return true;
}

void PaintMixerModel::clearHistory()
{
    m_history.clear();
    m_history.push_back(HistorySnapshot{"Initial palette", m_mediumId, m_activeDepositId, m_deposits});
    m_undoIndex = 0;
}

void PaintMixerModel::restoreWorkspaceState(const std::string& mediumId,
                                          std::vector<MixerDeposit> deposits,
                                          const std::string& activeDepositId)
{
    m_mediumId = (mediumId == "watercolour") ? "watercolour" : "gouache";
    m_deposits = std::move(deposits);
    for (const auto& d : m_deposits) {
        if (d.id.rfind("deposit-", 0) == 0) {
            try {
                const auto num = std::stoull(d.id.substr(8));
                if (num >= m_nextId) m_nextId = num + 1;
            } catch (...) {}
        }
    }
    m_activeDepositId = activeDepositId;
    if (!m_activeDepositId.empty() && !findDeposit(m_activeDepositId))
        m_activeDepositId = m_deposits.empty() ? std::string{} : m_deposits.front().id;
    clearHistory();
    rebuildSurfaceTiles();
}

} // namespace komapaint::material

