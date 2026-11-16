#include "MaterialTileStore.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace komapaint::material {
namespace {

std::size_t combineHash(std::size_t seed, std::size_t value) noexcept
{
    return seed ^ (value + std::size_t{0x9e3779b97f4a7c15ULL} + (seed << 6) + (seed >> 2));
}

bool finiteFloat(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0
        && value <= static_cast<double>(std::numeric_limits<float>::max());
}

bool mobileCell(const MaterialCell& cell) noexcept
{
    return !cell.empty() && cell.water > 1.0e-5F && cell.drying < 0.9999F;
}

bool emptyComposition(const MaterialComposition& composition) noexcept
{
    return composition.mediumId.empty() && composition.pigments.empty();
}

std::size_t compositionCharge(const MaterialComposition& composition) noexcept
{
    return sizeof(MaterialComposition) + composition.mediumId.capacity()
        + composition.pigments.capacity() * sizeof(PigmentComponent) + 3 * sizeof(std::size_t);
}

bool validProfile(const MaterialComposition& composition) noexcept
{
    if (composition.mediumId.empty()) return false;
    double sum = 0.0;
    for (const auto& pigment : composition.pigments) {
        if (!std::isfinite(pigment.mass) || pigment.mass <= 0.0
            || (pigment.pigmentId.empty() && (pigment.residualArgb >> 24U) != 0xffU)
            || (!pigment.pigmentId.empty() && pigment.residualArgb != 0)) return false;
        sum += pigment.mass;
        if (!std::isfinite(sum)) return false;
    }
    return composition.pigments.empty() || std::abs(sum - 1.0) <= 1e-9;
}

} // namespace

bool MaterialCell::empty() const noexcept
{
    return compositionId == 0 && pigmentMass == 0.0F && water == 0.0F
        && binder == 0.0F && filler == 0.0F;
}

std::size_t MaterialTileStore::Hash::operator()(TilePoint point) const noexcept
{
    const auto a = std::hash<std::int64_t>{}(point.x);
    const auto b = std::hash<std::int64_t>{}(point.y);
    return combineHash(a, b);
}

MaterialTileStore::MaterialTileStore(std::int64_t width, std::int64_t height, int tileSize,
                                     std::shared_ptr<DocumentMemoryBudget> budget)
    : m_geometry(width, height, tileSize), m_dirty(m_geometry), m_budget(std::move(budget))
{
    // ID zero is an explicit empty sentinel. Real compositions are immutable
    // while referenced; unreferenced and unpinned entries can be reclaimed.
    m_compositions.emplace_back();
    m_compositionHashes.push_back(0);
    m_compositionLiveRefs.push_back(0);
    m_compositionPinRefs.push_back(0);
}

MaterialTileStore::~MaterialTileStore() { releaseAll(); }

void MaterialTileStore::releaseAll() noexcept
{
    if (m_budget) m_budget->release(DocumentMemoryBudget::Category::Artwork,
                                    m_allocatedBytes + m_compositionBytes + m_activeTileBytes);
    m_allocatedBytes = 0;
    m_compositionBytes = 0;
    m_activeTileBytes = 0;
    m_activeTiles.clear();
}

bool MaterialTileStore::setDocumentBudget(std::shared_ptr<DocumentMemoryBudget> budget) noexcept
{
    if (!m_tiles.empty() || m_compositions.size() > 1 || m_beforeChange || m_batchDepth) return false;
    if (budget == m_budget) return true;
    if (budget && m_activeTileBytes
        && !budget->reserve(DocumentMemoryBudget::Category::Artwork, m_activeTileBytes)) return false;
    if (m_budget && m_activeTileBytes)
        m_budget->release(DocumentMemoryBudget::Category::Artwork, m_activeTileBytes);
    m_budget = std::move(budget);
    return true;
}

bool MaterialTileStore::resizeEmpty(std::int64_t width, std::int64_t height) noexcept
{
    TileGeometry replacement(width, height, m_geometry.tileSize());
    if (!m_tiles.empty() || m_compositions.size() > 1 || !m_dirty.regions().empty()
        || m_beforeChange || m_batchDepth || !replacement.isValid()) return false;
    if (!m_dirty.resetGeometry(replacement)) return false;
    m_geometry = replacement;
    return true;
}

std::vector<TilePoint> MaterialTileStore::allocatedTilePoints() const
{
    std::vector<TilePoint> result;
    result.reserve(m_tiles.size());
    for (const auto& entry : m_tiles)
        if (!entry.second.entries.empty()) result.push_back(entry.first);
    return result;
}

std::vector<TilePoint> MaterialTileStore::activeTilePoints() const
{
    return m_activeTiles;
}

std::span<const MaterialEntry> MaterialTileStore::tileEntries(TilePoint tile) const noexcept
{
    const auto it = m_tiles.find(tile);
    return it == m_tiles.end() ? std::span<const MaterialEntry>{}
                               : std::span<const MaterialEntry>(it->second.entries);
}

std::optional<MaterialCell> MaterialTileStore::cellAt(TilePoint point) const noexcept
{
    if (!isValid()) return std::nullopt;
    const auto address = m_geometry.address(point);
    if (!address) return std::nullopt;
    const auto tile = m_tiles.find(address->tile);
    if (tile == m_tiles.end()) return MaterialCell{};
    const auto index = static_cast<std::size_t>(address->local.y)
        * static_cast<std::size_t>(m_geometry.tileSize())
        + static_cast<std::size_t>(address->local.x);
    const auto entry = std::lower_bound(tile->second.entries.begin(), tile->second.entries.end(), index,
        [](const MaterialEntry& item, std::size_t value) { return item.localIndex < value; });
    return entry != tile->second.entries.end() && entry->localIndex == index
        ? std::optional<MaterialCell>(entry->cell) : std::optional<MaterialCell>(MaterialCell{});
}

const MaterialComposition* MaterialTileStore::composition(std::size_t id) const noexcept
{
    return id > 0 && id < m_compositions.size() && !m_compositions[id].mediumId.empty()
        ? &m_compositions[id] : nullptr;
}

std::size_t MaterialTileStore::activeCompositionCount() const noexcept
{
    std::size_t count = 0;
    for (std::size_t id = 1; id < m_compositions.size(); ++id)
        if (!m_compositions[id].mediumId.empty()) ++count;
    return count;
}

std::size_t MaterialTileStore::reclaimUnreferenced() noexcept
{
    std::size_t reclaimed = 0;
    for (std::size_t id = 1; id < m_compositions.size(); ++id) {
        if (m_compositions[id].mediumId.empty()) continue;
        if (m_compositionLiveRefs[id] != 0 || m_compositionPinRefs[id] != 0) continue;
        const std::size_t charge = compositionCharge(m_compositions[id]);
        const auto range = m_compositionIndex.equal_range(m_compositionHashes[id]);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second == id) { m_compositionIndex.erase(it); break; }
        }
        m_compositions[id] = MaterialComposition{};
        m_compositionHashes[id] = 0;
        m_compositionBytes -= charge;
        if (m_budget) m_budget->release(DocumentMemoryBudget::Category::Artwork, charge);
        try {
            m_freeCompositionIds.push_back(id);
        } catch (const std::bad_alloc&) {}
          catch (const std::length_error&) {}
        ++reclaimed;
    }
    while (m_compositions.size() > 1 && m_compositions.back().mediumId.empty()
           && m_compositionLiveRefs.back() == 0 && m_compositionPinRefs.back() == 0) {
        const std::size_t tailId = m_compositions.size() - 1;
        m_compositions.pop_back();
        m_compositionHashes.pop_back();
        m_compositionLiveRefs.pop_back();
        m_compositionPinRefs.pop_back();
        m_freeCompositionIds.erase(
            std::remove(m_freeCompositionIds.begin(), m_freeCompositionIds.end(), tailId),
            m_freeCompositionIds.end());
    }
    return reclaimed;
}

void MaterialTileStore::pinSnapshots(std::span<const MaterialTileSnapshot> snapshots) noexcept
{
    for (const auto& snap : snapshots) {
        for (const auto& entry : snap.entries) {
            const std::size_t id = entry.cell.compositionId;
            if (id > 0 && id < m_compositionPinRefs.size())
                ++m_compositionPinRefs[id];
        }
    }
}

void MaterialTileStore::unpinSnapshots(std::span<const MaterialTileSnapshot> snapshots) noexcept
{
    for (const auto& snap : snapshots) {
        for (const auto& entry : snap.entries) {
            const std::size_t id = entry.cell.compositionId;
            if (id > 0 && id < m_compositionPinRefs.size() && m_compositionPinRefs[id] > 0)
                --m_compositionPinRefs[id];
        }
    }
}

std::optional<PaintBatch> MaterialTileStore::expand(const MaterialCell& cell) const
{
    PaintBatch result;
    const MaterialComposition* recipe = composition(cell.compositionId);
    if (!recipe) return std::nullopt;
    result.mediumId = recipe->mediumId;
    result.water = cell.water;
    result.binder = cell.binder;
    result.filler = cell.filler;
    result.wetness = cell.wetness;
    result.consistency = cell.consistency;
    result.drying = cell.drying;
    result.settling = cell.settling;
    for (const auto& pigment : recipe->pigments) {
        const double mass = pigment.mass * static_cast<double>(cell.pigmentMass);
        const bool added = pigment.pigmentId.empty()
            ? result.addResidual(pigment.residualArgb, mass)
            : result.addPigment(pigment.pigmentId, mass);
        if (!added) return std::nullopt; // never silently drop an unrepresentable component
    }
    return result.valid() ? std::optional<PaintBatch>(std::move(result)) : std::nullopt;
}

std::optional<PaintBatch> MaterialTileStore::batchAt(TilePoint point) const try
{
    const auto cell = cellAt(point);
    if (!cell) return std::nullopt;
    if (cell->empty()) return PaintBatch{};
    if (!validateCell(*cell)) return std::nullopt;
    return expand(*cell);
} catch (const std::bad_alloc&) { return std::nullopt; }
  catch (const std::length_error&) { return std::nullopt; }

std::size_t MaterialTileStore::compositionHash(const MaterialComposition& value) const noexcept
{
    std::size_t result = std::hash<std::string>{}(value.mediumId);
    for (const auto& pigment : value.pigments) {
        result = combineHash(result, std::hash<std::string>{}(pigment.pigmentId));
        result = combineHash(result, std::hash<std::uint32_t>{}(pigment.residualArgb));
        result = combineHash(result, std::hash<double>{}(pigment.mass));
    }
    return result;
}

bool MaterialTileStore::sameComposition(const MaterialComposition& a,
                                        const MaterialComposition& b) const noexcept
{
    if (a.mediumId != b.mediumId || a.pigments.size() != b.pigments.size()) return false;
    for (std::size_t i = 0; i < a.pigments.size(); ++i) {
        const auto& x = a.pigments[i];
        const auto& y = b.pigments[i];
        if (x.pigmentId != y.pigmentId || x.residualArgb != y.residualArgb || x.mass != y.mass)
            return false;
    }
    return true;
}

std::optional<std::size_t> MaterialTileStore::intern(const PaintBatch& paint, bool* inserted)
{
    if (inserted) *inserted = false;
    if (!paint.valid()) return std::nullopt;
    MaterialComposition canonical;
    canonical.mediumId = paint.mediumId;
    canonical.pigments = paint.pigments;
    std::sort(canonical.pigments.begin(), canonical.pigments.end(), [](const auto& a, const auto& b) {
        if (a.pigmentId != b.pigmentId) return a.pigmentId < b.pigmentId;
        return a.residualArgb < b.residualArgb;
    });
    std::vector<PigmentComponent> merged;
    merged.reserve(canonical.pigments.size());
    for (const auto& pigment : canonical.pigments) {
        if (!merged.empty() && merged.back().pigmentId == pigment.pigmentId
            && merged.back().residualArgb == pigment.residualArgb) merged.back().mass += pigment.mass;
        else merged.push_back(pigment);
    }
    double pigmentMass = 0.0;
    for (const auto& pigment : merged) pigmentMass += pigment.mass;
    if (!std::isfinite(pigmentMass)) throw std::invalid_argument("pigment mass overflow");
    if (pigmentMass > 0.0) {
        std::size_t maxIdx = 0;
        double maxMass = 0.0;
        double sum = 0.0;
        for (std::size_t i = 0; i < merged.size(); ++i) {
            auto& pigment = merged[i];
            pigment.mass /= pigmentMass;
            if (!std::isfinite(pigment.mass) || pigment.mass <= 0.0)
                throw std::invalid_argument("pigment proportion is not representable");
            if (pigment.mass >= 1e-10) {
                const double quantized = std::round(pigment.mass * 1e12) / 1e12;
                if (quantized > 0.0) pigment.mass = quantized;
            }
            sum += pigment.mass;
            if (pigment.mass > maxMass) { maxMass = pigment.mass; maxIdx = i; }
        }
        if (std::abs(sum - 1.0) > 1e-14 && merged[maxIdx].mass + (1.0 - sum) > 0.0)
            merged[maxIdx].mass += (1.0 - sum);
    }
    canonical.pigments = std::move(merged);
    const std::size_t hash = compositionHash(canonical);
    const auto [first, last] = m_compositionIndex.equal_range(hash);
    for (auto candidate = first; candidate != last; ++candidate)
        if (!m_compositions[candidate->second].mediumId.empty()
            && sameComposition(m_compositions[candidate->second], canonical)) return candidate->second;

    const std::size_t charge = compositionCharge(canonical);
    if (m_budget && !m_budget->reserve(DocumentMemoryBudget::Category::Artwork, charge))
        return std::nullopt;
    try {
        std::size_t slotId = 0;
        if (!m_freeCompositionIds.empty()) {
            slotId = m_freeCompositionIds.back();
            m_compositionIndex.emplace(hash, slotId);
            m_freeCompositionIds.pop_back();
            m_compositions[slotId] = std::move(canonical);
            m_compositionHashes[slotId] = hash;
            m_compositionLiveRefs[slotId] = 0;
            m_compositionPinRefs[slotId] = 0;
        } else {
            m_compositions.push_back(std::move(canonical));
            try {
                m_compositionHashes.push_back(hash);
                try {
                    m_compositionLiveRefs.push_back(0);
                    m_compositionPinRefs.push_back(0);
                    slotId = m_compositions.size() - 1;
                    m_compositionIndex.emplace(hash, slotId);
                } catch (...) {
                    if (m_compositionLiveRefs.size() == m_compositions.size()) m_compositionLiveRefs.pop_back();
                    if (m_compositionPinRefs.size() == m_compositions.size()) m_compositionPinRefs.pop_back();
                    m_compositionHashes.pop_back();
                    throw;
                }
            } catch (...) {
                m_compositions.pop_back();
                throw;
            }
        }
        m_compositionBytes += charge;
        if (inserted) *inserted = true;
        return slotId;
    } catch (...) {
        if (m_budget) m_budget->release(DocumentMemoryBudget::Category::Artwork, charge);
        throw;
    }
}

bool MaterialTileStore::validateCell(const MaterialCell& cell) const noexcept
{
    return cell.compositionId > 0 && cell.compositionId < m_compositions.size()
        && validProfile(m_compositions[cell.compositionId])
        && std::isfinite(cell.pigmentMass) && cell.pigmentMass >= 0.0F
        && std::isfinite(cell.water) && cell.water >= 0.0F
        && std::isfinite(cell.binder) && cell.binder >= 0.0F
        && std::isfinite(cell.filler) && cell.filler >= 0.0F
        && std::isfinite(cell.wetness) && cell.wetness >= 0.0F && cell.wetness <= 1.0F
        && std::isfinite(cell.consistency) && cell.consistency >= 0.0F && cell.consistency <= 1.0F
        && std::isfinite(cell.drying) && cell.drying >= 0.0F && cell.drying <= 1.0F
        && std::isfinite(cell.settling) && cell.settling >= 0.0F && cell.settling <= 1.0F
        && (!m_compositions[cell.compositionId].pigments.empty() || cell.pigmentMass == 0.0F)
        && (cell.pigmentMass > 0.0F || cell.water > 0.0F || cell.binder > 0.0F || cell.filler > 0.0F);
}

void MaterialTileStore::notify(TilePoint tile) noexcept
{
    if (m_batchDepth) {
        try { m_batchTiles.insert(tile); return; }
        catch (const std::bad_alloc&) {}
        catch (const std::length_error&) {}
        // Allocation failure in the notification coalescer must not suppress
        // redraw; fall back to an immediate tile-local notification.
    }
    if (m_callback) {
        const bool wasNotifying = m_notifying;
        m_notifying = true;
        m_callback(m_callbackContext, tile);
        m_notifying = wasNotifying;
    }
}

MaterialTileStore::Result MaterialTileStore::writeCell(TilePoint point, const MaterialCell& cell)
{
    if (!isValid()) return Result::OutOfBounds;
    const auto address = m_geometry.address(point);
    if (!address) return Result::OutOfBounds;
    if (!cell.empty() && !validateCell(cell)) return Result::InvalidData;

    auto tileIt = m_tiles.find(address->tile);
    const std::size_t index = static_cast<std::size_t>(address->local.y)
        * static_cast<std::size_t>(m_geometry.tileSize())
        + static_cast<std::size_t>(address->local.x);
    auto findEntry = [&]() {
        return std::lower_bound(tileIt->second.entries.begin(), tileIt->second.entries.end(), index,
            [](const MaterialEntry& item, std::size_t value) { return item.localIndex < value; });
    };
    const bool hadTile = tileIt != m_tiles.end();
    const auto existing = hadTile ? findEntry() : std::vector<MaterialEntry>::const_iterator{};
    const bool exists = hadTile && existing != tileIt->second.entries.end() && existing->localIndex == index;
    const MaterialCell old = exists ? existing->cell : MaterialCell{};
    if (old == cell) return Result::Unchanged;

    const TilePoint tilePoint = address->tile;
    const bool wasActive = mobileCell(old);
    const bool isActive = mobileCell(cell);
    const auto beforeEntries = hadTile
        ? std::span<const MaterialEntry>(tileIt->second.entries)
        : std::span<const MaterialEntry>{};
    if (m_beforeChange && !m_beforeChange(m_beforeContext, tilePoint, beforeEntries))
        return Result::AllocationFailed;
    const auto bounds = m_geometry.tileBounds(tilePoint);
    if (!bounds) return Result::OutOfBounds;
    const bool activateTile = !wasActive && isActive
        && (!hadTile || tileIt->second.activeCells == 0);
    std::vector<TilePoint> stagedActive;
    std::size_t activeStagingBytes = 0;
    bool activeStageCommitted = false;
    struct ActiveStageRelease {
        std::shared_ptr<DocumentMemoryBudget> budget;
        std::size_t bytes = 0;
        bool* committed = nullptr;
        ~ActiveStageRelease() {
            if (budget && bytes && committed && !*committed)
                budget->release(DocumentMemoryBudget::Category::RestoreStaging, bytes);
        }
    } activeStage{m_budget, 0, &activeStageCommitted};
    if (activateTile && m_activeTiles.size() == m_activeTiles.capacity()) {
        const std::size_t currentCapacity = m_activeTiles.capacity();
        const std::size_t nextCapacity = currentCapacity == 0 ? std::size_t{8}
            : currentCapacity > std::numeric_limits<std::size_t>::max() / 2
                ? m_activeTiles.max_size() : currentCapacity * 2;
        if (nextCapacity <= currentCapacity || nextCapacity > m_activeTiles.max_size()
            || nextCapacity > std::numeric_limits<std::size_t>::max() / sizeof(TilePoint))
            return Result::AllocationFailed;
        activeStagingBytes = nextCapacity * sizeof(TilePoint);
        if (m_budget && !m_budget->reserve(DocumentMemoryBudget::Category::RestoreStaging, activeStagingBytes))
            return Result::DocumentMemoryLimit;
        activeStage.bytes = activeStagingBytes;
        try {
            stagedActive.reserve(nextCapacity);
            stagedActive.insert(stagedActive.end(), m_activeTiles.begin(), m_activeTiles.end());
            stagedActive.insert(std::lower_bound(stagedActive.begin(), stagedActive.end(), tilePoint, TilePointLess{}), tilePoint);
            const std::size_t actualBytes = stagedActive.capacity() * sizeof(TilePoint);
            if (actualBytes > activeStagingBytes) {
                const std::size_t extra = actualBytes - activeStagingBytes;
                if (m_budget && !m_budget->reserve(DocumentMemoryBudget::Category::RestoreStaging, extra))
                    return Result::DocumentMemoryLimit;
                activeStagingBytes += extra;
                activeStage.bytes = activeStagingBytes;
            }
        } catch (const std::bad_alloc&) { return Result::AllocationFailed; }
          catch (const std::length_error&) { return Result::AllocationFailed; }
    }
    const auto marked = m_dirty.mark(*bounds);
    if (marked != DirtyTileTracker::Result::Marked)
        return marked == DirtyTileTracker::Result::AllocationFailed ? Result::AllocationFailed : Result::TrackingLimit;

    bool createdTile = false;
    try {
        if (!cell.empty() && tileIt == m_tiles.end()) {
            auto inserted = m_tiles.try_emplace(tilePoint);
            if (!inserted.second) return Result::AllocationFailed;
            tileIt = inserted.first;
            createdTile = true;
        }
        if (!cell.empty() && !exists && tileIt->second.entries.size() == tileIt->second.entries.capacity()) {
            auto& tile = tileIt->second;
            const std::size_t oldBytes = tile.chargedBytes;
            const std::size_t currentCapacity = tile.entries.capacity();
            const std::size_t maximum = tile.entries.max_size();
            if (currentCapacity == maximum || maximum > std::numeric_limits<std::size_t>::max() / sizeof(MaterialEntry)) {
                if (createdTile) m_tiles.erase(tileIt);
                return Result::AllocationFailed;
            }
            const std::size_t nextCapacity = currentCapacity == 0 ? std::size_t{8}
                : currentCapacity > maximum / 2 ? maximum : currentCapacity * 2;
            if (nextCapacity > maximum / sizeof(MaterialEntry)) {
                if (createdTile) m_tiles.erase(tileIt);
                return Result::AllocationFailed;
            }
            const std::size_t requestedBytes = nextCapacity * sizeof(MaterialEntry);
            if (m_budget && !m_budget->reserve(DocumentMemoryBudget::Category::RestoreStaging, requestedBytes)) {
                if (createdTile) m_tiles.erase(tileIt);
                return Result::DocumentMemoryLimit;
            }
            struct Staging {
                std::shared_ptr<DocumentMemoryBudget> budget;
                std::size_t bytes;
                bool committed = false;
                ~Staging() {
                    if (budget && !committed)
                        budget->release(DocumentMemoryBudget::Category::RestoreStaging, bytes);
                }
            } staging{m_budget, requestedBytes};
            std::vector<MaterialEntry> replacement;
            replacement.reserve(nextCapacity);
            replacement.insert(replacement.end(), tile.entries.begin(), tile.entries.end());
            auto insertion = std::lower_bound(replacement.begin(), replacement.end(), index,
                [](const MaterialEntry& item, std::size_t value) { return item.localIndex < value; });
            replacement.insert(insertion, MaterialEntry{index, cell});
            const std::size_t actualBytes = replacement.capacity() * sizeof(MaterialEntry);
            if (actualBytes > requestedBytes) {
                const std::size_t extra = actualBytes - requestedBytes;
                if (m_budget && !m_budget->reserve(DocumentMemoryBudget::Category::RestoreStaging, extra)) {
                    if (createdTile) m_tiles.erase(tileIt);
                    return Result::DocumentMemoryLimit;
                }
                staging.bytes += extra;
            }
            tile.entries.swap(replacement);
            tile.chargedBytes = actualBytes;
            m_allocatedBytes += actualBytes;
            if (m_budget) {
                if (oldBytes) m_budget->release(DocumentMemoryBudget::Category::Artwork, oldBytes);
                m_budget->transferToArtwork(staging.bytes);
            }
            m_allocatedBytes -= oldBytes;
            staging.committed = true;
        } else if (cell.empty()) {
            if (exists) tileIt->second.entries.erase(findEntry());
            if (tileIt != m_tiles.end() && tileIt->second.entries.empty()) {
                const auto bytes = tileIt->second.chargedBytes;
                m_tiles.erase(tileIt);
                m_allocatedBytes -= bytes;
                if (m_budget && bytes) m_budget->release(DocumentMemoryBudget::Category::Artwork, bytes);
            }
        } else if (exists) {
            findEntry()->cell = cell;
        } else {
            const auto position = findEntry();
            tileIt->second.entries.insert(position, MaterialEntry{index, cell});
        }
    } catch (const std::bad_alloc&) {
        if (createdTile) {
            const auto it = m_tiles.find(tilePoint);
            if (it != m_tiles.end() && it->second.entries.empty()) m_tiles.erase(it);
        }
        return Result::AllocationFailed;
    } catch (const std::length_error&) {
        if (createdTile) {
            const auto it = m_tiles.find(tilePoint);
            if (it != m_tiles.end() && it->second.entries.empty()) m_tiles.erase(it);
        }
        return Result::AllocationFailed;
    }
    auto updatedTile = m_tiles.find(tilePoint);
    if (activateTile && !stagedActive.empty()) {
        const std::size_t oldActiveBytes = m_activeTileBytes;
        m_activeTiles.swap(stagedActive);
        m_activeTileBytes = m_activeTiles.capacity() * sizeof(TilePoint);
        if (m_budget) {
            if (oldActiveBytes) m_budget->release(DocumentMemoryBudget::Category::Artwork, oldActiveBytes);
            m_budget->transferToArtwork(activeStage.bytes);
        }
        activeStageCommitted = true;
    }
    if (updatedTile != m_tiles.end()) {
        if (wasActive && !isActive && updatedTile->second.activeCells > 0)
            --updatedTile->second.activeCells;
        else if (!wasActive && isActive)
            ++updatedTile->second.activeCells;
    }
    auto activePosition = std::lower_bound(m_activeTiles.begin(), m_activeTiles.end(),
                                           tilePoint, TilePointLess{});
    const bool activeListed = activePosition != m_activeTiles.end() && *activePosition == tilePoint;
    const bool activeAfter = updatedTile != m_tiles.end() && updatedTile->second.activeCells != 0;
    if (activeAfter && !activeListed)
        m_activeTiles.insert(activePosition, tilePoint); // capacity reserved before changing the cell
    else if (!activeAfter && activeListed)
        m_activeTiles.erase(activePosition);
    if (old.compositionId > 0 && old.compositionId < m_compositionLiveRefs.size()
        && m_compositionLiveRefs[old.compositionId] > 0)
        --m_compositionLiveRefs[old.compositionId];
    if (cell.compositionId > 0 && cell.compositionId < m_compositionLiveRefs.size())
        ++m_compositionLiveRefs[cell.compositionId];
    notify(tilePoint);
    return Result::Changed;
}

MaterialTileStore::Result MaterialTileStore::write(TilePoint point, const PaintBatch& paint, bool combine) try
{
    if (!paint.valid()) return Result::InvalidData;
    if (!isValid() || !m_geometry.address(point)) return Result::OutOfBounds;
    PaintBatch value = paint;
    if (combine) {
        const auto prior = batchAt(point);
        if (!prior) return Result::InvalidData;
        if (prior->totalMass() > 0.0) value = mix(*prior, paint);
    }
    const double pigmentMass = value.pigmentMass();
    if (!finiteFloat(pigmentMass) || !finiteFloat(value.water) || !finiteFloat(value.binder)
        || !finiteFloat(value.filler) || !std::isfinite(value.wetness) || !std::isfinite(value.consistency))
        return Result::InvalidData;
    if (value.totalMass() <= 0.0) return erase(point);
    bool inserted = false;
    std::optional<std::size_t> id;
    try { id = intern(value, &inserted); }
    catch (const std::bad_alloc&) { return Result::AllocationFailed; }
    catch (const std::length_error&) { return Result::AllocationFailed; }
    catch (const std::invalid_argument&) { return Result::InvalidData; }
    if (!id) return Result::DocumentMemoryLimit;
    const MaterialCell cell{*id, static_cast<float>(pigmentMass), static_cast<float>(value.water),
        static_cast<float>(value.binder), static_cast<float>(value.filler),
        static_cast<float>(value.wetness), static_cast<float>(value.consistency),
        static_cast<float>(value.drying), static_cast<float>(value.settling)};
    const auto result = writeCell(point, cell);
    if (result != Result::Changed && inserted) eraseUnreferencedComposition(*id);
    return result;
} catch (const std::bad_alloc&) { return Result::AllocationFailed; }
  catch (const std::length_error&) { return Result::AllocationFailed; }

MaterialTileStore::Result MaterialTileStore::deposit(TilePoint point, const PaintBatch& paint)
{
    return write(point, paint, true);
}

MaterialTileStore::Result MaterialTileStore::setBatch(TilePoint point, const PaintBatch& paint)
{
    return write(point, paint, false);
}

MaterialTileStore::Result MaterialTileStore::updateCellState(TilePoint point, double water,
                                                             double binder, double filler,
                                                             double drying, double settling)
{
    if (!finiteFloat(water) || !finiteFloat(binder) || !finiteFloat(filler)
        || !std::isfinite(drying) || drying < 0.0 || drying > 1.0
        || !std::isfinite(settling) || settling < 0.0 || settling > 1.0)
        return Result::InvalidData;
    if (!isValid() || !m_geometry.address(point)) return Result::OutOfBounds;
    const auto existing = cellAt(point);
    if (!existing) return Result::OutOfBounds;
    if (existing->empty()) return Result::Unchanged;
    MaterialCell next = *existing;
    next.water = static_cast<float>(water);
    next.binder = static_cast<float>(binder);
    next.filler = static_cast<float>(filler);
    next.drying = static_cast<float>(drying);
    next.settling = static_cast<float>(settling);
    const double total = static_cast<double>(next.pigmentMass) + water + binder + filler;
    if (total <= 0.0) return erase(point);
    next.wetness = static_cast<float>(std::clamp(water / total, 0.0, 1.0));
    next.consistency = static_cast<float>(std::clamp((static_cast<double>(next.pigmentMass) + binder + filler) / total, 0.0, 1.0));
    return writeCell(point, next);
}

MaterialTileStore::Result MaterialTileStore::scaleCell(TilePoint point, double factor)
{
    if (!std::isfinite(factor) || factor < 0.0 || factor > 1.0) return Result::InvalidData;
    if (!m_geometry.address(point)) return Result::OutOfBounds;
    const auto existing = cellAt(point);
    if (!existing) return Result::Unchanged;
    if (factor == 0.0) return writeCell(point, MaterialCell{});
    MaterialCell scaled = *existing;
    scaled.pigmentMass = static_cast<float>(static_cast<double>(scaled.pigmentMass) * factor);
    scaled.water = static_cast<float>(static_cast<double>(scaled.water) * factor);
    scaled.binder = static_cast<float>(static_cast<double>(scaled.binder) * factor);
    scaled.filler = static_cast<float>(static_cast<double>(scaled.filler) * factor);
    if (scaled.pigmentMass == 0.0F && scaled.water == 0.0F
        && scaled.binder == 0.0F && scaled.filler == 0.0F)
        return writeCell(point, MaterialCell{});
    return writeCell(point, scaled);
}

MaterialTileStore::Result MaterialTileStore::erase(TilePoint point)
{
    return writeCell(point, MaterialCell{});
}

MaterialTileStore::Result MaterialTileStore::take(TilePoint point, double amount, PaintBatch* removed)
{
    if (!removed || !std::isfinite(amount) || amount < 0.0) return Result::InvalidData;
    *removed = PaintBatch{};
    const auto current = batchAt(point);
    if (!current) return Result::InvalidData;
    const double total = current->totalMass();
    if (total <= 0.0 || amount <= 0.0) return Result::Unchanged;
    const double takenMass = std::min(amount, total);
    const double fraction = takenMass / total;
    PaintBatch remainder = *current;
    PaintBatch taken = *current;
    taken.scale(fraction);
    remainder.scale(1.0 - fraction);
    const Result result = remainder.totalMass() <= 1.0e-12
        ? erase(point) : setBatch(point, remainder);
    if (result == Result::Changed || result == Result::Unchanged) *removed = std::move(taken);
    return result;
}

void MaterialTileStore::eraseUnreferencedComposition(std::size_t id) noexcept
{
    if (id == 0 || id >= m_compositions.size() || m_compositions[id].mediumId.empty()) return;
    if (m_compositionLiveRefs[id] != 0 || m_compositionPinRefs[id] != 0) return;
    const auto charge = compositionCharge(m_compositions[id]);
    const auto range = m_compositionIndex.equal_range(m_compositionHashes[id]);
    for (auto it = range.first; it != range.second; ++it)
        if (it->second == id) { m_compositionIndex.erase(it); break; }
    m_compositionBytes -= charge;
    if (m_budget) m_budget->release(DocumentMemoryBudget::Category::Artwork, charge);
    if (id + 1 == m_compositions.size()) {
        m_compositions.pop_back();
        m_compositionHashes.pop_back();
        m_compositionLiveRefs.pop_back();
        m_compositionPinRefs.pop_back();
    } else {
        m_compositions[id] = MaterialComposition{};
        m_compositionHashes[id] = 0;
        try { m_freeCompositionIds.push_back(id); }
        catch (const std::bad_alloc&) {}
        catch (const std::length_error&) {}
    }
}

MaterialTileSnapshot MaterialTileStore::snapshot(TilePoint tile) const
{
    const auto it = m_tiles.find(tile);
    return {tile, it == m_tiles.end() ? std::vector<MaterialEntry>{} : it->second.entries};
}

MaterialTileStore::Result MaterialTileStore::restoreTiles(std::span<const MaterialTileSnapshot> snapshots,
                                                          bool notifyChanges)
{
    if (!isValid() || m_beforeChange || m_batchDepth) return Result::InvalidData;
    try {
        std::unordered_map<TilePoint, Tile, Hash> staged;
        staged.reserve(snapshots.size());
        auto dirty = m_dirty;
        std::size_t stagedBytes = 0;
        for (const auto& snapshot : snapshots) {
            const auto bounds = m_geometry.tileBounds(snapshot.tile);
            if (!bounds || staged.find(snapshot.tile) != staged.end()) return Result::InvalidData;
            std::size_t previous = 0;
            bool first = true;
            for (const auto& entry : snapshot.entries) {
                const auto tilePixels = static_cast<std::size_t>(m_geometry.tileSize())
                    * static_cast<std::size_t>(m_geometry.tileSize());
                if (entry.localIndex >= tilePixels || (!first && entry.localIndex <= previous)
                    || !validateCell(entry.cell)) return Result::InvalidData;
                const auto localX = entry.localIndex % static_cast<std::size_t>(m_geometry.tileSize());
                const auto localY = entry.localIndex / static_cast<std::size_t>(m_geometry.tileSize());
                if (bounds->left + static_cast<std::int64_t>(localX) >= bounds->right
                    || bounds->top + static_cast<std::int64_t>(localY) >= bounds->bottom) return Result::InvalidData;
                previous = entry.localIndex;
                first = false;
            }
            if (snapshot.entries.size() > std::numeric_limits<std::size_t>::max() / sizeof(MaterialEntry) - 1)
                return Result::AllocationFailed;
            Tile tile;
            tile.entries.reserve(snapshot.entries.size());
            tile.entries = snapshot.entries;
            tile.chargedBytes = tile.entries.capacity() * sizeof(MaterialEntry);
            tile.activeCells = static_cast<std::size_t>(std::count_if(tile.entries.begin(), tile.entries.end(),
                [](const MaterialEntry& entry) { return mobileCell(entry.cell); }));
            if (tile.chargedBytes > std::numeric_limits<std::size_t>::max() - stagedBytes)
                return Result::AllocationFailed;
            stagedBytes += tile.chargedBytes;
            const auto marked = dirty.mark(*bounds);
            if (marked != DirtyTileTracker::Result::Marked)
                return marked == DirtyTileTracker::Result::AllocationFailed ? Result::AllocationFailed : Result::TrackingLimit;
            staged.emplace(snapshot.tile, std::move(tile));
        }
        if (m_budget && !m_budget->reserve(DocumentMemoryBudget::Category::RestoreStaging, stagedBytes))
            return Result::DocumentMemoryLimit;
        struct StagingRelease {
            std::shared_ptr<DocumentMemoryBudget> budget;
            std::size_t bytes;
            bool committed = false;
            ~StagingRelease() {
                if (!budget || committed) return;
                budget->release(DocumentMemoryBudget::Category::RestoreStaging, bytes);
            }
        } staging{m_budget, stagedBytes};
        if (staged.size() > std::numeric_limits<std::size_t>::max() - m_activeTiles.size())
            return Result::AllocationFailed;
        std::vector<TilePoint> stagedActiveTiles;
        stagedActiveTiles.reserve(m_activeTiles.size() + staged.size());
        for (const TilePoint activeTile : m_activeTiles) {
            const bool replaced = std::any_of(snapshots.begin(), snapshots.end(),
                [activeTile](const MaterialTileSnapshot& snapshot) { return snapshot.tile == activeTile; });
            if (!replaced) stagedActiveTiles.push_back(activeTile);
        }
        for (const auto& snapshot : snapshots) {
            const auto replacement = staged.find(snapshot.tile);
            if (replacement != staged.end() && replacement->second.activeCells != 0)
                stagedActiveTiles.push_back(snapshot.tile);
        }
        std::sort(stagedActiveTiles.begin(), stagedActiveTiles.end(), TilePointLess{});
        std::size_t activeStagingBytes = stagedActiveTiles.capacity() * sizeof(TilePoint);
        if (m_budget && activeStagingBytes
            && !m_budget->reserve(DocumentMemoryBudget::Category::RestoreStaging, activeStagingBytes))
            return Result::DocumentMemoryLimit;
        bool activeCommitted = false;
        struct ActiveRestoreRelease {
            std::shared_ptr<DocumentMemoryBudget> budget;
            std::size_t bytes;
            bool* committed;
            ~ActiveRestoreRelease() {
                if (budget && bytes && !*committed)
                    budget->release(DocumentMemoryBudget::Category::RestoreStaging, bytes);
            }
        } activeRelease{m_budget, activeStagingBytes, &activeCommitted};
        m_tiles.reserve(m_tiles.size() + staged.size());
        m_dirty = std::move(dirty);
        for (const auto& snapshot : snapshots) {
            auto incoming = staged.extract(snapshot.tile);
            auto current = m_tiles.find(snapshot.tile);
            if (current != m_tiles.end()) {
                for (const auto& oldEntry : current->second.entries) {
                    const std::size_t oldId = oldEntry.cell.compositionId;
                    if (oldId > 0 && oldId < m_compositionLiveRefs.size()
                        && m_compositionLiveRefs[oldId] > 0)
                        --m_compositionLiveRefs[oldId];
                }
                m_allocatedBytes -= current->second.chargedBytes;
                if (m_budget) m_budget->release(DocumentMemoryBudget::Category::Artwork, current->second.chargedBytes);
                if (incoming.empty() || incoming.mapped().entries.empty()) m_tiles.erase(current);
                else {
                    for (const auto& newEntry : incoming.mapped().entries) {
                        const std::size_t newId = newEntry.cell.compositionId;
                        if (newId > 0 && newId < m_compositionLiveRefs.size())
                            ++m_compositionLiveRefs[newId];
                    }
                    m_allocatedBytes += incoming.mapped().chargedBytes;
                    current->second = std::move(incoming.mapped());
                }
            } else if (!incoming.empty() && !incoming.mapped().entries.empty()) {
                for (const auto& newEntry : incoming.mapped().entries) {
                    const std::size_t newId = newEntry.cell.compositionId;
                    if (newId > 0 && newId < m_compositionLiveRefs.size())
                        ++m_compositionLiveRefs[newId];
                }
                m_allocatedBytes += incoming.mapped().chargedBytes;
                m_tiles.insert(std::move(incoming));
            }
        }
        const std::size_t oldActiveBytes = m_activeTileBytes;
        m_activeTiles.swap(stagedActiveTiles);
        m_activeTileBytes = m_activeTiles.capacity() * sizeof(TilePoint);
        if (m_budget) {
            if (oldActiveBytes) m_budget->release(DocumentMemoryBudget::Category::Artwork, oldActiveBytes);
            if (activeStagingBytes) m_budget->transferToArtwork(activeStagingBytes);
            if (stagedBytes) m_budget->transferToArtwork(stagedBytes);
        }
        activeCommitted = true;
        staging.committed = true;
        if (notifyChanges) for (const auto& snapshot : snapshots) notify(snapshot.tile);
        return Result::Changed;
    } catch (const std::bad_alloc&) { return Result::AllocationFailed; }
      catch (const std::length_error&) { return Result::AllocationFailed; }
}

bool MaterialTileStore::importCompositions(const std::vector<MaterialComposition>& imported)
{
    if (!m_tiles.empty() || m_compositions.size() != 1 || imported.empty() || !emptyComposition(imported.front()))
        return false;
    std::size_t charge = 0;
    for (std::size_t i = 1; i < imported.size(); ++i) {
        if (emptyComposition(imported[i])) continue;
        if (!validProfile(imported[i])) return false;
        const std::size_t item = compositionCharge(imported[i]);
        if (item > std::numeric_limits<std::size_t>::max() - charge) return false;
        charge += item;
    }
    if (m_budget && !m_budget->reserve(DocumentMemoryBudget::Category::Artwork, charge)) return false;
    try {
        std::vector<MaterialComposition> staged = imported;
        std::vector<std::size_t> hashes;
        hashes.reserve(staged.size());
        hashes.push_back(0);
        std::vector<std::size_t> liveRefs(staged.size(), 0);
        std::vector<std::size_t> pinRefs(staged.size(), 0);
        std::vector<std::size_t> freeIds;
        std::unordered_multimap<std::size_t, std::size_t> index;
        index.reserve(staged.size() - 1);
        for (std::size_t i = 1; i < staged.size(); ++i) {
            if (emptyComposition(staged[i])) {
                hashes.push_back(0);
                freeIds.push_back(i);
            } else {
                const auto hash = compositionHash(staged[i]);
                hashes.push_back(hash);
                index.emplace(hash, i);
            }
        }
        m_compositions.swap(staged);
        m_compositionHashes.swap(hashes);
        m_compositionLiveRefs.swap(liveRefs);
        m_compositionPinRefs.swap(pinRefs);
        m_freeCompositionIds.swap(freeIds);
        m_compositionIndex.swap(index);
        m_compositionBytes = charge;
        return true;
    } catch (const std::bad_alloc&) {
        if (m_budget) m_budget->release(DocumentMemoryBudget::Category::Artwork, charge);
        return false;
    } catch (const std::length_error&) {
        if (m_budget) m_budget->release(DocumentMemoryBudget::Category::Artwork, charge);
        return false;
    }
}

void MaterialTileStore::endBatch() noexcept
{
    if (!m_batchDepth || --m_batchDepth || m_batchTiles.empty()) return;
    std::unordered_set<TilePoint, Hash> changed;
    changed.swap(m_batchTiles);
    if (!m_callback) return;
    const bool wasNotifying = m_notifying;
    m_notifying = true;
    for (const TilePoint tile : changed) m_callback(m_callbackContext, tile);
    m_notifying = wasNotifying;
}

bool MaterialRenderer::renderTile(const MaterialTileStore& material, TilePoint tile,
                                  std::span<const std::uint32_t> digitalBase,
                                  const MediumProfile& defaultMedium, const SurfaceProfile& surface,
                                  std::vector<std::uint32_t>* output) const
{
    if (!output || !material.geometry().tileBounds(tile)) return false;
    const auto tilePixels = static_cast<std::size_t>(material.geometry().tileSize())
        * static_cast<std::size_t>(material.geometry().tileSize());
    if (!digitalBase.empty() && digitalBase.size() != tilePixels) return false;
    try {
        output->assign(tilePixels, 0U);
        if (!digitalBase.empty()) std::copy(digitalBase.begin(), digitalBase.end(), output->begin());
        const auto entries = material.tileEntries(tile);
        const auto tileSize = static_cast<std::size_t>(material.geometry().tileSize());
        for (const auto& entry : entries) {
            const MaterialCell& cell = entry.cell;
            const std::int64_t docX = tile.x * material.geometry().tileSize()
                + static_cast<std::int64_t>(entry.localIndex % tileSize);
            const std::int64_t docY = tile.y * material.geometry().tileSize()
                + static_cast<std::int64_t>(entry.localIndex / tileSize);
            const auto batch = material.batchAt({docX, docY});
            if (!batch || !batch->valid()) return false;
            const MediumProfile* profile = &defaultMedium;
            if (batch->mediumId == gouacheProfile().id) profile = &gouacheProfile();
            else if (batch->mediumId == watercolourProfile().id) profile = &watercolourProfile();
            else if (batch->mediumId == inkProfile().id) profile = &inkProfile();
            else if (batch->mediumId != defaultMedium.id) return false;
            // MaterialDab deposits a local fraction of a batch per pixel; scale
            // by the shared reference unit so optical depth matches direct
            // batch evaluation without letting water inflate opacity.
            constexpr double ReferenceBatchPixels = 16384.0;
            const double toothHeight = surfaceToothHeightAt(docX, docY, surface);
            const double valley = 1.0 - toothHeight;
            const double opticalGran = m_engine.opticalResult(*batch, *profile, surface, ReferenceBatchPixels).granulation;
            const double granulationBias = 1.0 + opticalGran * (0.25 + 0.75 * double(cell.settling)) * (valley - 0.5) * 0.45;
            const double toothContact = (0.86 + 0.14 * std::clamp(0.45 * toothHeight + 0.55 * double(cell.consistency), 0.0, 1.0))
                * std::clamp(granulationBias, 0.72, 1.28);
            (*output)[entry.localIndex] = m_engine.composite((*output)[entry.localIndex], *batch,
                *profile, surface, 1.0, ReferenceBatchPixels, toothContact);
        }
        return true;
    } catch (const std::bad_alloc&) { return false; }
      catch (const std::length_error&) { return false; }
}

} // namespace komapaint::material
