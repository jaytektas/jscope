#pragma once

#include "JScopeLimits.h"
#include <cstdint>

// The part of a scope setup that lives in the DISPLAY rather than the
// instrument: where each trace has been dragged to, how far the record is zoomed
// and panned, and where the cursors sit.
//
// A plain struct rather than a call into the view, so JScopeSettings can persist
// it without depending on the widget that owns it. That split is not decoration:
// the settings layer is headless precisely so test_actions can exercise a full
// save/restore cycle with no window and no GPU.

inline namespace jf {

struct JScopeViewState {
    // Where each channel's 0 V has been dragged to, in volts. Not the device's
    // own offset — that belongs to the channel config and is stored with it.
    double positionVolts[JScopeLimits::kMaxChannels]{};

    // NOT the view window. That is derived from seconds/div, which is itself
    // persisted with the timebase — so the window survives a restart through the
    // setting that defines it. Storing it separately as a fraction of the record
    // meant a restored zoom silently contradicted the s/div readout and the
    // graticule, which is the one thing the timebase work was meant to stop.

    bool    cursorXEnabled{false};
    bool    cursorYEnabled{false};
    double  cursorX1{0.0};
    double  cursorX2{0.0};
    double  cursorY1{0.0};
    double  cursorY2{0.0};
    uint8_t cursorYChannel{0};
};

} // inline namespace jf
