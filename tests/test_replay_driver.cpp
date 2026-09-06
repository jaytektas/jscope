#include "capture/JCaptureWriter.h"
#include "sources/JReplayDriver.h"
#include "scope/JScopeDriverRegistry.h"
#include "support/JTestReport.h"
#include "support/JWaveformFixture.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>

using namespace jf;

namespace {

std::string writeFixtureCapture(int frames, uint8_t channels = 2) {
    char nameTemplate[] = "/tmp/jscope_replay_XXXXXX";
    const int fd = ::mkstemp(nameTemplate);
    if (fd >= 0) ::close(fd);
    const std::string path = nameTemplate;

    JCaptureMeta m;
    m.driverId            = "hantek-1008c";     // pretend it came from the 8-channel unit
    m.model               = "Hantek 1008C";
    m.serialNumber        = "CN-TEST";
    m.startedAtUtc        = "2026-09-05T00:00:00Z";
    m.channelCount        = channels;
    m.adcBits             = 12;
    m.countsMin           = 0;
    m.countsMax           = 4095;
    m.verticalDivisions   = 8;
    m.horizontalDivisions = 10;
    m.timebase.secondsPerDiv = 1.0e-4;
    m.timebase.recordLength  = 500;
    for (uint8_t c = 0; c < channels; ++c) {
        JScopeChannelConfig cfg;
        cfg.voltsPerDiv = 0.5;
        m.channels.push_back(cfg);
        m.channelLabels.push_back("CH" + std::to_string(c + 1));
        // A distinctive step list, so the test can prove the replay driver takes
        // its capabilities from the FILE rather than inventing a generic set.
        m.voltsPerDivSteps.push_back({ 0.02, 0.125, 1.0 });
    }

    JCaptureWriter w;
    w.open(path, m);
    JScopeFrame f;
    for (int i = 0; i < frames; ++i) {
        f.provision(channels, 500);
        f.shape(channels, 500);
        f.header.sequence         = static_cast<uint64_t>(i);
        f.header.timestampSeconds = i * 0.01;
        f.header.sampleInterval   = 1.0e-6;
        for (uint8_t p = 0; p < channels; ++p) {
            f.header.channelIds[p]       = p;
            f.header.countsToVolts[p]    = 0.001f;
            f.header.zeroOffsetCounts[p] = 2048.0f;
            for (uint32_t s = 0; s < 500; ++s)
                f.plane(p)[s] = static_cast<int16_t>(2048 + i * 10 + p);
        }
        w.write(f);
    }
    w.close();
    return path;
}

// The point of the replay driver: the UI above it cannot tell it from hardware,
// which is what lets the whole application be developed and regression-tested
// with nothing plugged in.
void testReplayLooksLikeADevice(JTestReport& r) {
    const std::string path = writeFixtureCapture(10);

    auto d = JScopeDriverRegistry::instance().create("replay");
    r.check(d != nullptr, "the replay driver is registered like any other");

    JScopeDeviceInfo info;
    info.driverId = "replay";
    info.path     = path;
    r.check(d->open(info), "a capture opens as a device");

    const JScopeCapabilities& caps = d->capabilities();
    r.check(caps.channelCount() == 2, "channel count comes from the file");
    r.check(caps.adcBits == 12,       "ADC width comes from the file");
    r.check(caps.model.find("1008C") != std::string::npos,
            "the model names the instrument that recorded it");
    r.check(caps.channels[0].voltsPerDiv.size() == 3 &&
            caps.channels[0].voltsPerDiv[0] == 0.02,
            "V/div steps are the ORIGINAL device's, not a generic set");

    // A recording cannot be re-dialled, and the capabilities say so rather than
    // the UI offering controls that quietly do nothing.
    r.check(caps.triggerModes == 0,        "a recording offers no sweep modes");
    r.check(!caps.hasForceTrigger,         "and no force trigger");
    r.check(!caps.hasAutoset,              "and no autoset");
    r.check(caps.channels[0].couplings.empty(), "and no coupling choice");
    r.check(caps.channels[0].offsetRangeVolts == 0.0, "and no offset control");
    r.check(!d->applyTimebase(d->timebaseConfig()), "applying a setting is refused");

    d->close();
    std::remove(path.c_str());
}

void testFramesMatchTheRecording(JTestReport& r) {
    const std::string path = writeFixtureCapture(6);

    auto d = JScopeDriverRegistry::instance().create("replay");
    JScopeDeviceInfo info;
    info.driverId = "replay";
    info.path     = path;
    d->open(info);

    auto* replay = static_cast<JReplayDriver*>(d.get());
    r.check(replay->frameCount() == 6, "the frame count is the recording's");

    // Seek publishes the sought frame whether or not playback is running —
    // otherwise dragging a scrub bar while paused would show nothing.
    replay->seek(3);
    JScopeFrame* f = nullptr;
    for (int i = 0; i < 200 && !f; ++i) {
        f = d->pool().tryPopReady();
        if (!f) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    r.check(f != nullptr, "seeking while stopped still publishes a frame");
    if (f) {
        r.check(f->header.sequence == 3, "and it is the frame that was sought");
        r.check(f->plane(0)[0] == static_cast<int16_t>(2048 + 3 * 10),
                "carrying that frame's samples");
        d->pool().release(f);
    }

    replay->step(-1);
    r.check(replay->position() == 2, "stepping back moves one frame");
    replay->step(-100);
    r.check(replay->position() == 0, "stepping past the start clamps");
    replay->step(100);
    r.check(replay->position() == 5, "stepping past the end clamps to the last frame");

    d->close();
    std::remove(path.c_str());
}

void testPlaybackRuns(JTestReport& r) {
    const std::string path = writeFixtureCapture(8);

    auto d = JScopeDriverRegistry::instance().create("replay");
    JScopeDeviceInfo info;
    info.driverId = "replay";
    info.path     = path;
    d->open(info);

    auto* replay = static_cast<JReplayDriver*>(d.get());
    replay->setRate(50.0);          // as fast as the driver allows, so the test is quick
    replay->setLooping(false);
    r.check(d->start(JScopeAcquisitionMode::Windowed), "playback starts");

    int collected = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (collected < 8 && std::chrono::steady_clock::now() < deadline) {
        if (JScopeFrame* f = d->pool().tryPopReady()) {
            ++collected;
            d->pool().release(f);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    d->stop();
    r.check(collected >= 8, "every recorded frame is played back");

    d->close();
    std::remove(path.c_str());
}

void testBadCapturesAreRefused(JTestReport& r) {
    auto d = JScopeDriverRegistry::instance().create("replay");
    JScopeDeviceInfo info;
    info.driverId = "replay";
    info.path     = "/nonexistent/capture.jscope";
    r.check(!d->open(info), "a missing capture is refused rather than half-opened");
    r.check(!d->isOpen(),   "and leaves the driver closed");

    // Captures are opened by the user, so there is nothing to enumerate — and
    // returning empty keeps replay out of the device list without a special case.
    r.check(JReplayDriver::enumerate().empty(), "replay enumerates no devices");
}

} // namespace

int main() {
    JTestReport r("JReplayDriver");
    testReplayLooksLikeADevice(r);
    testFramesMatchTheRecording(r);
    testPlaybackRuns(r);
    testBadCapturesAreRefused(r);
    return r.result();
}
