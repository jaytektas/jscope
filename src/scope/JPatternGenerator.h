#pragma once

#include "JScopeGenerator.h"

#include <cstdint>
#include <vector>

inline namespace jf {

// EIGHT DIGITAL OUTPUTS PLAYING A PATTERN, on repeat, at a speed given in RPM.
//
// This is not a function generator with a square-wave setting. There is no
// amplitude, no offset, no phase and no sine: the outputs are switches, and the
// only thing under control is which of them are closed at each step and how fast
// the steps go by. It exists to simulate a crank or cam sensor -- a toothed
// wheel with a gap -- which is why one pass of the pattern is one REVOLUTION and
// why the natural unit is engine speed rather than frequency.
//
// The pattern is one byte per step, bit i driving output i. So a step is a
// snapshot of all eight lines at one instant, and the pattern read end to end is
// a picture of the whole wheel.
//
// SPEED IS QUANTISED AND THIS INTERFACE SAYS SO. The device advances one step
// per whole tick of a fixed clock, so most requested speeds are not exactly
// reachable; the achievable one falls out of an integer division. Asking for a
// speed and reading back the same number would be a lie, so requestedRpm() and
// actualRpm() are separate -- the OEM shows both, labelled "Set Speed" and
// "Real Speed", for the same reason.
class JPatternGenerator : public JScopeGenerator {
public:
    JScopeGeneratorKind kind() const final { return JScopeGeneratorKind::DigitalPattern; }

    // One byte per step. Rejected when empty, or longer than
    // capabilities().maxPatternLength.
    virtual bool setPattern(const std::vector<uint8_t>& pattern) = 0;
    virtual const std::vector<uint8_t>& pattern() const = 0;

    // What was asked for, and what the hardware can actually do with it. The
    // second changes when the PATTERN changes too, because the step rate depends
    // on how many steps make up a revolution.
    virtual bool     setRpm(uint32_t rpm) = 0;
    virtual uint32_t requestedRpm() const = 0;
    virtual uint32_t actualRpm() const = 0;

    // What actualRpm() would be for this request, without asking the device to do
    // it. The UI needs to show the consequence of a speed before it is committed.
    virtual uint32_t achievableRpm(uint32_t rpm) const = 0;
};

} // inline namespace jf
