// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JMeasurementKind.h"

inline namespace jf {

// One measurement. `valid` is load-bearing: a record holding less than two full
// cycles has no frequency, and reporting a number anyway would be a fabrication
// the user has no way to spot. An invalid result displays as "—".
struct JMeasurementResult {
    JMeasurementKind kind{JMeasurementKind::Vpp};
    double           value{0.0};
    bool             valid{false};

    static JMeasurementResult invalid(JMeasurementKind k) { return { k, 0.0, false }; }
    static JMeasurementResult of(JMeasurementKind k, double v) { return { k, v, true }; }
};

} // inline namespace jf
