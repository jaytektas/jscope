// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeGeneratorKind.h"
#include <string>
#include <vector>

inline namespace jf {

struct JScopeGeneratorCapabilities {
    JScopeGeneratorKind kind{JScopeGeneratorKind::None};

    // AnalogDds
    std::vector<std::string> waveforms;        // "SINE", "SQUARE", …  device spelling
    double minFrequencyHz{0.0}, maxFrequencyHz{0.0};
    double minAmplitudeVpp{0.0}, maxAmplitudeVpp{0.0};
    double minOffsetVolts{0.0},  maxOffsetVolts{0.0};
    bool   hasBurst{false};
    uint32_t maxBurstCycles{0};

    // DigitalPattern
    uint32_t patternOutputs{0};                // number of digital lines
    uint32_t maxPatternLength{0};              // bytes in one pattern
    uint32_t minRpm{0}, maxRpm{0};
};

} // inline namespace jf
