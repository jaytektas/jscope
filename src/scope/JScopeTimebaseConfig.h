// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeAcquisitionMode.h"
#include <cstdint>

// The horizontal axis. Which fields matter depends on the acquisition mode:
// Windowed reads secondsPerDiv / triggerPosition / recordLength, Streaming reads
// sampleRate. A driver ignores what its mode does not use rather than failing.

inline namespace jf {

struct JScopeTimebaseConfig {
    JScopeAcquisitionMode mode{JScopeAcquisitionMode::Windowed};

    // Windowed
    double   secondsPerDiv{1.0e-3};
    double   triggerPosition{0.5};   // fraction of the record before the trigger
    uint32_t recordLength{0};        // 0 = let the device decide

    // Streaming
    double   sampleRate{440.0};      // samples per second per channel
};

} // inline namespace jf
