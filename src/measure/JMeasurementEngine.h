// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JEdgeDetector.h"
#include "JMeasurementKind.h"
#include "JMeasurementResult.h"
#include "scope/JScopeFrame.h"

#include <array>
#include <cstddef>
#include <vector>

inline namespace jf {

// Computes every automatic measurement from one plane of one frame.
//
// Headless and free of any device knowledge: it is handed samples, an affine
// transform and a sample interval, which is all a measurement can legitimately
// depend on. That is what lets the same numbers be trusted across two very
// different instruments, and what makes the whole thing testable against
// waveforms whose answers are known in closed form.
// Optional sample window, so "measure between the cursors" is the same code path
// as "measure the record". first/count are clamped to the plane. Declared outside
// the engine because a default argument cannot name a type nested in the class it
// is being declared in.
struct JMeasurementWindow {
    size_t first{0};
    size_t count{0};       // 0 means "to the end"
};

class JMeasurementEngine {
public:
    using JWindow = JMeasurementWindow;

    // All measurements for one plane. Results keep their kind, so a panel can
    // display a subset without re-deriving which is which.
    static std::vector<JMeasurementResult>
    measureAll(const JScopeFrame& frame, uint8_t plane, JMeasurementWindow window = {});

    // One measurement, when only a single readout is wanted.
    static JMeasurementResult
    measure(JMeasurementKind kind, const JScopeFrame& frame, uint8_t plane,
            JMeasurementWindow window = {});
};

} // inline namespace jf
