#pragma once

#include "JScopeCoupling.h"
#include <cstdint>

// One channel's requested settings. Requests, not readings: a driver quantises
// each field to a step the hardware actually supports and the UI re-reads
// through JScopeDriver::channelConfig() afterwards, so a knob can never end up
// silently disagreeing with the instrument.

inline namespace jf {

struct JScopeChannelConfig {
    bool           enabled{true};
    double         voltsPerDiv{1.0};
    double         offsetVolts{0.0};     // vertical position, about screen centre
    JScopeCoupling coupling{JScopeCoupling::DC};
    double         probeRatio{1.0};      // 1, 10, 100 …
    bool           inverted{false};
    bool           bandwidthLimited{false};
};

} // inline namespace jf
