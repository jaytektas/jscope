// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeState.h"
#include "JScopeTriggerMode.h"

#include <string>

inline namespace jf {

// WHAT THE SCOPE IS DOING, in the words that belong on top of the waveform.
//
// The legend used to lead with the trigger sweep mode, so a stopped instrument
// announced "Auto" -- naming the policy it would use for triggers it is not
// waiting for. Stop is not a kind of sweep, and the readout sitting over the
// trace is the one place that has to say what is actually happening.
//
// So the run state leads, and the sweep mode only describes what happens WHILE
// sweeping. Where the instrument reports its own status -- a bench scope knows
// whether it is armed or triggered -- that word wins over the mode we asked for,
// because it is the device's answer rather than our request.
//
// Pure, and headless, so the rule can be tested without a window.
inline std::string jScopeSweepLabel(JScopeState state,
                                    const std::string& instrumentStatus,
                                    JScopeTriggerMode mode,
                                    bool singleShotPending = false) {
    switch (state) {
        case JScopeState::Stopped: return "Stopped";
        case JScopeState::Idle:    return "Idle";
        case JScopeState::Error:   return "Error";
        case JScopeState::Closed:  return {};        // no device: nothing to report
        case JScopeState::Armed:
        case JScopeState::Triggered:
        case JScopeState::Running:
            // A requested single shot IS the sweep in progress, whatever mode the
            // trigger is left in. Pressing Single with the mode on Auto used to
            // announce "Auto" for the one frame it took, which describes the
            // policy rather than what the instrument is doing.
            if (singleShotPending) return jScopeTriggerModeName(JScopeTriggerMode::Single);
            return instrumentStatus.empty() ? jScopeTriggerModeName(mode) : instrumentStatus;
    }
    return {};
}

} // inline namespace jf
