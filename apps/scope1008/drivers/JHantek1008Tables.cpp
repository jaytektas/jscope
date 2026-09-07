// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JHantek1008Tables.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

uint8_t JHantek1008Tables::nsPerDivIdFor(double secondsPerDiv) {
    const double window = secondsPerDiv * kHorizontalDivisions;

    // The window and a code's record duration are reached by different
    // arithmetic from different constants, so AT AN EXACT LADDER VALUE they can
    // disagree in the last bit. 500us/div is the case that showed it: the
    // requested window comes out 0.005000000000000000104 and code 17's record
    // 0.004999999999999999237, so a bare >= stepped straight past the code that
    // exists to serve it and returned code 18 — the panel asking for 500us/div
    // and the instrument running at 1ms/div, a factor of two.
    //
    // It matters at every step rather than just this one, because every value
    // the panel offers IS an exact ladder value. A relative tolerance costs
    // nothing here and removes the whole class of it.
    constexpr double kLadderBoundaryTolerance = 1.0e-9;

    // Usable codes are in ascending duration order, so the first that covers the
    // window is also the one that samples it most finely.
    for (uint8_t id : kUsableTimeDivIds)
        if (recordDurationFor(id) >= window * (1.0 - kLadderBoundaryTolerance)) return id;
    return kUsableTimeDivIds.back();
}

uint8_t JHantek1008Tables::rollRateIdFor(double samplesPerSecond) {
    uint8_t best = kRollRates.back().id;
    double  bestErr = 1.0e300;
    for (const JRollRate& r : kRollRates) {
        const double err = std::abs(std::log(samplesPerSecond / r.rate));
        if (err < bestErr) { bestErr = err; best = r.id; }
    }
    return best;
}

} // inline namespace jf
