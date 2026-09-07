// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>

// The two instruments the bench calls a "generator" are not the same thing and
// do not share a vocabulary.
//
//   AnalogDds      — the DSO2D15: waveform shape, frequency, amplitude, offset,
//                    N-cycle burst.
//   DigitalPattern — the 1008C: eight digital outputs driven from a pattern of
//                    up to 1440 bytes at a rate expressed in RPM. A crank/cam
//                    simulator. It has no amplitude and no notion of a sine.
//
// Flattening both into one interface would give each half a set of methods that
// return false, so JScopeGenerator carries only what is common and the concrete
// interfaces derive from it. capabilities().generator.kind picks the panel.

inline namespace jf {

enum class JScopeGeneratorKind : uint8_t { None, AnalogDds, DigitalPattern };

} // inline namespace jf
