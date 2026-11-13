#include "drawing/StrokeStabilizer.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace komapaint;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "FAIL line " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::exit(1); } } while (false)

// Sum of the up-and-down movement of a filtered zig-zag: a measure of wobble.
static double wobble(int strength)
{
    StrokeStabilizer s(strength);
    s.begin({0, 0, 1});
    double total = 0, previous = 0;
    for (int i = 1; i <= 200; ++i) {
        const InputSample out = s.filter({static_cast<double>(i), (i % 2 == 0) ? 3.0 : -3.0, 1});
        total += std::abs(out.y - previous);
        previous = out.y;
    }
    return total;
}

int main()
{
    // Strength 0 is a pass-through: painting is exactly what it was before.
    {
        StrokeStabilizer off(0);
        off.begin({10, 20, 0.5});
        const InputSample out = off.filter({37.25, -4.5, 0.75});
        CHECK(out.x == 37.25 && out.y == -4.5 && out.pressure == 0.75);
        CHECK(StrokeStabilizer::responsiveness(0) == 1.0);
    }
    // More strength follows more slowly, and never overshoots the pointer.
    {
        double last = 1.0;
        for (int strength = 1; strength <= 100; ++strength) {
            const double r = StrokeStabilizer::responsiveness(strength);
            CHECK(r > 0 && r < last);
            last = r;
        }
        CHECK(std::abs(StrokeStabilizer::responsiveness(100) - 0.04) < 1e-9);
        StrokeStabilizer s(50);
        s.begin({0, 0, 1});
        double x = 0;
        for (int i = 0; i < 400; ++i) {
            const double next = s.filter({100, 0, 1}).x;
            CHECK(next >= x && next <= 100.0);
            x = next;
        }
        CHECK(x > 99.9);            // it does arrive when the pointer rests
    }
    // A shaky line gets smoother as the strength goes up.
    {
        const double none = wobble(0), light = wobble(10), medium = wobble(40), heavy = wobble(100);
        CHECK(none > 1100);         // 200 swings of 6 px
        CHECK(light < none * 0.5);
        CHECK(medium < light * 0.5);
        CHECK(heavy < medium);
        CHECK(heavy < none * 0.05);
    }
    // Pressure is smoothed with the position and stays between the inputs.
    {
        StrokeStabilizer s(30);
        s.begin({0, 0, 0.2});
        const InputSample out = s.filter({10, 0, 1.0});
        CHECK(out.pressure > 0.2 && out.pressure < 1.0);
        CHECK(out.x > 0 && out.x < 10);
    }
    // Out-of-range strengths are clamped; broken samples are passed on
    // untouched so the brush engine rejects them as it always has.
    {
        CHECK(StrokeStabilizer(-5).strength() == 0);
        CHECK(StrokeStabilizer(250).strength() == StrokeStabilizer::MaxStrength);
        StrokeStabilizer s(60);
        s.begin({5, 5, 1});
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const InputSample bad = s.filter({nan, 5, 1});
        CHECK(std::isnan(bad.x));
        const InputSample good = s.filter({15, 5, 1});
        CHECK(std::isfinite(good.x) && good.x > 5 && good.x < 15);   // state was not poisoned
    }
    // filter() before begin() starts at the given sample.
    {
        StrokeStabilizer s(80);
        const InputSample out = s.filter({42, 7, 0.5});
        CHECK(out.x == 42 && out.y == 7 && out.pressure == 0.5);
    }
    std::cout << "Stroke stabilizer tests passed\n";
}
