#pragma once

#include "JScopeTheme.h"

#include <j/core/JWidget.h>
#include <j/core/Signal.h>

#include <cstdint>
#include <vector>

inline namespace jf {

// THE GENERATOR'S PATTERN, AS SOMETHING YOU CAN EDIT.
//
// The first version of this drew the pattern through the acquisition trace
// renderer, which produced a picture of the signal and no way to change it. That
// was the wrong tool: the OEM's generator page is not a display, it is an EDITOR
// -- a grid of pulse columns across eight channel lanes, where clicking a cell
// flips that channel's level for that pulse. A crank wheel with a gap is made by
// clicking the gap in.
//
// THE HORIZONTAL AXIS IS AN ENGINE CYCLE, 0 to 720 DEGREES -- two crank
// revolutions, one full four-stroke cycle -- which is why the pattern is authored
// against it rather than against time. A cam event at 450 degrees is placed at
// 450 degrees; what that means in milliseconds is a consequence of the speed and
// is not the thing being drawn.
//
// The pulse count is ONE NUMBER FOR ALL EIGHT CHANNELS: a column is a column
// everywhere, so an event on one line lines up with an event on another. That is
// what makes the grid a cycle diagram rather than eight unrelated waveforms.
class JPulseGridEditor : public JWidget {
public:
    static constexpr double kCycleDegrees = 720.0;

    explicit JPulseGridEditor(JSceneGraph& graph);

    // One byte per pulse column, bit i being channel i's level there -- the
    // device's own format, so nothing is translated on the way to the wire.
    void setPattern(const std::vector<uint8_t>& pattern, uint8_t channels);
    const std::vector<uint8_t>& pattern() const { return m_pattern; }

    // Emitted when a cell is flipped, with the whole edited pattern.
    JSignal<std::vector<uint8_t>> onPatternEdited;

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;
    void handleMousePress(float mx, float my) override;
    void handleMouseMove(float mx, float my) override;

private:
    JRect _gridRect() const;
    int   _columnAt(float mx) const;   // -1 outside
    int   _laneAt(float my) const;     // -1 outside

    std::vector<uint8_t> m_pattern;
    uint8_t              m_channels{8};
    int                  m_hoverColumn{-1};
    int                  m_hoverLane{-1};
};

} // inline namespace jf
