// close() must JOIN the acquisition thread before it destroys anything that
// thread is using, and before the std::thread member itself is destroyed — a
// thread still joinable when its owner dies calls std::terminate outright.
//
// That is what switching instrument in the Device menu did: JScopeSession::close
// reset the driver, ~JDso2d15Driver ran, and std::thread::~thread terminated the
// process with "terminate called without an active exception". The 1008C and the
// synthetic source both called stop() from close(); the DSO2D15 did not.
//
// Driven through the synthetic source, so this needs no instrument. The two USB
// drivers cannot be built without one, and hardware belongs in probes/ — but the
// invariant is the same for all three, and the shape of the failure (a running
// driver closed out from under its thread) is exactly reproduced here.

#include "scope/JScopeDriverRegistry.h"
#include "scope/JScopeSession.h"
#include "support/JTestReport.h"

#include <chrono>
#include <thread>

using namespace jf;

int main() {
    JTestReport r("driver_close");

    JScopeDeviceInfo dev;
    for (const auto& d : JScopeDriverRegistry::instance().enumerateAll())
        if (d.driverId == "synthetic") { dev = d; break; }
    r.check(!dev.driverId.empty(), "the synthetic source is registered");
    if (dev.driverId.empty()) return r.result();

    // Closing a driver that was never started joins trivially and proves
    // nothing. It has to be RUNNING.
    JScopeSession session;
    r.check(session.open(dev), "the synthetic source opens");

    JScopeDriver* d = session.driver();
    r.check(d != nullptr, "the session hands back a driver");
    if (!d) return r.result();

    r.check(d->start(JScopeAcquisitionMode::Windowed), "acquisition starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    r.check(d->state() != JScopeState::Idle, "the acquisition thread is actually running");

    // The failure was not a bad value or an assertion — it ended the process.
    // Reaching the next line at all is what is being asserted.
    session.close();
    r.check(true, "close() on a RUNNING driver joined its thread instead of terminating");

    // And again from the destructor path, which is how the Device menu reached
    // it: openAsync -> _createDriver -> close -> unique_ptr::reset -> ~Driver.
    {
        JScopeSession second;
        second.open(dev);
        if (JScopeDriver* s = second.driver()) s->start(JScopeAcquisitionMode::Windowed);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    r.check(true, "destroying a session with a running driver joined its thread");

    return r.result();
}
