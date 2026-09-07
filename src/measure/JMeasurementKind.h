// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>

// The automatic measurements the app computes. Computed APP-SIDE from the
// samples, never asked of the instrument: the 1008C cannot measure anything, the
// DSO2D15's measurement set is its own, and a reading that changes meaning when
// you change scope is worse than useless on a bench with two of them.

inline namespace jf {

enum class JMeasurementKind : uint8_t {
    Vpp, Vmax, Vmin, Vavg, Vrms,
    Top, Base, Amplitude,
    Frequency, Period, DutyCycle,
    RiseTime, FallTime,
    Count_
};

inline const char* jMeasurementKindName(JMeasurementKind k) {
    switch (k) {
        case JMeasurementKind::Vpp:       return "Vpp";
        case JMeasurementKind::Vmax:      return "Vmax";
        case JMeasurementKind::Vmin:      return "Vmin";
        case JMeasurementKind::Vavg:      return "Vavg";
        case JMeasurementKind::Vrms:      return "Vrms";
        case JMeasurementKind::Top:       return "Top";
        case JMeasurementKind::Base:      return "Base";
        case JMeasurementKind::Amplitude: return "Ampl";
        case JMeasurementKind::Frequency: return "Freq";
        case JMeasurementKind::Period:    return "Period";
        case JMeasurementKind::DutyCycle: return "Duty";
        case JMeasurementKind::RiseTime:  return "Rise";
        case JMeasurementKind::FallTime:  return "Fall";
        default:                          return "?";
    }
}

// The unit a kind is expressed in, so a readout formats without a lookup table
// of its own.
enum class JMeasurementUnit : uint8_t { Volts, Seconds, Hertz, Percent };

inline JMeasurementUnit jMeasurementUnit(JMeasurementKind k) {
    switch (k) {
        case JMeasurementKind::Frequency: return JMeasurementUnit::Hertz;
        case JMeasurementKind::Period:
        case JMeasurementKind::RiseTime:
        case JMeasurementKind::FallTime:  return JMeasurementUnit::Seconds;
        case JMeasurementKind::DutyCycle: return JMeasurementUnit::Percent;
        default:                          return JMeasurementUnit::Volts;
    }
}

} // inline namespace jf
