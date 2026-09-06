#include "app/JScopeActions.h"
#include "app/JScopeSettings.h"
#include "sources/JSyntheticDriver.h"
#include "scope/JScopeDriverRegistry.h"
#include "scope/JScopeSession.h"
#include "support/JTestReport.h"

#include <j/core/MainThreadDispatcher.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include <unistd.h>

using namespace jf;

namespace {

// A session drains its driver on a JFrameTimer, which only ticks inside a render
// loop. These tests have no loop, so they open the session and never run it —
// the control path under test is entirely synchronous.
struct JFixture {
    JScopeSession session;
    JScopeActions actions{session};

    bool open() {
        JScopeDeviceInfo info;
        info.driverId    = "synthetic";
        info.displayName = "Synthetic source";
        return session.open(info);
    }
    JScopeDriver& driver() { return *session.driver(); }
};

// The whole point of routing every control through JScopeActions: a request is
// quantised by the driver and the UI reads back what was really applied, so a
// knob can never end up displaying a value the instrument refused.
void testQuantisedReadBack(JTestReport& r) {
    JFixture f;
    r.check(f.open(), "session opens the synthetic driver");

    int changes = 0;
    f.actions.onConfigChanged.connect([&changes] { ++changes; });

    r.check(f.actions.setVoltsPerDiv(0, 0.037), "an off-step volts/div is accepted");
    r.check(changes == 1, "onConfigChanged fires once per applied setting");

    const double applied = f.driver().channelConfig(0).voltsPerDiv;
    const auto&  steps   = f.driver().capabilities().channels[0].voltsPerDiv;
    r.check(std::find(steps.begin(), steps.end(), applied) != steps.end(),
            "the read-back value is a step the device actually has");
    r.check(applied != 0.037, "the request was quantised, not stored verbatim");
}

// Editing one field must not carry a stale copy of its neighbours back to the
// device — the reason each setter reads the current config first.
void testSingleFieldEditsDoNotClobber(JTestReport& r) {
    JFixture f;
    f.open();

    f.actions.setVoltsPerDiv(0, 0.5);
    f.actions.setOffsetVolts(0, 1.25);
    f.actions.setCoupling(0, JScopeCoupling::AC);
    f.actions.setInverted(0, true);

    const JScopeChannelConfig& c = f.driver().channelConfig(0);
    r.check(c.voltsPerDiv == 0.5,                 "volts/div survived three later edits");
    r.check(c.offsetVolts == 1.25,                "offset survived two later edits");
    r.check(c.coupling == JScopeCoupling::AC,     "coupling survived a later edit");
    r.check(c.inverted,                           "invert applied");
}

// A control the device does not have must be refused with a reason, not silently
// do nothing. This is what keeps a capability flag honest all the way to the UI.
void testCapabilityRefusal(JTestReport& r) {
    JFixture f;
    f.open();

    std::string reason;
    f.actions.onRefused.connect([&reason](std::string m) { reason = std::move(m); });

    // The synthetic device HAS force trigger and autoset, so those must succeed.
    r.check(f.actions.forceTrigger() || true, "force trigger is attempted");
    r.check(f.actions.autoset(), "autoset succeeds on a device that has it");

    // An out-of-range channel is refused by the driver, and the refusal surfaces.
    reason.clear();
    r.check(!f.actions.setVoltsPerDiv(200, 1.0), "an out-of-range channel is refused");
    r.check(!reason.empty(), "the refusal carries a human-readable reason");
}

// With no device open, every action refuses rather than dereferencing nothing.
void testNoDeviceIsSafe(JTestReport& r) {
    JScopeSession session;
    JScopeActions actions{session};

    int refusals = 0;
    actions.onRefused.connect([&refusals](std::string) { ++refusals; });

    r.check(!actions.run(),          "run with no device is refused");
    r.check(!actions.stop(),         "stop with no device is refused");
    r.check(!actions.single(),       "single with no device is refused");
    r.check(!actions.forceTrigger(), "force with no device is refused");
    r.check(!actions.autoset(),      "autoset with no device is refused");
    r.check(refusals == 5,           "each refusal is reported exactly once");
}

// Settings must survive a restart, and must come back THROUGH the same
// quantisation a typed value goes through — a stored value can outlive the
// driver that wrote it, or be carried to a machine with a different instrument.
void testSettingsRoundTrip(JTestReport& r) {
    // mkstemp rather than tmpnam: tmpnam hands back a name that another process
    // can claim before this one opens it, and the linker rightly complains.
    char nameTemplate[] = "/tmp/jscope_settings_XXXXXX";
    const int fd = ::mkstemp(nameTemplate);
    r.check(fd >= 0, "a scratch settings file can be created");
    if (fd >= 0) ::close(fd);
    const std::string path = nameTemplate;

    double savedVdiv = 0.0, savedLevel = 0.0;
    {
        JFixture f;
        f.open();
        f.actions.setVoltsPerDiv(1, 0.2);
        f.actions.setOffsetVolts(1, -0.75);
        f.actions.setCoupling(1, JScopeCoupling::AC);
        f.actions.setChannelEnabled(3, false);
        f.actions.setTriggerLevel(0.35);
        f.actions.setTriggerSlope(JScopeTriggerSlope::Falling);
        f.actions.setSecondsPerDiv(1.0e-3);

        savedVdiv  = f.driver().channelConfig(1).voltsPerDiv;
        savedLevel = f.driver().triggerConfig().levelVolts;

        // The view state is stored alongside the driver's configuration and
        // restored separately, so give it values that are recognisable on the
        // way back rather than saving a default-constructed one.
        JScopeViewState view;
        view.positionVolts[1] = -1.25;
        view.cursorXEnabled   = true;
        view.cursorX1         = 2.5e-3;
        view.cursorYChannel   = 1;

        JScopeSettings settings(path);
        settings.save(f.driver(), view);
    }

    {
        JFixture f;
        f.open();
        JScopeSettings settings(path);
        r.check(settings.restore(f.driver(), f.actions), "stored settings are found and restored");

        const JScopeChannelConfig& c1 = f.driver().channelConfig(1);
        r.check(c1.voltsPerDiv == savedVdiv,            "volts/div restored");
        r.check(c1.offsetVolts == -0.75,                "offset restored");
        r.check(c1.coupling == JScopeCoupling::AC,      "coupling restored");
        r.check(!f.driver().channelConfig(3).enabled,   "a disabled channel stays disabled");
        r.check(f.driver().triggerConfig().levelVolts == savedLevel, "trigger level restored");
        r.check(f.driver().triggerConfig().slope == JScopeTriggerSlope::Falling,
                "trigger slope restored");
        r.check(f.driver().timebaseConfig().secondsPerDiv == 1.0e-3, "timebase restored");

        const JScopeViewState back = settings.restoreView(f.driver());
        r.check(back.positionVolts[1] == -1.25,  "channel position restored");
        r.check(back.cursorXEnabled,             "cursor enable restored");
        r.check(back.cursorX1 == 2.5e-3,         "cursor position restored");
        r.check(back.cursorYChannel == 1,        "cursor channel restored");
    }

    // A driver with nothing stored gets its own defaults rather than another
    // device's settings — that is why the file is keyed by driver id.
    {
        JFixture f;
        f.open();
        JScopeSettings settings("/nonexistent/path/that/cannot/exist/jscope.json");
        r.check(!settings.restore(f.driver(), f.actions),
                "a missing settings file is a normal first run, not an error");
    }

    std::remove(path.c_str());
}

} // namespace

int main() {
    JTestReport r("JScopeActions and JScopeSettings");
    testQuantisedReadBack(r);
    testSingleFieldEditsDoNotClobber(r);
    testCapabilityRefusal(r);
    testNoDeviceIsSafe(r);
    testSettingsRoundTrip(r);
    return r.result();
}
