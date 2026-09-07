// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "measure/JMeasurementKind.h"

#include <string>

inline namespace jf {

// Engineering-notation formatting for readouts.
//
// One place, because the timebase panel, the measurement panel and the cursor
// panel all display volts and seconds, and three private copies of "is this
// microseconds or milliseconds" would drift apart the first time one of them was
// touched.
class JValueFormat {
public:
    static std::string volts(double v);
    static std::string seconds(double s);
    static std::string hertz(double hz);
    static std::string percent(double pct);

    // Format according to the unit a measurement kind is expressed in.
    static std::string measurement(JMeasurementKind kind, double value);

    // What an invalid reading shows. A measurement with too little data to be
    // computed must look unmistakably absent, never like a real zero.
    static const char* invalid() { return "—"; }
};

} // inline namespace jf
