#include "sources/JSyntheticDriver.h"
#include "scope/JScopeDriverRegistry.h"
#include "support/JTestReport.h"

#include <chrono>
#include <thread>
#include <vector>

using namespace jf;

namespace {

uint64_t hashPlane(const JScopeFrame& f, uint8_t plane) {
    uint64_t h = 1469598103934665603ull;
    const int16_t* p = f.plane(plane);
    for (uint32_t i = 0; i < f.header.sampleCount; ++i)
        h = (h ^ static_cast<uint16_t>(p[i])) * 1099511628211ull;
    return h;
}

// Collect `want` published frames, hashing every plane of each.
std::vector<std::vector<uint64_t>> collect(JScopeDriver& d, size_t want) {
    std::vector<std::vector<uint64_t>> out;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (out.size() < want && std::chrono::steady_clock::now() < deadline) {
        JScopeFrame* f = d.pool().tryPopReady();
        if (!f) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
        std::vector<uint64_t> hashes;
        for (uint8_t p = 0; p < f->header.channelCount; ++p) hashes.push_back(hashPlane(*f, p));
        out.push_back(std::move(hashes));
        d.pool().release(f);
    }
    return out;
}

// A TRIGGERED sweep of a periodic signal must reproduce the same samples every
// time. When it does not, the trace twitches sideways on screen — and the causes
// are subtle enough that nothing else in this suite would notice:
//
//   * quantising with a cast truncates, which puts the code boundary exactly ON
//     each integer, precisely where a signal's DC level sits;
//   * a timebase step such as 1 ms has no exact binary representation, so the
//     sweep time cannot land in the same place twice, and a mathematically
//     instantaneous edge flips a sample between rails on a ~1e-13 phase change.
//
// Both produce a display that is almost right, and both are invisible to a test
// that only checks amplitudes or frequencies. This one compares whole frames.
void testTriggeredFramesAreReproducible(JTestReport& r) {
    auto d = JScopeDriverRegistry::instance().create("synthetic");
    r.check(d != nullptr, "synthetic driver is available");
    if (!d) return;

    JScopeDeviceInfo info;
    info.driverId = "synthetic";
    r.check(d->open(info), "driver opens");

    JScopeTriggerConfig trig;
    trig.mode          = JScopeTriggerMode::Normal;   // must genuinely trigger
    trig.slope         = JScopeTriggerSlope::Rising;
    trig.sourceChannel = 0;
    trig.levelVolts    = 0.0;
    r.check(d->applyTrigger(trig), "trigger applied");

    r.check(d->start(JScopeAcquisitionMode::Windowed), "acquisition starts");

    const auto frames = collect(*d, 6);
    d->stop();
    d->close();

    r.check(frames.size() >= 6, "six triggered frames arrived");
    if (frames.size() < 2) return;

    bool identical = true;
    size_t firstDifferingPlane = 0;
    for (size_t i = 1; i < frames.size(); ++i) {
        if (frames[i].size() != frames[0].size()) { identical = false; break; }
        for (size_t p = 0; p < frames[i].size(); ++p)
            if (frames[i][p] != frames[0][p]) {
                identical = false;
                firstDifferingPlane = p;
            }
    }
    r.check(identical, identical
        ? "every triggered frame is sample-identical — the trace stands still"
        : "FRAMES DIFFER at plane " + std::to_string(firstDifferingPlane)
          + " — the trace will twitch sideways");
}

// The two shapes with a transition are the ones that used to break, so they get
// their own check: a discontinuous waveform must still reproduce exactly.
void testEdgedWaveformsAreReproducible(JTestReport& r) {
    auto d = JScopeDriverRegistry::instance().create("synthetic");
    JScopeDeviceInfo info;
    info.driverId = "synthetic";
    d->open(info);

    auto* synth = static_cast<JSyntheticDriver*>(d.get());

    // Put a square on the trigger source and a ramp beside it, both at the base
    // frequency, so their edges land on the sample grid the way they do in use.
    JSyntheticChannel sq;
    sq.waveform = JSyntheticWaveform::Square;
    sq.amplitudeVolts = 2.0;
    sq.frequencyHz = 1000.0;
    synth->setSignal(0, sq);

    JSyntheticChannel ramp;
    ramp.waveform = JSyntheticWaveform::Ramp;
    ramp.amplitudeVolts = 2.0;
    ramp.frequencyHz = 1000.0;
    synth->setSignal(1, ramp);

    JScopeTriggerConfig trig;
    trig.mode = JScopeTriggerMode::Normal;
    trig.slope = JScopeTriggerSlope::Rising;
    trig.sourceChannel = 0;
    trig.levelVolts = 0.0;
    d->applyTrigger(trig);
    d->start(JScopeAcquisitionMode::Windowed);

    const auto frames = collect(*d, 6);
    d->stop();
    d->close();

    r.check(frames.size() >= 6, "six frames arrived with edged waveforms");
    bool identical = frames.size() >= 2;
    for (size_t i = 1; i < frames.size(); ++i)
        if (frames[i] != frames[0]) identical = false;
    r.check(identical, "square and ramp reproduce exactly — a sample on an edge does not flip");
}

// A loop that retires ITSELF — a completed single shot, a fatal transport error,
// the end of a non-looping capture — clears the running flag but leaves the
// std::thread joinable. Assigning a new thread over a joinable one calls
// std::terminate, so the next Run aborts the whole application.
//
// It killed jscope on the bench: the scope was unplugged, Run failed, and the
// next Run took the process down. Both sides need the guard — stop() must join
// even when it did not clear the flag, and start() must reap before it spawns.
void testRestartAfterSelfRetire(JTestReport& r) {
    auto d = JScopeDriverRegistry::instance().create("synthetic");
    JScopeDeviceInfo info;
    info.driverId = "synthetic";
    d->open(info);

    // single() retires the loop by itself once a frame has been produced.
    r.check(d->single(), "a single shot starts");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (d->state() != JScopeState::Stopped &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    r.check(d->state() == JScopeState::Stopped, "and retires itself");

    // This is the line that used to abort the process.
    r.check(d->start(JScopeAcquisitionMode::Windowed),
            "starting again after a self-retired loop does not terminate");
    d->stop();

    // And again, the other way round: stop() after a self-retire, then start.
    r.check(d->single(), "a second single shot starts");
    while (d->state() != JScopeState::Stopped &&
           std::chrono::steady_clock::now() < deadline + std::chrono::seconds(5))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    d->stop();
    r.check(d->start(JScopeAcquisitionMode::Windowed),
            "stop() then start() after a self-retire is also safe");
    d->stop();
    d->close();
}

} // namespace

int main() {
    JTestReport r("JSyntheticDriver stability");
    testTriggeredFramesAreReproducible(r);
    testEdgedWaveformsAreReproducible(r);
    testRestartAfterSelfRetire(r);
    return r.result();
}
