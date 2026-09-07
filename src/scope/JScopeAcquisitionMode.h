// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>

// The two shapes an oscilloscope acquisition can take. Both are first-class:
// forcing one into the other is what makes a multi-device HAL leak.
//
//   Windowed  — a triggered record of N samples per channel at a known
//               seconds/div, with a trigger position inside it. The DSO2D15's
//               normal mode; the 1008C's burst mode.
//   Streaming — a continuous roll at a chosen sample rate, delivered as
//               contiguous chunks carrying a monotonic start index, with no
//               trigger at all. The 1008C's roll mode.
//
// Expressing roll as a "screen" would mean inventing a fake trigger and a fake
// record length; expressing a screen as a stream would throw away the trigger
// position and everything before it. So neither is expressed in terms of the
// other. A driver advertises which it supports in
// JScopeCapabilities::acquisitionModes.

inline namespace jf {

enum class JScopeAcquisitionMode : uint8_t { Windowed = 0, Streaming = 1 };

inline constexpr uint32_t jScopeAcquisitionModeBit(JScopeAcquisitionMode m) {
    return 1u << static_cast<uint32_t>(m);
}

inline constexpr bool jScopeAcquisitionModeSupported(uint32_t mask, JScopeAcquisitionMode m) {
    return (mask & jScopeAcquisitionModeBit(m)) != 0;
}

inline const char* jScopeAcquisitionModeName(JScopeAcquisitionMode m) {
    switch (m) {
        case JScopeAcquisitionMode::Windowed:  return "Windowed";
        case JScopeAcquisitionMode::Streaming: return "Streaming";
    }
    return "Windowed";
}

} // inline namespace jf
