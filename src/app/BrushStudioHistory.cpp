#include "BrushStudioModel.h"

#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace komapaint {
namespace {

// Charge retained capacities, including nested buffers. For admission, ask
// about a copy of the live metadata (its sizes, not any spare capacity). The
// pixel payloads are immutable shared allocations and don't need copying.
class MetadataBytes {
public:
    explicit MetadataBytes(bool copying) : m_copying(copying) {}

    void add(std::size_t count, std::size_t width = 1) noexcept {
        constexpr auto max = std::numeric_limits<std::size_t>::max();
        m_bytes = count > (max - m_bytes) / width ? max : m_bytes + count * width;
    }

    template<class T>
    void buffer(const std::vector<T>& items) noexcept {
        add(m_copying ? items.size() : items.capacity(), sizeof(T));
    }

    void text(const std::string& value) noexcept {
        const auto length = m_copying ? value.size() : value.capacity();
        // Inline string storage is already included in the containing object.
        if (length > std::string{}.capacity()) {
            add(length);
            add(1); // terminator
        }
    }

    void texts(const std::vector<std::string>& items) noexcept {
        buffer(items);
        for (const auto& item : items) text(item);
    }

    void bag(const brush::ParamBag& values) noexcept {
        buffer(values.entries());
        for (const auto& entry : values.entries()) text(entry.first);
    }

    void document(const brush::BrushDocument& d) noexcept {
        text(d.id); text(d.name); text(d.description); text(d.thumbnail); text(d.category);
        texts(d.tags);
        buffer(d.components);
        for (const auto& c : d.components) {
            text(c.id); text(c.name); text(c.resourceId); text(c.groupId); text(c.maskComponentId);
            bag(c.parameters);
        }
        buffer(d.groups);
        for (const auto& g : d.groups) {
            text(g.id); text(g.name); text(g.parentGroupId);
            texts(g.members);
        }
        buffer(d.generators);
        for (const auto& g : d.generators) { text(g.id); text(g.groupId); bag(g.parameters); }
        buffer(d.dynamics);
        for (const auto& binding : d.dynamics) {
            text(binding.parameter);
            buffer(binding.curve.points());
        }
        buffer(d.randomization);
        for (const auto& rule : d.randomization) text(rule.parameter);
        buffer(d.exposed);
        for (const auto& prop : d.exposed) { text(prop.parameter); text(prop.label); text(prop.section); }
        buffer(d.resources);
        for (const auto& r : d.resources) { text(r.id); text(r.name); }
        bag(d.parameters);
        buffer(d.opaqueChunks);
        for (const auto& chunk : d.opaqueChunks) { text(chunk.tag); text(chunk.payload); }
    }

    std::size_t bytes() const noexcept { return m_bytes; }

private:
    bool m_copying;
    std::size_t m_bytes = 0;
};

std::string_view selectionId(const brush::BrushDocument& d, int index) noexcept {
    return index >= 0 && static_cast<std::size_t>(index) < d.components.size()
        ? std::string_view(d.components[static_cast<std::size_t>(index)].id) : std::string_view{};
}

} // namespace

BrushStudioModel::StorageUse BrushStudioModel::retainedStorage() const {
    MetadataBytes metadata(false), pixels(false);
    std::unordered_set<const std::vector<std::uint8_t>*> blobs;
    const auto count = [&](const brush::BrushDocument& document) {
        metadata.document(document);
        for (const auto& resource : document.resources) {
            const auto& bytes = resource.pixels();
            if (!bytes.empty() && blobs.insert(&bytes).second) {
                metadata.add(sizeof(bytes));
                pixels.add(bytes.capacity());
            }
        }
    };
    metadata.add(sizeof(m_document));
    count(m_document);
    for (const auto* history : {&m_undo, &m_redo}) {
        metadata.add(history->size(), StepBytes);
        for (const auto& step : *history) count(step.document);
    }
    return {metadata.bytes(), pixels.bytes(), m_undo.size() + m_redo.size()};
}

bool BrushStudioModel::editTransaction(const char* label, void* context, bool (*apply)(void*), EditMode mode,
                                       const brush::BrushDocument* incomingCopy) {
    bool documentChangedByEdit = false;
    bool selectionChangedByEdit = false;
    const bool wasModified = m_modified;
    try {
        MetadataBytes copy(true);
        copy.add(StepBytes);
        copy.document(m_document);
        if (incomingCopy && incomingCopy != &m_document) copy.document(*incomingCopy);
        if (m_memoryCheck && !m_memoryCheck(m_memoryContext, copy.bytes())) return historyMemoryError();
        // Reserve and populate the undo node before mutating anything. It also
        // rolls back an individual batched edit if that edit runs out of memory.
        m_undo.emplace_back(m_document, m_selectedIndex, m_modified);
        auto& before = m_undo.back();
        bool applied;
        try {
            applied = apply(context);
            documentChangedByEdit = applied && m_document != before.document;
            selectionChangedByEdit = applied && (m_selectedIndex != before.selectedIndex ||
                selectionId(m_document, m_selectedIndex) != selectionId(before.document, before.selectedIndex));
        } catch (...) {
            swapCurrent(before);
            m_undo.pop_back();
            throw;
        }
        if (!applied) {
            swapCurrent(before);
            m_undo.pop_back();
            const QString action = m_inStep && !m_stepLabel.isEmpty() ? m_stepLabel : QString::fromUtf8(label);
            return fail(QStringLiteral("Could not apply brush edit: %1.").arg(action));
        }
        if (!documentChangedByEdit || (mode == EditMode::Change && m_inStep && m_stepHasEdit)) m_undo.pop_back();
        if (documentChangedByEdit) {
            m_redo.clear(); // Only a successful divergent edit invalidates redo.
            m_modified = true;
            m_stepHasEdit = m_inStep;
        }
        if (mode != EditMode::Change) commitStep();
        if (mode == EditMode::LoadSaved) markSaved();
    } catch (const std::bad_alloc&) {
        return historyMemoryError();
    } catch (const std::length_error&) {
        return fail(QStringLiteral("This brush edit exceeds the size this system can store. Nothing was changed."));
    }
    clearError();
    if (documentChangedByEdit || m_modified != wasModified) emit documentChanged();
    if (documentChangedByEdit || selectionChangedByEdit) emit componentsChanged();
    if (selectionChangedByEdit) emit selectionChanged();
    if (documentChangedByEdit) {
        emit exposureChanged();
        emit historyChanged();
    }
    return true;
}

void BrushStudioModel::swapCurrent(Step& step) noexcept {
    static_assert(std::is_nothrow_swappable_v<brush::BrushDocument>);
    std::swap(m_document, step.document);
    std::swap(m_selectedIndex, step.selectedIndex);
    std::swap(m_modified, step.modified);
}

void BrushStudioModel::replay(History& source, History& destination) {
    if (source.empty()) return;
    commitStep();
    const auto state = std::prev(source.end());
    // Reuse the existing node in the opposite direction; replay neither
    // allocates a snapshot nor needs permission to grow history.
    swapCurrent(*state);
    destination.splice(destination.end(), source, state);
    clearError();
    emit documentChanged();
    emit componentsChanged();
    emit selectionChanged();
    emit exposureChanged();
    emit historyChanged();
}

void BrushStudioModel::undo() { replay(m_undo, m_redo); }
void BrushStudioModel::redo() { replay(m_redo, m_undo); }

void BrushStudioModel::beginStep(const QString& label) {
    if (m_inStep) return;
    m_stepLabel = label;
    m_inStep = true;
    m_stepHasEdit = false;
}

void BrushStudioModel::commitStep() {
    m_stepHasEdit = false;
    m_inStep = false;
    m_stepLabel.clear();
}

void BrushStudioModel::markSaved() noexcept {
    // The last successful save (or load) is the one clean state. Swapping the
    // flag along with the snapshot locates that state during replay.
    for (auto* history : {&m_undo, &m_redo}) {
        for (auto& step : *history) step.modified = true;
    }
    m_modified = false;
}

void BrushStudioModel::setModified(bool modified) {
    if (m_modified == modified) return;
    if (modified) m_modified = true;
    else {
        commitStep();
        markSaved();
    }
    emit documentChanged();
}

bool BrushStudioModel::historyMemoryError() {
    return fail(QStringLiteral("Not enough available memory to record this brush edit. The brush and its history were not changed."));
}

} // namespace komapaint
