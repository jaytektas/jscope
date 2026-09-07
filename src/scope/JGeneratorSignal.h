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
    static constexpr float kLowVolts  = 0.0f;
    static constexpr float kHighVolts = 5.0f;   // the outputs are 5 V logic

    // 5 V/div, so one output's full swing is exactly ONE DIVISION. With eight
    // divisions on the graticule and eight outputs, the lanes then fill the screen
    // exactly and none of them overlaps its neighbour -- which is how the OEM's
    // generator window presents it, and the only way eight digital lines are
    // readable at once.
    static constexpr double kVoltsPerDiv = 5.0;

    // Where channel `index`'s 0 V sits, in volts, so the channels stack top to
    // bottom in the order they are numbered -- CH1 in the top lane, like the OEM.
    //
    // The band for lane i runs from the top edge downwards, and 0 V goes at the
    // BOTTOM of its band so the trace rises into the lane rather than out of it.
    static constexpr double lanePosition(uint8_t index, uint8_t verticalDivisions) {
        const double halfSpan = (verticalDivisions / 2.0) * kVoltsPerDiv;
        return halfSpan - kVoltsPerDiv * (index + 1);
    }

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
