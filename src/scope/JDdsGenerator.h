// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeGenerator.h"

#include <cstdint>
#include <string>
#include <vector>

inline namespace jf {

// An analog DDS generator: a waveform shape at a frequency and amplitude.
//
// Separate from JPatternGenerator because the two instruments' generators are
// not the same kind of thing and share no vocabulary — see JScopeGeneratorKind.
// A DDS has amplitude and a sine; the 1008C's eight-line crank simulator has
// neither.
//
// Every setter returns what the instrument ACCEPTED, not what was asked for.
// A generator quantises frequency and amplitude to its own steps, and a panel
// that displayed the request would show a value the hardware is not producing.
class JDdsGenerator : public JScopeGenerator {
public:
    JScopeGeneratorKind kind() const override { return JScopeGeneratorKind::AnalogDds; }

    virtual bool   setWaveform(const std::string& name) = 0;
    virtual std::string waveform() const = 0;

    virtual double setFrequency(double hz) = 0;
    virtual double frequency() const = 0;

    virtual double setAmplitude(double vpp) = 0;
    virtual double amplitude() const = 0;

    virtual double setOffset(double volts) = 0;
    virtual double offset() const = 0;

    // Burst: a finite number of cycles, fired on demand rather than free-running.
    virtual bool     setBurstEnabled(bool on) = 0;
    virtual bool     burstEnabled() const = 0;
    virtual uint32_t setBurstCycles(uint32_t cycles) = 0;
    virtual uint32_t burstCycles() const = 0;

    // Fire one burst. This is the whole reason the burst mode is worth having
    // over the front panel: the panel can only trigger a burst by hand, so
    // nothing on the bench can be synchronised to it.
    virtual bool triggerBurst() = 0;

    // Read every setting back from the instrument, in case it was changed at the
    // front panel while this application was looking away.
    virtual bool refresh() = 0;
};

} // inline namespace jf
