// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdio>
#include <string>

// How a number is SHOWN. Engineering notation, in the units a scope's front
// panel uses — 500 ns, 20 us, 1 ms; 50.0 mV, 2 V.
//
// Presentation, so it belongs to the UI layer and not the HAL: drivers deal in
// volts and seconds and nothing else. Gathered here because the channel strip,
// the timebase panel and the on-screen legend all have to agree — they carried
// their own copies, and a legend that rounded differently from the control that
// set it is a bug report waiting to happen.

inline namespace jf {

inline std::string jScopeFormatVolts(double v) {
    char buf[32];
    const double a = v < 0.0 ? -v : v;
    if (a < 1.0) std::snprintf(buf, sizeof buf, "%g mV", v * 1000.0);
    else         std::snprintf(buf, sizeof buf, "%g V",  v);
    return buf;
}

inline std::string jScopeFormatSeconds(double s) {
    char buf[32];
    const double a = s < 0.0 ? -s : s;
    if (a < 1.0e-6)      std::snprintf(buf, sizeof buf, "%g ns", s * 1.0e9);
    else if (a < 1.0e-3) std::snprintf(buf, sizeof buf, "%g us", s * 1.0e6);
    else if (a < 1.0)    std::snprintf(buf, sizeof buf, "%g ms", s * 1.0e3);
    else                 std::snprintf(buf, sizeof buf, "%g s",  s);
    return buf;
}

} // inline namespace jf
