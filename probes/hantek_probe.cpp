// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Brings up the real Hantek 1008C and reports what it does.
//
// A bench tool: it needs the instrument, so it is EXCLUDE_FROM_ALL and never
// registered with ctest.
//
//     cmake --build build --target hantek_probe
//     ./build/hantek_probe            # init, calibrate, one roll capture
//     ./build/hantek_probe --burst    # init, calibrate, one burst capture
//     ./build/hantek_probe --trace    # with every USB byte dumped
//
// The zero offsets it prints are the number to compare against hantek1008py's
// on the same unit: they are per-device, so agreement between the two is the
// evidence that this port reads the instrument the same way the reference does.

#include "drivers/JHantek1008Driver.h"
#include "measure/JMeasurementEngine.h"
#include "scope/JScopeDriverRegistry.h"

#include <j/core/Log.h>

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

using namespace jf;

namespace {

const char* kCat = "probe";

void report(const JScopeFrame& f) {
    JLOGC(kCat, JLogLevel::Info)
        << "frame " << f.header.sequence << ": " << int(f.header.channelCount)
        << " channels x " << f.header.sampleCount << " samples,  dt="
        << f.header.sampleInterval << " s  (" << (1.0 / f.header.sampleInterval)
        << " Sa/s)  triggered=" << (f.header.triggered ? "yes" : "no");

    for (uint8_t p = 0; p < f.header.channelCount; ++p) {
        const auto vpp  = JMeasurementEngine::measure(JMeasurementKind::Vpp,  f, p);
        const auto vmin = JMeasurementEngine::measure(JMeasurementKind::Vmin, f, p);
        const auto vmax = JMeasurementEngine::measure(JMeasurementKind::Vmax, f, p);
        const auto vavg = JMeasurementEngine::measure(JMeasurementKind::Vavg, f, p);
        const auto freq = JMeasurementEngine::measure(JMeasurementKind::Frequency, f, p);
        const auto duty = JMeasurementEngine::measure(JMeasurementKind::DutyCycle, f, p);

        JLOGC(kCat, JLogLevel::Info)
            << "  CH" << int(f.header.channelIds[p] + 1)
            << "  Vpp "  << (vpp.valid  ? std::to_string(vpp.value)  : "-")
            << "  Vmin " << (vmin.valid ? std::to_string(vmin.value) : "-")
            << "  Vmax " << (vmax.valid ? std::to_string(vmax.value) : "-")
            << "  Vavg " << (vavg.valid ? std::to_string(vavg.value) : "-")
            << "  freq " << (freq.valid ? std::to_string(freq.value) : "-")
            << "  duty " << (duty.valid ? std::to_string(duty.value) : "-");
    }
}

} // namespace

int main(int argc, char** argv) {
    bool   burst = false;
    double sdiv  = 0.0;      // 0 = leave the driver's default
    const char* dump = nullptr;   // write one frame's planes here as CSV
    int    channels = 0;     // 0 = leave the driver's default
    int    record   = 0;     // samples per capture across all channels
    int    trigsrc  = -1;    // trigger source channel, 0-based
    double triglevel = 1e9;  // volts; 1e9 = leave the driver's default
    JLog::instance().setGlobalLevel(JLogLevel::Info);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--burst") == 0) burst = true;
        // The burst record length is a function of the timebase, so reproducing
        // a bench report needs the same s/div the app was set to.
        if (std::strcmp(argv[i], "--sdiv") == 0 && i + 1 < argc) sdiv = std::atof(argv[++i]);
        // Raw counts for one frame. Reasoning about a record from summary
        // statistics is how several wrong diagnoses got made on this device.
        if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) dump = argv[++i];
        // The device shares one sample-rate budget across active channels, so a
        // timebase limit found at four channels need not hold at one.
        if (std::strcmp(argv[i], "--channels") == 0 && i + 1 < argc)
            channels = std::atoi(argv[++i]);
        // 0xa1 sets this; the device otherwise sits on its power-on default.
        if (std::strcmp(argv[i], "--record") == 0 && i + 1 < argc)
            record = std::atoi(argv[++i]);
        // Which channel the device triggers on. Proving this works needs several
        // frames: a correctly triggered channel's edge sits at the same sample
        // every time, a channel that is merely present does not.
        if (std::strcmp(argv[i], "--trigsrc") == 0 && i + 1 < argc)
            trigsrc = std::atoi(argv[++i]);
        if (std::strcmp(argv[i], "--triglevel") == 0 && i + 1 < argc)
            triglevel = std::atof(argv[++i]);
        if (std::strcmp(argv[i], "--trace") == 0) {
            JLog::instance().setLevel("usb.*", JLogLevel::Trace);
            JLog::instance().setLevel("scope.hantek1008", JLogLevel::Trace);
        }
        if (std::strcmp(argv[i], "--debug") == 0)
            JLog::instance().setGlobalLevel(JLogLevel::Debug);
    }

    const auto devices = JHantek1008Driver::enumerate();
    if (devices.empty()) {
        JLOGC(kCat, JLogLevel::Error)
            << "no Hantek 1008C found — check the cable and udev/99-hantek-scopes.rules";
        return 1;
    }

    auto d = JScopeDriverRegistry::instance().create("hantek-1008c");
    auto* hantek = static_cast<JHantek1008Driver*>(d.get());

    JLOGC(kCat, JLogLevel::Info) << "opening " << devices[0].displayName;
    if (!d->open(devices[0])) {
        JLOGC(kCat, JLogLevel::Error) << "open failed";
        return 1;
    }

    // The 24 numbers to diff against hantek1008py on this same unit.
    JLOGC(kCat, JLogLevel::Info) << "--- zero offsets (counts) ---";
    for (uint8_t rid = 1; rid <= JHantek1008Calibration::kRanges; ++rid) {
        std::string row;
        for (uint8_t c = 0; c < JHantek1008Tables::kChannelCount; ++c)
            row += "  " + std::to_string(
                static_cast<int>(hantek->calibration().zeroOffset(rid, c) + 0.5));
        JLOGC(kCat, JLogLevel::Info)
            << "  vscale " << JHantek1008Tables::vscaleForId(rid) << ":" << row;
    }

    const JScopeAcquisitionMode mode = burst ? JScopeAcquisitionMode::Windowed
                                             : JScopeAcquisitionMode::Streaming;
    if (channels > 0) {
        for (uint8_t c = 0; c < d->capabilities().channelCount(); ++c) {
            JScopeChannelConfig cc = d->channelConfig(c);
            cc.enabled = (c < channels);
            d->applyChannel(c, cc);
        }
    }

    if (trigsrc >= 0 || triglevel < 1e8) {
        JScopeTriggerConfig tr = d->triggerConfig();
        if (trigsrc >= 0)     tr.sourceChannel = static_cast<uint8_t>(trigsrc);
        if (triglevel < 1e8)  tr.levelVolts    = triglevel;
        tr.mode  = JScopeTriggerMode::Normal;   // do not sweep; prove the trigger
        tr.slope = JScopeTriggerSlope::Rising;
        d->applyTrigger(tr);
        const JScopeTriggerConfig& got = d->triggerConfig();
        JLOGC(kCat, JLogLevel::Info)
            << "trigger: CH" << (got.sourceChannel + 1) << " rising at "
            << got.levelVolts << " V, mode " << jScopeTriggerModeName(got.mode);
    }

    JScopeTimebaseConfig tb = d->timebaseConfig();
    tb.mode = mode;
    if (sdiv > 0.0)  tb.secondsPerDiv = sdiv;
    if (record > 0)  tb.recordLength  = static_cast<uint32_t>(record);
    d->applyTimebase(tb);
    JLOGC(kCat, JLogLevel::Info)
        << "timebase: " << d->timebaseConfig().secondsPerDiv << " s/div";

    JLOGC(kCat, JLogLevel::Info)
        << "--- acquiring in " << jScopeAcquisitionModeName(mode) << " mode ---";
    if (!d->start(mode)) {
        JLOGC(kCat, JLogLevel::Error) << "start failed";
        d->close();
        return 1;
    }

    // Frame rate matters here: the device is FULL SPEED with 64-byte packets and
    // a request/response protocol, so burst throughput is the open question this
    // probe exists to answer.
    const auto begin = std::chrono::steady_clock::now();
    int collected = 0;
    const auto deadline = begin + std::chrono::seconds(10);
    while (collected < 5 && std::chrono::steady_clock::now() < deadline) {
        if (JScopeFrame* f = d->pool().tryPopReady()) {
            report(*f);
            if (dump) {
                std::ofstream out(std::string(dump) + "." + std::to_string(collected));
                out << "sample";
                for (uint8_t p = 0; p < f->header.channelCount; ++p)
                    out << ",CH" << int(f->header.channelIds[p] + 1);
                out << "\n";
                for (uint32_t i2 = 0; i2 < f->header.sampleCount; ++i2) {
                    out << i2;
                    for (uint8_t p = 0; p < f->header.channelCount; ++p)
                        out << "," << f->plane(p)[i2];
                    out << "\n";
                }
                JLOGC(kCat, JLogLevel::Info) << "dumped frame to " << dump;
            }
            d->pool().release(f);
            ++collected;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - begin).count();

    d->stop();
    JLOGC(kCat, JLogLevel::Info)
        << "--- " << collected << " frames in " << seconds << " s  ("
        << (seconds > 0 ? collected / seconds : 0.0) << " frames/s, "
        << d->framesDropped() << " dropped) ---";
    d->close();
    return collected > 0 ? 0 : 1;
}
