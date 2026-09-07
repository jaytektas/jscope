// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JEdgeDetector.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

inline double voltsOf(int16_t c, float countsToVolts, float zeroOffsetCounts) {
    return (static_cast<double>(c) - zeroOffsetCounts) * countsToVolts;
}

} // namespace

JEdgeDetector::JLevels JEdgeDetector::findLevels(const int16_t* samples, size_t count,
                                                 float countsToVolts,
                                                 float zeroOffsetCounts) {
    JLevels out;
    if (!samples || count == 0) return out;

    int16_t mn = samples[0], mx = samples[0];
    for (size_t i = 1; i < count; ++i) {
        mn = std::min(mn, samples[i]);
        mx = std::max(mx, samples[i]);
    }
    const double vMin = voltsOf(mn, countsToVolts, zeroOffsetCounts);
    const double vMax = voltsOf(mx, countsToVolts, zeroOffsetCounts);

    if (mx == mn) {                       // flat: top and base are the same level
        out.top = out.base = vMin;
        out.valid = true;
        return out;
    }

    // Histogram in COUNTS — integer bucketing, no float in the inner loop.
    std::vector<uint32_t> bins(kHistogramBins, 0);
    const double span = static_cast<double>(mx - mn);
    for (size_t i = 0; i < count; ++i) {
        int b = static_cast<int>((samples[i] - mn) / span * (kHistogramBins - 1));
        bins[static_cast<size_t>(std::clamp(b, 0, kHistogramBins - 1))]++;
    }

    // The two densest bands, found by DENSITY AND SEPARATION rather than by
    // splitting the range in half.
    //
    // Splitting at the midpoint fails on a single outlier. One sample at ADC
    // zero — a dropout, which this instrument does produce — stretches the
    // histogram so that BOTH real levels land in the upper half, and the
    // lower-half search then returns the outlier's own bin. On the bench that
    // reported a base of -21 V for a 0-5 V signal, which took Top, Base,
    // Amplitude and every timing measurement with it, because the mid level it
    // implied is never crossed.
    //
    // Taking the densest bin, then the densest one far enough away from it, is
    // immune to that: an outlier's bin holds one sample and is never the
    // densest anything.
    int firstBin = 0;
    uint32_t firstMax = 0;
    for (int b = 0; b < kHistogramBins; ++b)
        if (bins[b] > firstMax) { firstMax = bins[b]; firstBin = b; }

    // Far enough away that the second mode is a different LEVEL rather than the
    // shoulder of the first.
    constexpr int kMinSeparationBins = kHistogramBins / 8;
    int secondBin = firstBin;
    uint32_t secondMax = 0;
    for (int b = 0; b < kHistogramBins; ++b) {
        if (std::abs(b - firstBin) < kMinSeparationBins) continue;
        if (bins[b] > secondMax) { secondMax = bins[b]; secondBin = b; }
    }

    // Two clear modes, or not? A sine spends most of its time near the turning
    // points, so it does produce two modes — but a ramp does not, and neither
    // does noise. Requiring each mode to hold a meaningful share of the samples
    // keeps the fallback honest, and stops a lone outlier being promoted to a
    // level in its own right.
    const double modeShare = static_cast<double>(firstMax + secondMax) / static_cast<double>(count);
    constexpr double kMinSecondModeShare = 0.02;
    const bool secondIsReal =
        static_cast<double>(secondMax) / static_cast<double>(count) >= kMinSecondModeShare;
    if (modeShare < 0.05 || !secondIsReal) {
        out.base = vMin;
        out.top  = vMax;
        out.valid = true;
        return out;
    }

    const double binVolts = (vMax - vMin) / (kHistogramBins - 1);
    const int lowBin  = std::min(firstBin, secondBin);
    const int highBin = std::max(firstBin, secondBin);
    out.base  = vMin + lowBin  * binVolts;
    out.top   = vMin + highBin * binVolts;
    out.valid = true;
    return out;
}

std::vector<JEdgeDetector::JCrossing>
JEdgeDetector::findCrossings(const int16_t* samples, size_t count,
                             float countsToVolts, float zeroOffsetCounts,
                             double sampleInterval, double level, double hysteresis) {
    std::vector<JCrossing> out;
    if (!samples || count < 2) return out;

    // A signal with no amplitude has no crossings. Without this the hysteresis
    // band collapses to nothing, every sample sitting exactly at the level counts
    // as both a rise and a fall, and a DC input reports a frequency of half the
    // sample rate — a confident, completely fictitious number.
    if (!(hysteresis > 0.0)) return out;

    const double hi = level + hysteresis * 0.5;
    const double lo = level - hysteresis * 0.5;

    // Start from whichever side the signal begins on, so the first genuine
    // transition is reported rather than skipped.
    double prev = voltsOf(samples[0], countsToVolts, zeroOffsetCounts);
    bool above = prev > level;

    for (size_t i = 1; i < count; ++i) {
        const double cur = voltsOf(samples[i], countsToVolts, zeroOffsetCounts);

        // A crossing is only accepted once the signal has cleared the far side of
        // the hysteresis band. Grazing the threshold produces nothing.
        if (!above && cur >= hi) {
            const double denom = cur - prev;
            const double frac  = (denom != 0.0) ? (level - prev) / denom : 0.0;
            out.push_back({ (static_cast<double>(i - 1) + frac) * sampleInterval, true });
            above = true;
        } else if (above && cur <= lo) {
            const double denom = cur - prev;
            const double frac  = (denom != 0.0) ? (level - prev) / denom : 0.0;
            out.push_back({ (static_cast<double>(i - 1) + frac) * sampleInterval, false });
            above = false;
        }
        prev = cur;
    }
    return out;
}

} // inline namespace jf
