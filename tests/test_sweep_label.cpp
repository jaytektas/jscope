// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "scope/JScopeSweepLabel.h"
#include "support/JTestReport.h"

using namespace jf;

namespace {

// The words written across the graticule. This is the readout a user looks at to
// answer "is it running?", so it being stale or wrong is worse than it being
// absent -- an idle scope that says "Auto" is actively misleading about whether
// what is on screen is live.
void testStoppedIsNotASweepMode(JTestReport& r) {
    // The bug: Stop is a RUN STATE, and the legend was showing the trigger sweep
    // mode, which does not change when acquisition halts. So stopping an
    // Auto-swept scope left "Auto" on the screen.
    r.check(jScopeSweepLabel(JScopeState::Stopped, "", JScopeTriggerMode::Auto) == "Stopped",
            "a stopped scope says Stopped, not the sweep mode it would have used");
    r.check(jScopeSweepLabel(JScopeState::Stopped, "", JScopeTriggerMode::Normal) == "Stopped",
            "and the same whatever the sweep mode was");

    r.check(jScopeSweepLabel(JScopeState::Idle, "", JScopeTriggerMode::Auto) == "Idle",
            "an open but unswept scope says Idle");
    r.check(jScopeSweepLabel(JScopeState::Error, "", JScopeTriggerMode::Auto) == "Error",
            "an errored one says so rather than describing a sweep");
    r.check(jScopeSweepLabel(JScopeState::Closed, "", JScopeTriggerMode::Auto).empty(),
            "with no device there is nothing to report");
}

void testSweepingShowsTheMode(JTestReport& r) {
    // While it IS sweeping, the mode is the useful thing to say.
    r.check(jScopeSweepLabel(JScopeState::Running, "", JScopeTriggerMode::Auto) ==
                jScopeTriggerModeName(JScopeTriggerMode::Auto),
            "a running scope names its sweep mode");
    r.check(jScopeSweepLabel(JScopeState::Armed, "", JScopeTriggerMode::Normal) ==
                jScopeTriggerModeName(JScopeTriggerMode::Normal),
            "and so does an armed one");
}

void testInstrumentWordWins(JTestReport& r) {
    // A bench scope knows whether it is armed or triggered. That is its answer
    // about itself; the mode is only what we asked it for.
    r.check(jScopeSweepLabel(JScopeState::Triggered, "TRIG'D", JScopeTriggerMode::Auto) == "TRIG'D",
            "the instrument's own status beats the mode we requested");

    // But only while sweeping: a device that reports a stale status word must not
    // be able to claim it is triggered after it has been stopped.
    r.check(jScopeSweepLabel(JScopeState::Stopped, "TRIG'D", JScopeTriggerMode::Auto) == "Stopped",
            "a stale instrument status cannot override a stopped scope");
}

// Pressing Single takes one shot whatever the trigger mode is left on, so that is
// what the screen has to say while it happens. It used to announce the mode --
// "Auto" for the single frame it took -- which describes the policy that would
// have applied rather than what the instrument is doing.
void testSingleShotOverridesTheMode(JTestReport& r) {
    r.check(jScopeSweepLabel(JScopeState::Armed, "", JScopeTriggerMode::Auto,
                             /*singleShotPending=*/true) ==
                jScopeTriggerModeName(JScopeTriggerMode::Single),
            "a single shot armed on Auto reads Single, not Auto");
    r.check(jScopeSweepLabel(JScopeState::Triggered, "", JScopeTriggerMode::Normal, true) ==
                jScopeTriggerModeName(JScopeTriggerMode::Single),
            "and the same once it has triggered");

    // It beats even the instrument's own word, because a bench scope reporting
    // "TRIG'D" is not saying whether this sweep was the last one.
    r.check(jScopeSweepLabel(JScopeState::Triggered, "TRIG'D", JScopeTriggerMode::Auto, true) ==
                jScopeTriggerModeName(JScopeTriggerMode::Single),
            "a requested single shot outranks the instrument's status word");

    // And it says nothing once the shot is over: the run state leads.
    r.check(jScopeSweepLabel(JScopeState::Stopped, "", JScopeTriggerMode::Auto, true) == "Stopped",
            "when the shot has finished the scope reads Stopped");

    // Without the flag nothing changes.
    r.check(jScopeSweepLabel(JScopeState::Running, "", JScopeTriggerMode::Auto, false) ==
                jScopeTriggerModeName(JScopeTriggerMode::Auto),
            "a free-running sweep still names its mode");
}

} // namespace

int main() {
    JTestReport r("sweep label");
    testStoppedIsNotASweepMode(r);
    testSweepingShowsTheMode(r);
    testInstrumentWordWins(r);
    testSingleShotOverridesTheMode(r);
    return r.result();
}
