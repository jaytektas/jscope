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

} // namespace

int main() {
    JTestReport r("sweep label");
    testStoppedIsNotASweepMode(r);
    testSweepingShowsTheMode(r);
    testInstrumentWordWins(r);
    return r.result();
}
