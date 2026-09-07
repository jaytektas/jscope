#pragma once

#include "JScopeFrame.h"
#include "JScopeLimits.h"

#include <cstdint>
#include <vector>

inline namespace jf {

// THE PATTERN THE GENERATOR IS PLAYING, DRAWN AS A FRAME.
//
// The generator's output never comes back through the ADC unless somebody wires
// an output to an input, so there is nothing to acquire and nothing to show. What
// can be shown is what was SENT: the pattern is known exactly, and so is the step
// rate, so the waveform on those eight wires is fully determined.
//
// Rendering it as an ordinary JScopeFrame means it is drawn by the same trace
// renderer as a real acquisition -- same graticule, same channel colours, same
// cursors -- rather than by a second drawing routine that would drift away from
// the first. It is the instrument's own picture of its own output.
//
// It is NOT a measurement, and nothing here should let it be mistaken for one:
// the frame is marked untriggered, and its counts are a clean 0/1 with no noise,
// because this is the commanded signal rather than an observed one.
class JGeneratorSignal {
public:
    // Levels chosen so the trace sits inside the graticule at 1 V/div with the
    // logic-low line on a division boundary rather than off the bottom.
    static constexpr float kLowVolts  = 0.0f;
    static constexpr float kHighVolts = 5.0f;   // the outputs are 5 V logic

    // `stepsPerScreen` samples are drawn per pattern step, so an edge lands on a
    // sample boundary and the squares stay square however few steps there are.
    void build(const std::vector<uint8_t>& pattern, uint8_t outputs,
               double revolutionSeconds, uint32_t samplesPerStep = 8);

    const JScopeFrame& frame() const { return m_frame; }
    bool valid() const { return m_valid; }

private:
    JScopeFrame m_frame;
    bool        m_valid{false};
};

} // inline namespace jf
