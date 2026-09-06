#pragma once

#include <cstdint>

inline namespace jf {

enum class JScopeState : uint8_t {
    Closed,     // no device
    Idle,       // open, not acquiring
    Armed,      // acquiring, waiting for a trigger
    Triggered,  // trigger seen, reading the record out
    Running,    // continuous acquisition (Streaming, or Windowed in Auto)
    Stopped,    // acquisition halted; the last frame stands
    Error,      // unrecoverable; see the driver's onError
};

inline const char* jScopeStateName(JScopeState s) {
    switch (s) {
        case JScopeState::Closed:    return "Closed";
        case JScopeState::Idle:      return "Idle";
        case JScopeState::Armed:     return "Armed";
        case JScopeState::Triggered: return "Triggered";
        case JScopeState::Running:   return "Running";
        case JScopeState::Stopped:   return "Stopped";
        case JScopeState::Error:     return "Error";
    }
    return "Closed";
}

} // inline namespace jf
