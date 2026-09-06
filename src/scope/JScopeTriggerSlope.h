#pragma once

#include <cstdint>

inline namespace jf {

enum class JScopeTriggerSlope : uint8_t { Rising, Falling, Either };

inline const char* jScopeTriggerSlopeName(JScopeTriggerSlope s) {
    switch (s) {
        case JScopeTriggerSlope::Rising:  return "Rising";
        case JScopeTriggerSlope::Falling: return "Falling";
        case JScopeTriggerSlope::Either:  return "Either";
    }
    return "Rising";
}

} // inline namespace jf
