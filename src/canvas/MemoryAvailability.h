#pragma once
#include <cstddef>

namespace komapaint {
// Advisory admission check for a prospective major allocation, not a memory
// reservation. Allocation may still fail and callers must handle that safely.
// If the platform cannot provide an observation, allocation is attempted.
bool systemMemoryAvailable(std::size_t bytes) noexcept;
}
