// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JReferenceSignal.h"

#include <cstddef>
#include <string>
#include <vector>

// Generates the reference trace for a signal.
//
// Headless and pure: the same signal always produces the same samples, because
// the dither is a fixed-seed sequence rather than a random one. That is what
// lets a test assert a peak or a period, and it means the picture does not
// shimmer between redraws.
//
// EACH MODEL CHOOSES ITS OWN WINDOW. A crank sensor shown across two full
// revolutions is a solid block of ink at any sensible width — the useful view
// is a handful of teeth either side of the gap, and only the model knows what a
// handful means for its own signal. So `window` comes back with the trace and
// the caller reports it rather than deciding it.
//
// The values are the textbook ones for each device and are named in the .cpp,
// where they can be read and disagreed with. They are facts about hardware, so
// they live in the model and not in the theme.

inline namespace jf {

class JReferenceWaveform {
public:
    struct JTrace {
        std::vector<float> samples;
        double      window{0.0};        // seconds the samples span
        double      minValue{0.0};      // as generated, before any display scaling
        double      maxValue{0.0};
        const char* unit{"V"};          // "V", or "A" for the current models
        std::string caption;            // what the shape is showing, for the legend
    };

    // An empty trace for None, so a caller can render unconditionally.
    static JTrace generate(JReferenceSignal signal);
};

} // inline namespace jf
