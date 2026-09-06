#pragma once

#include <cstdint>

// Sweep mode. Where the hardware has no notion of one — the Hantek 1008C's
// protocol does not — the driver implements all three as host-side policy around
// its ready-poll and advertises them honestly. The distinction the UI cares
// about is only ever "what happens when no trigger arrives".

inline namespace jf {

enum class JScopeTriggerMode : uint8_t {
    Auto   = 0,   // sweep anyway after a timeout, so the screen is never blank
    Normal = 1,   // wait indefinitely for a trigger
    Single = 2,   // one frame, then stop
};

// Bitmask helpers for JScopeCapabilities::triggerModes.
inline constexpr uint32_t jScopeTriggerModeBit(JScopeTriggerMode m) {
    return 1u << static_cast<uint32_t>(m);
}

inline constexpr bool jScopeTriggerModeSupported(uint32_t mask, JScopeTriggerMode m) {
    return (mask & jScopeTriggerModeBit(m)) != 0;
}

inline const char* jScopeTriggerModeName(JScopeTriggerMode m) {
    switch (m) {
        case JScopeTriggerMode::Auto:   return "Auto";
        case JScopeTriggerMode::Normal: return "Normal";
        case JScopeTriggerMode::Single: return "Single";
    }
    return "Auto";
}

} // inline namespace jf
