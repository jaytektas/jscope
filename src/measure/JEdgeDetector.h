#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Finds the reference levels and the level crossings a timing measurement needs.
//
// TOP AND BASE ARE NOT MAX AND MIN. A real square wave overshoots, and using the
// extremes as the reference levels makes rise time measure the overshoot rather
// than the transition — every reading comes out short, consistently, in a way
// that looks plausible. So top and base come from the two densest bands of the
// amplitude histogram: the levels the signal actually SITS at. That is what a
// bench scope means by them, and it is why the 10%/90% points are worth
// anything.
//
// Crossings use hysteresis for the same class of reason: a noisy signal grazing
// its own threshold produces a burst of spurious edges, and a frequency computed
// from those is wildly wrong rather than slightly wrong.

inline namespace jf {

class JEdgeDetector {
public:
    struct JLevels {
        double top{0.0};
        double base{0.0};
        bool   valid{false};
        double amplitude() const { return top - base; }
        double mid()       const { return (top + base) * 0.5; }
    };

    struct JCrossing {
        double  time;      // seconds from the start of the record, interpolated
        bool    rising;
    };

    // Histogram the samples and take the two densest bands as base and top.
    // Falls back to min/max when the distribution has no two clear modes, which
    // is the honest answer for a sine or a ramp — they have no flat levels.
    static JLevels findLevels(const int16_t* samples, size_t count,
                              float countsToVolts, float zeroOffsetCounts);

    // Crossings of `level`, with a hysteresis band of `hysteresis` volts either
    // side. Times are linearly interpolated between the bracketing samples, so
    // resolution is not limited to the sample interval.
    static std::vector<JCrossing> findCrossings(const int16_t* samples, size_t count,
                                                float countsToVolts, float zeroOffsetCounts,
                                                double sampleInterval,
                                                double level, double hysteresis);

    // Fraction of the top-base span used as the hysteresis band by default.
    static constexpr double kDefaultHysteresisFraction = 0.10;

    // Histogram resolution for the level search. Enough bands to separate two
    // levels on an 8-bit device without smearing them together on a 12-bit one.
    static constexpr int kHistogramBins = 128;
};

} // inline namespace jf
