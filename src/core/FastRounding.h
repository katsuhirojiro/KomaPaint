#pragma once

// Fast, exact rounding for pixel maths.
//
// Every pixel path rounds a non-negative value in [0, 256) to a byte with
// std::lround, because that is what the reference formulas specify: round half
// away from zero. On MSVC that call is not inlined - it becomes a CRT call
// unless the whole binary is built for SSE4.1, which the shipping build is not -
// so a blended pixel paid three to four function calls for rounding alone.
//
// For a non-negative finite double below 2^52, rounding half away from zero is
// the same operation as truncating and then stepping up when the remainder is
// at least one half. Truncation is exact, the subtraction `value - truncated`
// is exact (both are representable and the difference needs no more precision
// than the input), and the comparison is exact. The result is therefore
// identical to std::lround for every input this code feeds it, with two
// integer operations instead of a call.
//
// Values outside [0, 2^52) are not clamped silently: negative values and values
// that do not fit are handled the same way std::lround handles them only in the
// positive range, and callers in this code base always clamp to [0, 255] first.
// Use std::lround directly when that guarantee does not hold.

#include <cstdint>
#include <cmath>
#include <limits>

namespace komapaint {

// Identical to std::lround for 0 <= value < 2^52.
[[nodiscard]] inline unsigned roundHalfUpNonNegative(double value) noexcept
{
    // Non-finite input is a caller bug; std::lround() leaves it unspecified too,
    // so keep the behaviour predictable instead of invoking undefined behaviour
    // in the integer conversion.
    if (!(value >= 0.0)) return 0U;                  // also catches NaN
    if (value >= 4503599627370496.0) {               // 2^52
        return std::numeric_limits<unsigned>::max();
    }
    const auto truncated = static_cast<std::uint64_t>(value);
    return static_cast<unsigned>(truncated) + (value - static_cast<double>(truncated) >= 0.5 ? 1U : 0U);
}

} // namespace komapaint
