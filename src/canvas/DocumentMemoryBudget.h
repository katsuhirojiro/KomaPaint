#pragma once
#include <cassert>
#include <cstddef>
#include <limits>

namespace komapaint {
// First-stage accounting for layer pixel buffers and restore staging only.
// Owned/shared by a document's layers, used on the document thread. Not a
// process-memory meter and not yet a complete document resource manager.
class DocumentMemoryBudget {
public:
    enum class Category { Artwork, RestoreStaging };
    explicit DocumentMemoryBudget(std::size_t limit = std::numeric_limits<std::size_t>::max()) : m_limit(limit) {}
    std::size_t limit() const noexcept { return m_limit; }
    std::size_t artworkBytes() const noexcept { return m_artwork; }
    std::size_t stagingBytes() const noexcept { return m_staging; }
    std::size_t usedBytes() const noexcept { return m_artwork + m_staging; }
    std::size_t peakBytes() const noexcept { return m_peak; }
    bool setLimit(std::size_t limit) noexcept {
        if (limit < usedBytes()) return false;
        m_limit = limit; return true;
    }
    bool reserve(Category category, std::size_t bytes) noexcept {
        if (bytes > m_limit || usedBytes() > m_limit - bytes) return false;
        counter(category) += bytes;
        if (usedBytes() > m_peak) m_peak = usedBytes();
        return true;
    }
    void release(Category category, std::size_t bytes) noexcept {
        auto& count = counter(category);
        assert(bytes <= count);
        count -= bytes;
    }
    void transferToArtwork(std::size_t bytes) noexcept {
        assert(bytes <= m_staging);
        m_staging -= bytes;
        m_artwork += bytes; // total is unchanged
    }
private:
    std::size_t& counter(Category category) noexcept { return category == Category::Artwork ? m_artwork : m_staging; }
    std::size_t m_limit, m_artwork = 0, m_staging = 0, m_peak = 0;
};
} // namespace komapaint
