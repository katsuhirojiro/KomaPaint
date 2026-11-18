// Proves that komapaint::roundHalfUpNonNegative() is an exact replacement for
// std::lround() over the range the pixel paths feed it, on every compiler we
// ship. The helper exists because MSVC does not inline std::lround; the whole
// point is that it must never change a pixel, so the equivalence is asserted
// here rather than trusted.
#include "core/FastRounding.h"

#include <QtTest/QtTest>
#include <cmath>
#include <random>

class TestFastRounding : public QObject
{
    Q_OBJECT

private slots:
    void matchesLroundAcrossByteRange()
    {
        long long mismatches = 0;
        for (int i = 0; i <= 256 * 2048; ++i) {
            const double value = double(i) / 2048.0;
            if (std::lround(value) != static_cast<long long>(komapaint::roundHalfUpNonNegative(value)))
                ++mismatches;
        }
        QCOMPARE(mismatches, 0LL);
    }

    void matchesLroundOnHalfStepsAndNeighbours()
    {
        long long mismatches = 0;
        for (int k = 0; k <= 255; ++k) {
            const double half = double(k) + 0.5;
            const double candidates[] = {
                half,
                std::nextafter(half, 0.0),
                std::nextafter(half, 512.0),
                double(k),
                std::nextafter(double(k), 0.0),
                std::nextafter(double(k), 512.0),
            };
            for (double value : candidates) {
                if (std::lround(value) != static_cast<long long>(komapaint::roundHalfUpNonNegative(value)))
                    ++mismatches;
            }
        }
        QCOMPARE(mismatches, 0LL);
    }

    void matchesLroundOnRandomValues()
    {
        std::mt19937_64 rng(20261117);
        std::uniform_real_distribution<double> byteRange(0.0, 256.0);
        long long mismatches = 0;
        for (int i = 0; i < 2000000; ++i) {
            const double value = byteRange(rng);
            if (std::lround(value) != static_cast<long long>(komapaint::roundHalfUpNonNegative(value)))
                ++mismatches;
        }
        QCOMPARE(mismatches, 0LL);
    }

    void handlesValuesOutsideTheByteRangeSafely()
    {
        QCOMPARE(komapaint::roundHalfUpNonNegative(0.0), 0U);
        QCOMPARE(komapaint::roundHalfUpNonNegative(255.5), 256U); // callers clamp
        QCOMPARE(komapaint::roundHalfUpNonNegative(-0.4), 0U);    // negative clamps to 0
        QCOMPARE(komapaint::roundHalfUpNonNegative(-7.0), 0U);
        // Non-finite input must not become undefined behaviour.
        QCOMPARE(komapaint::roundHalfUpNonNegative(std::nan("")), 0U);
        QCOMPARE(komapaint::roundHalfUpNonNegative(-std::numeric_limits<double>::infinity()), 0U);
        QVERIFY(komapaint::roundHalfUpNonNegative(std::numeric_limits<double>::infinity()) > 0U);
        QVERIFY(komapaint::roundHalfUpNonNegative(1e300) > 0U);
    }
};

QTEST_MAIN(TestFastRounding)
#include "TestFastRounding.moc"
