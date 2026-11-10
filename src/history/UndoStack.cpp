#include "UndoStack.h"
#include "../layers/Layer.h"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace komapaint {
UndoStack::UndoStack(QObject* parent) : QObject(parent) {}
UndoStack::~UndoStack() { detachCapture(); }
QString UndoStack::undoText() const { return canUndo() ? QStringLiteral("Brush stroke") : QString(); }
QString UndoStack::redoText() const { return canRedo() ? QStringLiteral("Brush stroke") : QString(); }
bool UndoStack::setByteLimit(std::size_t bytes) {
    if (m_current || !m_commands.empty()) return false;
    m_limit = bytes; return true;
}
bool UndoStack::beginStroke(Layer* layer)
{
    if (!layer || m_current) return false;
    try {
        m_commands.reserve(m_commands.size() + 1); // commit must not allocate
        m_current = std::make_unique<StrokeCommand>();
        m_current->layer = layer;
        layer->tiles().setBeforeChange(&UndoStack::capture, this);
        emit stackChanged();
        return true;
    } catch (const std::bad_alloc&) { fail(QStringLiteral("Not enough memory to start undo history.")); return false; }
      catch (const std::length_error&) { fail(QStringLiteral("Undo history is full.")); return false; }
}
bool UndoStack::capture(void* context, TilePoint tile, bool existed, std::span<const std::uint32_t> pixels) noexcept
{
    try { return static_cast<UndoStack*>(context)->captureTile(tile, existed, pixels); }
    catch (const std::bad_alloc&) { return false; }
    catch (const std::length_error&) { return false; }
}
bool UndoStack::captureTile(TilePoint tile, bool existed, std::span<const std::uint32_t> pixels)
{
    if (!m_current || !m_current->layer) return false;
    for (const auto& item : m_current->before) if (item.tile == tile) return true;
    const auto tileBytes = m_current->layer->tiles().tileBytes();
    const std::size_t charge = 2 * tileBytes + 512; // conservative allowance for containers
    if (charge > m_limit || m_bytes > m_limit - charge || m_currentBytes > m_limit - charge - m_bytes) return false;
    SparseTileStore::TileSnapshot before{tile, {}}, after{tile, {}};
    if (existed) before.pixels.assign(pixels.begin(), pixels.end());
    after.pixels.resize(tileBytes / sizeof(std::uint32_t)); // reserve redo before painting
    m_current->before.reserve(m_current->before.size() + 1);
    m_current->after.reserve(m_current->after.size() + 1);
    m_current->before.push_back(std::move(before));
    m_current->after.push_back(std::move(after));
    m_currentBytes += charge;
    return true;
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
            m_current->before.erase(m_current->before.begin() + static_cast<std::ptrdiff_t>(i));
            m_current->after.erase(m_current->after.begin() + static_cast<std::ptrdiff_t>(i));
        } else ++i;
    }
}
bool UndoStack::commitStroke()
{
    if (!m_current) return false;
    detachCapture();
    if (!m_current->layer) { m_current.reset(); m_currentBytes = 0; clear(); fail(QStringLiteral("Layer was removed; history was cleared.")); return false; }
    finishCapture();
    if (m_current->before.empty()) { m_current.reset(); m_currentBytes = 0; emit stackChanged(); return true; }
    while (m_commands.size() > m_cursor) { m_bytes -= m_commands.back()->bytes; m_commands.pop_back(); }
    m_current->bytes = m_currentBytes; m_bytes += m_currentBytes; m_currentBytes = 0;
    m_commands.push_back(std::move(m_current)); ++m_cursor;
    emit stackChanged(); return true;
}
bool UndoStack::cancelStroke()
{
    if (!m_current) return true;
    detachCapture();
    if (!m_current->layer) { m_current.reset(); m_currentBytes = 0; clear(); fail(QStringLiteral("Layer was removed; history was cleared.")); return false; }
    finishCapture();
    if (m_current->before.empty()) { m_current.reset(); m_currentBytes = 0; emit stackChanged(); return true; }
    auto result = m_current->layer->tiles().restoreTiles(m_current->before);
    if (result != SparseTileStore::WriteResult::Changed) {
        // Keep the partial stroke as a real undoable command; never discard its history.
        commitStroke();
        fail(QStringLiteral("Could not cancel the stroke; partial paint remains and can be undone."));
        return false;
    }
    m_current.reset(); m_currentBytes = 0; emit stackChanged(); return true;
}
void UndoStack::undo()
{
    if (!canUndo()) return;
    auto& command = m_commands[m_cursor - 1];
    if (!command->layer) { clear(); fail(QStringLiteral("A layer was removed; history was cleared.")); return; }
    if (command->layer->tiles().restoreTiles(command->before) != SparseTileStore::WriteResult::Changed) {
        fail(QStringLiteral("Undo could not be completed due to memory or tile limits. Artwork was not changed.")); return;
    }
    --m_cursor; emit stackChanged();
}
void UndoStack::redo()
{
    if (!canRedo()) return;
    auto& command = m_commands[m_cursor];
    if (!command->layer) { clear(); fail(QStringLiteral("A layer was removed; history was cleared.")); return; }
    if (command->layer->tiles().restoreTiles(command->after) != SparseTileStore::WriteResult::Changed) {
        fail(QStringLiteral("Redo could not be completed due to memory or tile limits. Artwork was not changed.")); return;
    }
    ++m_cursor; emit stackChanged();
}
void UndoStack::clear()
{
    if (m_current && !cancelStroke()) return;
    m_commands.clear(); m_cursor = m_bytes = m_currentBytes = 0;
    emit stackChanged();
}
} // namespace komapaint
