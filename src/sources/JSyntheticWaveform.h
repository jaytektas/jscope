// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>

// Waveform shapes the synthetic source can produce. Analytic, so a test can
// assert the measured Vpp, RMS, frequency and duty against values known in
// closed form — which is what makes JMeasurementEngine testable at all.

inline namespace jf {

enum class JSyntheticWaveform : uint8_t { Sine, Square, Triangle, Ramp, Noise, Dc };

inline const char* jSyntheticWaveformName(JSyntheticWaveform w) {
    switch (w) {
        case JSyntheticWaveform::Sine:     return "Sine";
        case JSyntheticWaveform::Square:   return "Square";
        case JSyntheticWaveform::Triangle: return "Triangle";
        case JSyntheticWaveform::Ramp:     return "Ramp";
        case JSyntheticWaveform::Noise:    return "Noise";
        case JSyntheticWaveform::Dc:       return "DC";
    }
    return "Sine";
}

} // inline namespace jf
