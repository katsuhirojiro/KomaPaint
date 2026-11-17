#include "UndoStack.h"
#include "../layers/Layer.h"
#include <algorithm>
#include <new>
#include <limits>
#include <stdexcept>

namespace komapaint {
UndoStack::StrokeCommand::~StrokeCommand()
{
    if (layer && materialStroke && (!materialBefore.empty() || !materialAfter.empty())) {
        auto& store = layer->materialTiles();
        store.unpinSnapshots(materialBefore);
        store.unpinSnapshots(materialAfter);
        store.reclaimUnreferenced();
    }
    if (budget) budget->release(DocumentMemoryBudget::Category::History, historyBytes);
}

UndoStack::UndoStack(QObject* parent) : QObject(parent)
{
    // Qt allocates a QObject's weak-reference block the first time a QPointer
    // is made for it. Doing that once here means the "is this stack still
    // alive?" guards in cancel/undo/redo never allocate, so cancelling a
    // stroke keeps working when memory has just run out.
    m_self = this;
}
UndoStack::~UndoStack() {
    if (m_current && m_current->layer) {
        detachCapture();
        if (m_current->materialStroke)
            m_current->layer->materialTiles().restoreTiles(m_current->materialBefore, false);
        else m_current->layer->tiles().rollbackRecordedStroke(m_current->before);
    }
}
QString UndoStack::undoText() const
{
    return canUndo() ? (m_commands[m_cursor - 1]->materialStroke ? QStringLiteral("Material stroke")
                                                                : QStringLiteral("Brush stroke")) : QString();
}
QString UndoStack::redoText() const
{
    return canRedo() ? (m_commands[m_cursor]->materialStroke ? QStringLiteral("Material stroke")
                                                             : QStringLiteral("Brush stroke")) : QString();
}
bool UndoStack::beginStroke(Layer* layer)
{
    if (!layer || m_current || m_replaying) return false;
    try {
        if (m_commands.size() == m_commands.capacity()) {
            const auto max = m_commands.max_size();
            if (m_commands.size() == max) throw std::length_error("history full");
            const auto growth = std::max<std::size_t>(m_commands.size(), 1);
            const auto capacity = m_commands.size() + std::min(growth, max - m_commands.size());
            if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(m_commands[0]))
                throw std::length_error("history index too large");
            const auto bytes = capacity * sizeof(m_commands[0]);
            auto budget = layer->tiles().documentBudget();
            if (budget && !budget->reserve(DocumentMemoryBudget::Category::History, bytes)) {
                fail(QStringLiteral("Not enough available memory to grow undo history. Free memory and try again."));
                return false;
            }
            struct Reservation {
                std::shared_ptr<DocumentMemoryBudget> budget;
                std::size_t bytes;
                ~Reservation() { if (budget) budget->release(DocumentMemoryBudget::Category::History, bytes); }
            } reservation{std::move(budget), bytes};
            m_commands.reserve(capacity);
        } // commit must not allocate
        m_current = std::make_unique<StrokeCommand>();
        m_current->layer = layer;
        m_current->budget = layer->tiles().documentBudget();
        if (!layer->tiles().beginRecordedStroke()) { m_current.reset(); return false; }
        layer->tiles().setBeforeChange(&UndoStack::capture, this);
        emit stackChanged();
        return true;
    } catch (const std::bad_alloc&) {
        // An allocation can fail after the command object exists (for example
        // while Qt attaches the layer pointer). No stroke may stay half-open.
        m_current.reset();
        fail(QStringLiteral("Not enough memory to start undo history.")); return false;
    } catch (const std::length_error&) {
        m_current.reset();
        fail(QStringLiteral("Undo history is full.")); return false;
    }
}
bool UndoStack::beginMaterialStroke(Layer* layer)
{
    if (!layer || m_current || m_replaying) return false;
    try {
        if (m_commands.size() == m_commands.capacity()) {
            const auto max = m_commands.max_size();
            if (m_commands.size() == max) throw std::length_error("history full");
            const auto growth = std::max<std::size_t>(m_commands.size(), 1);
            const auto capacity = m_commands.size() + std::min(growth, max - m_commands.size());
            if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(m_commands[0]))
                throw std::length_error("history index too large");
            const auto bytes = capacity * sizeof(m_commands[0]);
            auto budget = layer->materialTiles().documentBudget();
            if (budget && !budget->reserve(DocumentMemoryBudget::Category::History, bytes)) {
                fail(QStringLiteral("Not enough available memory to grow undo history. Free memory and try again."));
                return false;
            }
            struct Reservation {
                std::shared_ptr<DocumentMemoryBudget> budget;
                std::size_t bytes;
                ~Reservation() { if (budget) budget->release(DocumentMemoryBudget::Category::History, bytes); }
            } reservation{std::move(budget), bytes};
            m_commands.reserve(capacity);
        }
        m_current = std::make_unique<StrokeCommand>();
        m_current->layer = layer;
        m_current->budget = layer->materialTiles().documentBudget();
        m_current->materialStroke = true;
        m_current->materialBefore.reserve(4);
        m_current->materialAfter.reserve(4);
        m_current->materialBeforeCharges.reserve(4);
        m_hasLastCapturedMaterialTile = false;
        layer->materialTiles().setBeforeChange(&UndoStack::captureMaterial, this);
        emit stackChanged();
        return true;
    } catch (const std::bad_alloc&) {
        m_current.reset();
        fail(QStringLiteral("Not enough memory to start material undo history."));
        return false;
    } catch (const std::length_error&) {
        m_current.reset();
        fail(QStringLiteral("Undo history is full."));
        return false;
    }
}

SparseTileStore::CaptureResult UndoStack::capture(void* context, TilePoint tile, bool existed, std::span<const std::uint32_t> pixels) noexcept
{
    try { return static_cast<UndoStack*>(context)->captureTile(tile, existed, pixels); }
    catch (const std::bad_alloc&) { return SparseTileStore::CaptureResult::AllocationFailed; }
    catch (const std::length_error&) { return SparseTileStore::CaptureResult::AllocationFailed; }
}
SparseTileStore::CaptureResult UndoStack::captureTile(TilePoint tile, bool existed, std::span<const std::uint32_t> pixels)
{
    if (!m_current || !m_current->layer) return SparseTileStore::CaptureResult::AllocationFailed;
    for (const auto& item : m_current->before) if (item.tile == tile) return SparseTileStore::CaptureResult::Ready;
    const auto tileBytes = m_current->layer->tiles().tileBytes();
    const std::size_t charge = 2 * tileBytes + 512; // conservative allowance for containers
    const auto max = std::numeric_limits<std::size_t>::max();
    if (charge > max - m_bytes || m_currentBytes > max - m_bytes - charge)
        return SparseTileStore::CaptureResult::AllocationFailed;
    const std::size_t tracked = tileBytes * (existed ? 2 : 1);
    struct Reservation {
        DocumentMemoryBudget* budget;
        std::size_t bytes;
        bool retained = false;
        ~Reservation() { if (budget && !retained) budget->release(DocumentMemoryBudget::Category::History, bytes); }
    };
    auto* budget = m_current->budget.get();
    if (budget && !budget->reserve(DocumentMemoryBudget::Category::History, tracked)) return SparseTileStore::CaptureResult::DocumentLimit;
    Reservation reservation{budget, tracked};
    SparseTileStore::TileSnapshot before{tile, {}}, after{tile, {}};
    if (existed) before.pixels.assign(pixels.begin(), pixels.end());
    after.pixels.resize(tileBytes / sizeof(std::uint32_t)); // reserve redo before painting
    m_current->before.reserve(m_current->before.size() + 1);
    m_current->after.reserve(m_current->after.size() + 1);
    m_current->before.push_back(std::move(before));
    m_current->after.push_back(std::move(after));
    m_currentBytes += charge;
    m_current->historyBytes += tracked;
    reservation.retained = true;
    return SparseTileStore::CaptureResult::Ready;
}
bool UndoStack::captureMaterial(void* context, TilePoint tile,
                                std::span<const material::MaterialEntry> entries) noexcept
{
    try { return static_cast<UndoStack*>(context)->captureMaterialTile(tile, entries); }
    catch (const std::bad_alloc&) { return false; }
    catch (const std::length_error&) { return false; }
}
bool UndoStack::captureMaterialTile(TilePoint tile, std::span<const material::MaterialEntry> entries)
{
    if (!m_current || !m_current->layer || !m_current->materialStroke) return false;
    if (m_hasLastCapturedMaterialTile && m_lastCapturedMaterialTile == tile) return true;
    for (const auto& item : m_current->materialBefore) {
        if (item.tile == tile) {
            m_lastCapturedMaterialTile = tile;
            m_hasLastCapturedMaterialTile = true;
            return true;
        }
    }
    if (entries.size() > (std::numeric_limits<std::size_t>::max() - 512) / sizeof(material::MaterialEntry))
        return false;
    const std::size_t dataBytes = entries.size() * sizeof(material::MaterialEntry);
    const std::size_t charge = dataBytes + 512;
    if (charge > std::numeric_limits<std::size_t>::max() - m_bytes
        || m_currentBytes > std::numeric_limits<std::size_t>::max() - m_bytes - charge) return false;
    auto* budget = m_current->budget.get();
    if (budget && dataBytes && !budget->reserve(DocumentMemoryBudget::Category::History, dataBytes)) return false;
    struct Reservation {
        DocumentMemoryBudget* budget; std::size_t bytes; bool retained = false;
        ~Reservation() { if (budget && bytes && !retained) budget->release(DocumentMemoryBudget::Category::History, bytes); }
    } reservation{budget, dataBytes};
    material::MaterialTileSnapshot before{tile, std::vector<material::MaterialEntry>(entries.begin(), entries.end())};
    material::MaterialTileSnapshot after{tile, {}};
    m_current->materialBefore.reserve(m_current->materialBefore.size() + 1);
    m_current->materialAfter.reserve(m_current->materialAfter.size() + 1);
    m_current->materialBeforeCharges.reserve(m_current->materialBeforeCharges.size() + 1);
    m_current->materialBefore.push_back(std::move(before));
    m_current->materialAfter.push_back(std::move(after));
    m_current->materialBeforeCharges.push_back(dataBytes);
    m_current->layer->materialTiles().pinSnapshots(
        std::span<const material::MaterialTileSnapshot>(&m_current->materialBefore.back(), 1));
    m_current->historyBytes += dataBytes;
    m_currentBytes += charge;
    reservation.retained = true;
    m_lastCapturedMaterialTile = tile;
    m_hasLastCapturedMaterialTile = true;
    return true;
}
void UndoStack::detachCapture() noexcept
{
    m_hasLastCapturedMaterialTile = false;
    if (!m_current || !m_current->layer) return;
    if (m_current->materialStroke) m_current->layer->materialTiles().setBeforeChange(nullptr, nullptr);
    else m_current->layer->tiles().setBeforeChange(nullptr, nullptr);
}
void UndoStack::finishCapture()
{
    if (m_current->materialStroke) { finishMaterialCapture(); return; }
    auto& store = m_current->layer->tiles();
    for (std::size_t i = 0; i < m_current->before.size();) {
        auto pixels = store.tilePixels(m_current->before[i].tile);
        auto& after = m_current->after[i].pixels;
        if (pixels.empty()) after.clear();
        else std::copy(pixels.begin(), pixels.end(), after.begin());
        if (m_current->before[i].pixels == after) {
            const auto tileBytes = store.tileBytes();
            const auto tracked = tileBytes * (m_current->before[i].pixels.empty() ? 1 : 2);
            m_current->before.erase(m_current->before.begin() + static_cast<std::ptrdiff_t>(i));
            m_current->after.erase(m_current->after.begin() + static_cast<std::ptrdiff_t>(i));
            // Both snapshot buffers have been destroyed. Keep the logical
            // budget and history-limit charge in step with the retained tiles.
            if (m_current->budget) m_current->budget->release(DocumentMemoryBudget::Category::History, tracked);
            m_current->historyBytes -= tracked;
            m_currentBytes -= 2 * tileBytes + 512;
        } else ++i;
    }
}
void UndoStack::finishMaterialCapture()
{
    auto& store = m_current->layer->materialTiles();
    for (std::size_t i = 0; i < m_current->materialBefore.size();) {
        const auto current = store.tileEntries(m_current->materialBefore[i].tile);
        if (current.size() > std::numeric_limits<std::size_t>::max() / sizeof(material::MaterialEntry))
            throw std::length_error("material tile history size overflow");
        const std::size_t bytes = current.size() * sizeof(material::MaterialEntry);
        auto* budget = m_current->budget.get();
        if (budget && bytes && !budget->reserve(DocumentMemoryBudget::Category::History, bytes))
            throw std::bad_alloc();
        struct Reservation {
            DocumentMemoryBudget* budget; std::size_t bytes; bool retained = false;
            ~Reservation() { if (budget && bytes && !retained) budget->release(DocumentMemoryBudget::Category::History, bytes); }
        } reservation{budget, bytes};
        auto& after = m_current->materialAfter[i].entries;
        after.assign(current.begin(), current.end());
        m_current->historyBytes += bytes;
        m_currentBytes += bytes;
        reservation.retained = true;
        if (m_current->materialBefore[i].entries == after) {
            store.unpinSnapshots(
                std::span<const material::MaterialTileSnapshot>(&m_current->materialBefore[i], 1));
            const auto beforeBytes = m_current->materialBeforeCharges[i];
            if (budget && beforeBytes) budget->release(DocumentMemoryBudget::Category::History, beforeBytes);
            if (budget && bytes) budget->release(DocumentMemoryBudget::Category::History, bytes);
            m_current->historyBytes -= beforeBytes + bytes;
            m_currentBytes -= beforeBytes + bytes + 512;
            m_current->materialBefore.erase(m_current->materialBefore.begin() + static_cast<std::ptrdiff_t>(i));
            m_current->materialAfter.erase(m_current->materialAfter.begin() + static_cast<std::ptrdiff_t>(i));
            m_current->materialBeforeCharges.erase(m_current->materialBeforeCharges.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            store.pinSnapshots(
                std::span<const material::MaterialTileSnapshot>(&m_current->materialAfter[i], 1));
            ++i;
        }
    }
}

// Compact only after the complete raw rollback/fallback record is admitted.
// Failure is optional: retain the already-reserved raw record unchanged.
void UndoStack::tryCompact() noexcept
{
    if (m_current->before.empty()) return;
    const auto n = m_current->before.size();
    const auto tileBytes = m_current->layer->tiles().tileBytes();
    if (n > std::numeric_limits<std::size_t>::max() / (2 * tileBytes + sizeof(CompactTile))) return;
    const auto reserved = n * (2 * tileBytes + sizeof(CompactTile));
    auto* budget = m_current->budget.get();
    if (budget && !budget->reserve(DocumentMemoryBudget::Category::History, reserved)) return;
    struct Release {
        DocumentMemoryBudget* budget; std::size_t bytes;
        ~Release() { if (budget) budget->release(DocumentMemoryBudget::Category::History, bytes); }
    } extra{budget, reserved};
    try {
        std::vector<CompactTile> compact;
        compact.reserve(n);
        std::size_t actual = compact.capacity() * sizeof(CompactTile);
        if (actual > reserved) return;
        for (std::size_t i = 0; i < n; ++i) {
            auto record = history_codec::encode(m_current->before[i].pixels, m_current->after[i].pixels,
                                                static_cast<std::uint32_t>(tileBytes / sizeof(std::uint32_t)));
            if (record.payload.capacity() > reserved - actual) return;
            actual += record.payload.capacity();
            compact.push_back({m_current->before[i].tile, std::move(record)});
        }
        // Keep the conservative per-tile metadata allowance in reporting.
        if (n > (std::numeric_limits<std::size_t>::max() - actual) / 512) return;
        const auto charge = actual + 512 * n;
        if (charge >= m_currentBytes) return; // only retain a genuine saving
        if (actual > std::numeric_limits<std::size_t>::max() - m_current->historyBytes ||
            actual > std::numeric_limits<std::size_t>::max() - m_currentBytes - m_bytes) return;
        m_current->compact = std::move(compact);
        // Keep the before-images alive until the stroke is actually committed.
        // Future persistence failures must still be able to roll back.
        m_current->compactBytes = actual;
        m_current->historyBytes += actual;
        m_currentBytes += actual;
        extra.bytes -= actual; // transfer the retained part of the reservation
    } catch (const std::bad_alloc&) {} catch (const std::length_error&) {} catch (const std::invalid_argument&) {}
}
bool UndoStack::commitStroke()
{
    if (!m_current || (m_current->layer && (m_current->materialStroke
            ? m_current->layer->materialTiles().notifyingChange()
            : m_current->layer->tiles().notifyingChange()))) return false;
    if (!m_current->materialStroke && m_current->layer && m_current->layer->tiles().recordedStrokeFailed()) {
        cancelStroke();
        return false;
    }
    detachCapture();
    if (!m_current->layer) { m_current.reset(); m_currentBytes = 0; fail(QStringLiteral("The stroke layer was removed; earlier history was retained.")); return false; }
    try {
        finishCapture();
    } catch (const std::bad_alloc&) {
        fail(QStringLiteral("Not enough memory to finish stroke history; the stroke was rolled back."));
        cancelStroke();
        return false;
    } catch (const std::length_error&) {
        fail(QStringLiteral("The stroke history record is too large; the stroke was rolled back."));
        cancelStroke();
        return false;
    }
    if (!m_current->materialStroke) tryCompact();
    if (!m_current->materialStroke) m_current->layer->tiles().commitRecordedStroke();
    if (!m_current->compact.empty()) {
        const auto rawBytes = m_current->historyBytes - m_current->compactBytes;
        std::vector<SparseTileStore::TileSnapshot>().swap(m_current->before);
        std::vector<SparseTileStore::TileSnapshot>().swap(m_current->after);
        if (m_current->budget) m_current->budget->release(DocumentMemoryBudget::Category::History, rawBytes);
        m_current->historyBytes = m_current->compactBytes;
        m_currentBytes = m_current->compactBytes + 512 * m_current->compact.size();
    }
    if (m_current->before.empty() && m_current->materialBefore.empty() && m_current->compact.empty()) { m_current.reset(); m_currentBytes = 0; emit stackChanged(); return true; }
    while (m_commands.size() > m_cursor) { m_bytes -= m_commands.back()->bytes; m_commands.pop_back(); }
    m_current->bytes = m_currentBytes; m_bytes += m_currentBytes; m_currentBytes = 0;
    m_commands.push_back(std::move(m_current)); ++m_cursor;
    while (m_maxCommands > 0 && m_commands.size() > m_maxCommands && m_cursor > 0) {
        m_bytes -= m_commands.front()->bytes;
        m_commands.erase(m_commands.begin());
        --m_cursor;
    }
    emit stackChanged(); return true;
}

void UndoStack::setMaxCommands(int count)
{
    const std::size_t next = count > 0 ? static_cast<std::size_t>(count) : 0;
    if (m_maxCommands == next) return;
    m_maxCommands = next;
    bool trimmed = false;
    while (m_maxCommands > 0 && m_commands.size() > m_maxCommands && m_cursor > 0) {
        m_bytes -= m_commands.front()->bytes;
        m_commands.erase(m_commands.begin());
        --m_cursor;
        trimmed = true;
    }
    if (trimmed) emit stackChanged();
}
bool UndoStack::cancelStroke()
{
    if (!m_current) return true;
    if (m_current->layer && (m_current->materialStroke
            ? m_current->layer->materialTiles().notifyingChange()
            : m_current->layer->tiles().notifyingChange())) return false;
    detachCapture();
    if (!m_current->layer) { m_current.reset(); m_currentBytes = 0; fail(QStringLiteral("The stroke layer was removed; earlier history was retained.")); return false; }
    if (m_current->materialStroke) {
        const auto result = m_current->layer->materialTiles().restoreTiles(m_current->materialBefore, false);
        if (result != material::MaterialTileStore::Result::Changed
            && result != material::MaterialTileStore::Result::Unchanged) {
            fail(QStringLiteral("Could not roll back the material stroke; the history transaction is still active."));
            return false;
        }
    } else if (!m_current->layer->tiles().rollbackRecordedStroke(m_current->before)) {
        fail(QStringLiteral("Could not roll back the stroke; the history transaction is still active."));
        return false;
    }
    m_current.reset(); m_currentBytes = 0;
    QPointer<UndoStack> alive(this);
    emit strokeRolledBack();
    if (!alive) return true;
    emit stackChanged();
    return true;
}
void UndoStack::undo() { replay(false); }
void UndoStack::redo() { replay(true); }
void UndoStack::replay(bool forward)
{
    if (forward ? !canRedo() : !canUndo()) return;
    auto& command = m_commands[forward ? m_cursor : m_cursor - 1];
    if (!command->layer) { fail(QStringLiteral("Cannot replay history because its layer was removed. History has been retained.")); return; }
    // Keep the budget alive even if a synchronous notification destroys history.
    struct Staging {
        std::shared_ptr<DocumentMemoryBudget> budget;
        std::size_t bytes = 0;
        ~Staging() { if (budget) budget->release(DocumentMemoryBudget::Category::RestoreStaging, bytes); }
    } staging{command->budget};
    if (command->materialStroke) {
        m_replaying = true;
        QPointer<UndoStack> alive(this);
        QPointer<Layer> layer = command->layer;
        const auto& snapshots = forward ? command->materialAfter : command->materialBefore;
        const auto result = layer->materialTiles().restoreTiles(snapshots, false);
        if (result == material::MaterialTileStore::Result::Changed) {
            if (forward) ++m_cursor; else --m_cursor;
            for (const auto& snapshot : snapshots) {
                if (!layer) break;
                emit layer->artworkChanged(snapshot.tile.x, snapshot.tile.y);
                if (!alive) return;
            }
        }
        m_replaying = false;
        if (result != material::MaterialTileStore::Result::Changed) {
            fail(QStringLiteral("Material history replay could not be completed due to memory, tile or tracking limits. Artwork and history were not changed."));
            return;
        }
        emit stackChanged();
        return;
    }
    std::vector<SparseTileStore::TileSnapshot> decoded;
    const auto* snapshots = forward ? &command->after : &command->before;
    m_replaying = true;
    if (!command->compact.empty()) {
        const auto tile = command->layer->tiles().tileBytes();
        const auto count = command->compact.size();
        bool prepared = count <= std::numeric_limits<std::size_t>::max() / (2 * tile);
        const auto bytes = prepared ? count * 2 * tile : 0;
        if (prepared && staging.budget && !staging.budget->reserve(DocumentMemoryBudget::Category::RestoreStaging, bytes)) prepared = false;
        else if (prepared && staging.budget) staging.bytes = bytes;
        if (prepared) {
            try {
                decoded.reserve(count);
                auto& store = command->layer->tiles();
                for (const auto& item : command->compact) {
                    decoded.push_back({item.tile, history_codec::apply(item.record, store.tilePixels(item.tile), forward)});
                }
            } catch (const std::bad_alloc&) { prepared = false; }
              catch (const std::length_error&) { prepared = false; }
              catch (const std::invalid_argument&) { prepared = false; }
        }
        if (!prepared) {
            m_replaying = false;
            fail(QStringLiteral("History replay could not be prepared due to memory or incompatible tile state. Artwork and history were not changed."));
            return;
        }
        snapshots = &decoded;
    }
    QPointer<UndoStack> alive(this);
    QPointer<Layer> layer = command->layer;
    const auto result = layer->tiles().restoreTiles(*snapshots, false);
    if (result == SparseTileStore::WriteResult::Changed) {
        if (forward) ++m_cursor; else --m_cursor;
        for (const auto& snapshot : *snapshots) {
            if (!layer) break;
            emit layer->artworkChanged(snapshot.tile.x, snapshot.tile.y);
            if (!alive) return;
        }
    }
    m_replaying = false;
    if (result != SparseTileStore::WriteResult::Changed) {
        fail(QStringLiteral("History replay could not be completed due to memory, tile or tracking limits. Artwork and history were not changed.")); return;
    }
    emit stackChanged();
}
void UndoStack::clear()
{
    if (m_replaying) return;
    QPointer<UndoStack> alive(this);
    if (m_current && !cancelStroke()) return;
    if (!alive) return;
    m_commands.clear(); m_cursor = m_bytes = m_currentBytes = 0;
    emit stackChanged();
}
} // namespace komapaint
