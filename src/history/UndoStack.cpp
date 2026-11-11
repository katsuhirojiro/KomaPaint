#include "UndoStack.h"
#include "../layers/Layer.h"
#include <algorithm>
#include <new>
#include <limits>
#include <stdexcept>

namespace komapaint {
UndoStack::UndoStack(QObject* parent) : QObject(parent) {}
UndoStack::~UndoStack() {
    if (m_current && m_current->layer) {
        detachCapture();
        m_current->layer->tiles().rollbackRecordedStroke(m_current->before);
    }
}
QString UndoStack::undoText() const { return canUndo() ? QStringLiteral("Brush stroke") : QString(); }
QString UndoStack::redoText() const { return canRedo() ? QStringLiteral("Brush stroke") : QString(); }
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
    } catch (const std::bad_alloc&) { fail(QStringLiteral("Not enough memory to start undo history.")); return false; }
      catch (const std::length_error&) { fail(QStringLiteral("Undo history is full.")); return false; }
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
void UndoStack::detachCapture() noexcept
{
    if (m_current && m_current->layer) m_current->layer->tiles().setBeforeChange(nullptr, nullptr);
}
void UndoStack::finishCapture()
{
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
        m_current->compact = std::move(compact);
        std::vector<SparseTileStore::TileSnapshot>().swap(m_current->before);
        std::vector<SparseTileStore::TileSnapshot>().swap(m_current->after);
        if (budget) budget->release(DocumentMemoryBudget::Category::History, m_current->historyBytes);
        m_current->historyBytes = actual;
        extra.bytes -= actual; // transfer the retained part of the reservation
        m_currentBytes = charge;
    } catch (const std::bad_alloc&) {} catch (const std::length_error&) {} catch (const std::invalid_argument&) {}
}
bool UndoStack::commitStroke()
{
    if (!m_current || (m_current->layer && m_current->layer->tiles().notifyingChange())) return false;
    if (m_current->layer && m_current->layer->tiles().recordedStrokeFailed()) {
        cancelStroke();
        return false;
    }
    detachCapture();
    if (!m_current->layer) { m_current.reset(); m_currentBytes = 0; fail(QStringLiteral("The stroke layer was removed; earlier history was retained.")); return false; }
    finishCapture();
    tryCompact();
    m_current->layer->tiles().commitRecordedStroke();
    if (m_current->before.empty() && m_current->compact.empty()) { m_current.reset(); m_currentBytes = 0; emit stackChanged(); return true; }
    while (m_commands.size() > m_cursor) { m_bytes -= m_commands.back()->bytes; m_commands.pop_back(); }
    m_current->bytes = m_currentBytes; m_bytes += m_currentBytes; m_currentBytes = 0;
    m_commands.push_back(std::move(m_current)); ++m_cursor;
    emit stackChanged(); return true;
}
bool UndoStack::cancelStroke()
{
    if (!m_current) return true;
    if (m_current->layer && m_current->layer->tiles().notifyingChange()) return false;
    detachCapture();
    if (!m_current->layer) { m_current.reset(); m_currentBytes = 0; fail(QStringLiteral("The stroke layer was removed; earlier history was retained.")); return false; }
    if (!m_current->layer->tiles().rollbackRecordedStroke(m_current->before)) {
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
