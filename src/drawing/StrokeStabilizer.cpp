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
    // Stabilisation smooths geometry, not the speed signal. Speed Dynamics has
    // its own time-based smoothing and must react to the pointer that produced
    // this sample.
    m_current.speed = raw.speed;
    if (raw.hasTilt) {
        m_current.tiltX = m_current.hasTilt ? m_current.tiltX + (raw.tiltX - m_current.tiltX) * m_weight : raw.tiltX;
        m_current.tiltY = m_current.hasTilt ? m_current.tiltY + (raw.tiltY - m_current.tiltY) * m_weight : raw.tiltY;
        m_current.hasTilt = true;
    } else {
        m_current.hasTilt = false;
        m_current.tiltX = 0.0;
        m_current.tiltY = 0.0;
    }
    if (raw.hasRotation) {
        if (m_current.hasRotation) {
            constexpr double kPi = 3.14159265358979323846;
            double diff = raw.rotationRadians - m_current.rotationRadians;
            while (diff > kPi) diff -= 2.0 * kPi;
            while (diff < -kPi) diff += 2.0 * kPi;
            m_current.rotationRadians += diff * m_weight;
        } else {
            m_current.rotationRadians = raw.rotationRadians;
        }
        m_current.hasRotation = true;
    } else {
        m_current.hasRotation = false;
        m_current.rotationRadians = 0.0;
    }
    m_current.tangentialPressure += (raw.tangentialPressure - m_current.tangentialPressure) * m_weight;
    return m_current;
}
} // namespace komapaint
