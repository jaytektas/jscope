// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>

// The signals this application can draw a REFERENCE trace for.
//
// A reference is the shape a healthy device produces, drawn behind the live
// trace so what is on the bench can be compared against what it is supposed to
// look like. That comparison is most of automotive diagnosis: a crank sensor
// that reads 1.2 V peak is meaningless on its own and obvious the moment it is
// held against a waveform that should be symmetric and is not.
//
// MODELLED, NOT RECORDED. Each of these is generated from what the device
// physically does — a 60-2 trigger wheel gives one cycle per tooth and nothing
// across the gap, a coil sits at battery until it is pulled down to dwell, an
// injector's current climbs on an L/R curve and notches where the pintle lifts.
// None of it is a capture, nobody's and not ours, which means the shape carries
// no particular vehicle's quirks and the parameters can be read and argued with
// in JReferenceWaveform.cpp rather than being an opaque array of samples.
//
// The list is deliberately SHORT. These are the signals whose healthy shape is
// unambiguous enough to be worth asserting; a sensor whose normal output varies
// by manufacturer has no single reference and gets none, which is honest.

inline namespace jf {

enum class JReferenceSignal : uint8_t {
    None = 0,
    CrankInductive,     // variable-reluctance sensor on a 60-2 trigger wheel
    CrankHall,          // Hall switch, open collector pulled to 5 V
    PrimaryIgnition,    // coil primary: dwell, spike, spark line, ringing
    InjectorCurrent,    // saturated driver: L/R rise, pintle notch, hold
    LambdaZirconia,     // Nernst cell switching either side of stoichiometric
    AirFlowHotWire,     // hot-wire MAF through a snap-throttle test
    ThrottlePosition,   // potentiometer swept open and closed
};

// Every value above, in menu order, so a caller cannot iterate a stale list.
inline constexpr JReferenceSignal kReferenceSignals[] = {
    JReferenceSignal::None,
    JReferenceSignal::CrankInductive,
    JReferenceSignal::CrankHall,
    JReferenceSignal::PrimaryIgnition,
    JReferenceSignal::InjectorCurrent,
    JReferenceSignal::LambdaZirconia,
    JReferenceSignal::AirFlowHotWire,
    JReferenceSignal::ThrottlePosition,
};

inline const char* jReferenceSignalName(JReferenceSignal s) {
    switch (s) {
        case JReferenceSignal::None:             return "None";
        case JReferenceSignal::CrankInductive:   return "Crankshaft (inductive)";
        case JReferenceSignal::CrankHall:        return "Crankshaft / camshaft (Hall)";
        case JReferenceSignal::PrimaryIgnition:  return "Primary ignition";
        case JReferenceSignal::InjectorCurrent:  return "Injector current";
        case JReferenceSignal::LambdaZirconia:   return "Lambda (zirconia)";
        case JReferenceSignal::AirFlowHotWire:   return "Air flow meter (hot wire)";
        case JReferenceSignal::ThrottlePosition: return "Throttle position";
    }
    return "None";
}

} // inline namespace jf
