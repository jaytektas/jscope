#include "scope/JScopeSession.h"
#include "scope/JScopeDriverRegistry.h"
#include "support/JTestReport.h"

#include <j/core/MainThreadDispatcher.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace jf;

namespace {

JScopeDeviceInfo syntheticDevice() {
    for (const auto& d : JScopeDriverRegistry::instance().enumerateAll())
        if (d.driverId == "synthetic") return d;
    return {};
}

// Pump the main-thread queue the way the render loop does, with a deadline so a
// callback that never arrives fails the test instead of hanging it.
bool pumpUntil(const std::function<bool()>& done, int milliseconds) {
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        JMainThreadDispatcher::instance().drain();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    JMainThreadDispatcher::instance().drain();
    return done();
}

// OPENING A DEVICE MUST NOT BLOCK THE CALLER. Bringing up the 1008C takes about
// twenty seconds — three vertical ranges of 500 averaged samples with protocol
// delays between transfers — and doing that inline froze the window for the whole
// of it. Choosing the instrument from the Device menu was indistinguishable from
// a crash, which is what this exists to stop coming back.
void testOpenAsyncDoesNotBlock(JTestReport& r) {
    JScopeSession session;
    const JScopeDeviceInfo dev = syntheticDevice();
    r.check(!dev.driverId.empty(), "the synthetic source is registered");
    if (dev.driverId.empty()) return;

    bool called = false, opened = false;
    std::string connecting;
    session.onConnecting.connect([&](std::string name) { connecting = std::move(name); });

    session.openAsync(dev, [&](bool ok) { called = true; opened = ok; });

    // The callback must NOT have run yet: it is delivered through the main-thread
    // queue, and nothing has pumped it.
    r.check(!called, "openAsync returns before the device is open");
    r.check(!connecting.empty(), "and says what it is connecting to");

    r.check(pumpUntil([&] { return called; }, 5000), "the result arrives on the main thread");
    r.check(opened, "and the device opened");
    r.check(session.isOpen(), "the session is open afterwards");
    r.check(!session.isConnecting(), "and no longer reports a connect in flight");
}

// A second request while one is in flight must be refused rather than queued: two
// bring-ups racing for the same USB interface is how the device ends up wedged.
void testSecondConnectIsRefused(JTestReport& r) {
    JScopeSession session;
    const JScopeDeviceInfo dev = syntheticDevice();
    if (dev.driverId.empty()) return;

    bool firstDone = false;
    int  refusals  = 0;
    session.openAsync(dev, [&](bool) { firstDone = true; });
    session.openAsync(dev, [&](bool ok) { if (!ok) ++refusals; });

    r.check(refusals == 1, "a second connect is refused while one is in flight");
    r.check(pumpUntil([&] { return firstDone; }, 5000), "the first still completes");
}

// SIGNALS MUST ONLY EVER ARRIVE ON THE MAIN THREAD, including during a connect.
//
// A driver's open() emits — JSyntheticDriver::open ends in _setState, which
// fires onStateChanged synchronously. That was safe while open() ran on the main
// thread. Once the connect moved to a worker, wiring the driver's signals BEFORE
// handing it over delivered that emission to the widgets from the worker, and
// glibc aborted the process with
//
//   pthread_mutex_lock.c:94: assertion failed: mutex->__data.__owner == 0
//
// the first time a device was chosen from the Device menu. This is the check
// that fails if the signals are ever connected before the open again.
void testSignalsArriveOnTheMainThread(JTestReport& r) {
    JScopeSession session;
    const JScopeDeviceInfo dev = syntheticDevice();
    if (dev.driverId.empty()) return;

    const std::thread::id main = std::this_thread::get_id();
    std::atomic<bool> wrongThread{false};
    std::atomic<int>  states{0};

    session.onStateChanged.connect([&](JScopeState) {
        if (std::this_thread::get_id() != main) wrongThread.store(true);
        ++states;
    });
    session.onError.connect([&](std::string) {
        if (std::this_thread::get_id() != main) wrongThread.store(true);
    });

    bool done = false;
    session.openAsync(dev, [&](bool) { done = true; });
    r.check(pumpUntil([&] { return done; }, 5000), "the connect completes");
    r.check(!wrongThread.load(), "no session signal arrived off the main thread");

    // And the driver is still wired up afterwards, so late-connecting has not
    // simply thrown the signals away.
    if (JScopeDriver* d = session.driver()) {
        const int before = states.load();
        d->start(JScopeAcquisitionMode::Streaming);
        JMainThreadDispatcher::instance().drain();
        d->stop();
        JMainThreadDispatcher::instance().drain();
        r.check(states.load() > before, "state changes still reach the session after opening");
        r.check(!wrongThread.load(), "and still on the main thread");
    }
}

} // namespace

// CLOSING WHILE THE DEVICE WORKER IS STILL IN THE DRIVER MUST NOT FREE IT
// UNDER HIM. openAsync hands the worker a bare pointer to m_driver, and close()
// used to reset it without waiting — so shutting the window during a slow open
// freed the transport the worker was mid-write on. It faulted in
// JUsbTmcSession::write, and a wedged DSO2D15 made it easy to hit because that
// open sits in two five-second bulk timeouts.
//
// runOnDevice puts work on the SAME worker, so a task that is still running
// when close() arrives reproduces the ordering without needing a real
// instrument that is slow to open.
void testCloseWaitsForTheDeviceWorker(JTestReport& r) {
    JScopeSession session;
    const JScopeDeviceInfo dev = syntheticDevice();
    if (dev.driverId.empty()) return;

    r.check(session.open(dev), "the synthetic source opens");

    std::atomic<bool> entered{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> continuationRan{false};

    session.runOnDevice(
        [&] {
            entered.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            finished.store(true);
        },
        [&] { continuationRan.store(true); });

    // Only close once the worker is genuinely inside the task; closing before it
    // started would drain an empty queue and prove nothing.
    while (!entered.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    session.close();

    r.check(finished.load(),
            "close() waited for the in-flight device task instead of freeing under it");
    r.check(!session.isBusy(), "the busy flags are cleared by close()");

    // The worker's completion was posted to the main thread before close(); it
    // must not run against the session it no longer belongs to.
    JMainThreadDispatcher::instance().drain();
    r.check(!continuationRan.load(),
            "a continuation queued before close() does not run afterwards");
}

int main() {
    JTestReport r("JScopeSession asynchronous connect");
    testOpenAsyncDoesNotBlock(r);
    testSecondConnectIsRefused(r);
    testSignalsArriveOnTheMainThread(r);
    testCloseWaitsForTheDeviceWorker(r);
    return r.result();
}
