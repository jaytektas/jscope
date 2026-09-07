// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JSyntheticWaveform.h"

inline namespace jf {

// One synthetic channel's signal definition, in volts and seconds.
struct JSyntheticChannel {
    JSyntheticWaveform waveform{JSyntheticWaveform::Sine};
    double amplitudeVolts{1.0};   // peak, so Vpp is twice this
    double frequencyHz{1000.0};
    double phaseRadians{0.0};
    double offsetVolts{0.0};
    double dutyCycle{0.5};        // Square only

    // Edge duration, as a fraction of one period, for the shapes that have a
    // transition (Square, Ramp). NOT cosmetic: a mathematically instantaneous
    // edge makes a sample landing exactly on it ambiguous, and the sweep time
    // cannot land in the same place twice because a timebase step such as 1 ms
    // has no exact binary representation. The phase therefore differs by ~1e-13
    // between frames, which is harmless on a continuous waveform and flips a
    // sample between rails on a discontinuous one — a visible twitch on exactly
    // the shapes that have an edge, and on no others.
    //
    // A finite edge removes the ambiguity structurally rather than papering over
    // it, is what any real signal actually does, and gives rise/fall-time
    // measurement something true to measure against.
    double edgeFraction{0.002};

    double noiseVolts{0.0};       // uniform +/- this, added to any shape
};

} // inline namespace jf
