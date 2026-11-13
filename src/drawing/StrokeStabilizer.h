#pragma once
#include "BrushEngine.h"

namespace komapaint {
// Smooths pointer input before it reaches the brush, so a shaky hand or a
// jittery mouse still gives an even line. Each new sample pulls the drawn
// point only part of the way towards the pointer; the stronger the setting,
// the smaller that part, and the further the line trails behind the pointer.
// Strength 0 returns every sample unchanged. The caller ends the stroke at
// the real pointer position, so a stabilised line always reaches the point
// where the pen was lifted.
class StrokeStabilizer {
public:
    static constexpr int MaxStrength = 100;
    explicit StrokeStabilizer(int strength = 0);
    int strength() const { return m_strength; }
    // Share of the remaining distance covered per sample: 1 at strength 0,
    // falling to 0.04 at the maximum.
    static double responsiveness(int strength);
    void begin(InputSample first);
    InputSample filter(InputSample raw);
private:
    int m_strength = 0;
    double m_weight = 1;
    InputSample m_current{};
    bool m_started = false;
};
} // namespace komapaint
