#include "StrokeStabilizer.h"
#include <algorithm>
#include <cmath>

namespace komapaint {
StrokeStabilizer::StrokeStabilizer(int strength)
    : m_strength(std::clamp(strength, 0, MaxStrength)), m_weight(responsiveness(m_strength)) {}

double StrokeStabilizer::responsiveness(int strength)
{
    return 1.0 / (1.0 + 0.24 * std::clamp(strength, 0, MaxStrength));
}

void StrokeStabilizer::begin(InputSample first)
{
    m_current = first;
    m_started = true;
}

InputSample StrokeStabilizer::filter(InputSample raw)
{
    // Invalid input is passed on untouched: the stroke rejects it, exactly as
    // it does without stabilisation, and the smoothed state stays usable.
    if (!std::isfinite(raw.x) || !std::isfinite(raw.y) || !std::isfinite(raw.pressure)) return raw;
    if (!m_started || m_strength == 0) { m_current = raw; m_started = true; return raw; }
    m_current.x += (raw.x - m_current.x) * m_weight;
    m_current.y += (raw.y - m_current.y) * m_weight;
    m_current.pressure += (raw.pressure - m_current.pressure) * m_weight;
    return m_current;
}
} // namespace komapaint
