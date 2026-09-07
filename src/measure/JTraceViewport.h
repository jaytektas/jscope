// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstddef>
#include <cstdint>

// The mapping from a window of samples onto a rectangle of pixels. Everything
// the decimator needs and nothing about widgets, so it stays testable headlessly.

inline namespace jf {

struct JTraceViewport {
    // Destination rectangle, in screen pixels.
    float x{0.0f}, y{0.0f}, width{0.0f}, height{0.0f};

    // Source window: which samples of the plane are on screen.
    size_t firstSample{0};
    size_t sampleCount{0};

    // Vertical mapping. verticalDivisions * voltsPerDiv spans the full height.
    double  voltsPerDiv{1.0};
    double  offsetVolts{0.0};
    uint8_t verticalDivisions{8};
    bool    inverted{false};
};

} // inline namespace jf
