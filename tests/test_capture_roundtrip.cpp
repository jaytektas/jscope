// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "capture/JCaptureReader.h"
#include "capture/JCaptureWriter.h"
#include "support/JTestReport.h"
#include "support/JWaveformFixture.h"

#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

using namespace jf;

namespace {

std::string scratchPath() {
    char nameTemplate[] = "/tmp/jscope_capture_XXXXXX";
    const int fd = ::mkstemp(nameTemplate);
    if (fd >= 0) ::close(fd);
    return nameTemplate;
}

JCaptureMeta makeMeta(uint8_t channels) {
    JCaptureMeta m;
    m.driverId            = "synthetic";
    m.model               = "Test device";
    m.serialNumber        = "SN-0001";
    m.startedAtUtc        = "2026-09-05T00:00:00Z";
    m.channelCount        = channels;
    m.adcBits             = 12;
    m.countsMin           = 0;
    m.countsMax           = 4095;
    m.verticalDivisions   = 8;
    m.horizontalDivisions = 10;
    m.timebase.secondsPerDiv = 1.0e-4;
    m.timebase.recordLength  = 1000;
    m.trigger.levelVolts     = 0.25;
    for (uint8_t c = 0; c < channels; ++c) {
        JScopeChannelConfig cfg;
        cfg.voltsPerDiv = 0.5;
        cfg.offsetVolts = 0.1 * c;
        m.channels.push_back(cfg);
        m.channelLabels.push_back("CH" + std::to_string(c + 1));
        m.voltsPerDivSteps.push_back({ 0.1, 0.2, 0.5, 1.0 });
    }
    return m;
}

// A capture must come back byte-for-byte. Samples are the whole point of the
// file; anything less than exact means a measurement taken from a recording
// disagrees with the same measurement taken live.
void testRoundTripIsExact(JTestReport& r) {
    const std::string path = scratchPath();
    constexpr int kFrames = 12;

    std::vector<JScopeFrame> sent(kFrames);
    {
        JCaptureWriter w;
        r.check(w.open(path, makeMeta(1)), "the writer opens a capture");

        for (int i = 0; i < kFrames; ++i) {
            JWaveformFixture::sine(sent[i], 500, 1.0e-5, 1000.0 + i * 10.0, 1.0);
            sent[i].header.sequence         = static_cast<uint64_t>(i);
            sent[i].header.timestampSeconds = i * 0.05;
            sent[i].header.startSampleIndex = static_cast<uint64_t>(i) * 500;
            sent[i].header.triggerSampleIndex = 250;
            sent[i].header.triggered        = true;
            w.write(sent[i]);
        }
        w.close();
        r.check(w.framesWritten() == kFrames, "every frame reached the file");
    }

    JCaptureReader rd;
    r.check(rd.open(path), "the reader opens what the writer wrote");
    r.check(!rd.wasRecovered(), "a properly closed capture needs no recovery");
    r.check(rd.frameCount() == kFrames, "the frame count round-trips");

    r.check(rd.meta().model == "Test device",   "metadata: model");
    r.check(rd.meta().adcBits == 12,            "metadata: ADC width");
    r.check(rd.meta().channelCount == 1,        "metadata: channel count");
    r.check(rd.meta().channelLabels.size() == 1 && rd.meta().channelLabels[0] == "CH1",
            "metadata: channel labels");
    r.check(rd.meta().voltsPerDivSteps.size() == 1 &&
            rd.meta().voltsPerDivSteps[0].size() == 4,
            "metadata: the original device's V/div steps");
    r.check(rd.meta().trigger.levelVolts == 0.25, "metadata: trigger level");

    JScopeFrame got;
    bool headersMatch = true, samplesMatch = true;
    for (int i = 0; i < kFrames; ++i) {
        if (!rd.readFrame(static_cast<uint64_t>(i), got)) { headersMatch = false; break; }
        const JScopeFrameHeader& a = sent[i].header;
        const JScopeFrameHeader& b = got.header;
        if (a.sequence != b.sequence || a.sampleCount != b.sampleCount ||
            a.channelCount != b.channelCount || a.sampleInterval != b.sampleInterval ||
            a.startSampleIndex != b.startSampleIndex ||
            a.triggerSampleIndex != b.triggerSampleIndex ||
            a.countsToVolts[0] != b.countsToVolts[0] ||
            a.zeroOffsetCounts[0] != b.zeroOffsetCounts[0])
            headersMatch = false;
        for (uint32_t s = 0; s < a.sampleCount; ++s)
            if (sent[i].plane(0)[s] != got.plane(0)[s]) samplesMatch = false;
    }
    r.check(headersMatch, "every frame header round-trips exactly");
    r.check(samplesMatch, "every sample round-trips exactly");

    std::remove(path.c_str());
}

// An interrupted recording is a normal case, not a corrupt file. Refusing to
// open it would throw away exactly the capture someone most wants to look at.
void testTruncatedFileRecovers(JTestReport& r) {
    const std::string path = scratchPath();
    {
        JCaptureWriter w;
        w.open(path, makeMeta(1));
        JScopeFrame f;
        for (int i = 0; i < 8; ++i) {
            JWaveformFixture::sine(f, 200, 1.0e-5, 1000.0, 1.0);
            f.header.sequence = static_cast<uint64_t>(i);
            f.header.timestampSeconds = i * 0.01;
            w.write(f);
        }
        w.close();
    }

    // Chop off the index and footer, and part of the last frame with them —
    // exactly what a crash mid-write leaves behind.
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    std::fseek(fp, 0, SEEK_END);
    const long size = std::ftell(fp);
    std::fclose(fp);
    ::truncate(path.c_str(), size - 300);

    JCaptureReader rd;
    r.check(rd.open(path), "a truncated capture still opens");
    r.check(rd.wasRecovered(), "and reports that its index had to be rebuilt");
    r.check(rd.frameCount() > 0, "the frames that did get written are readable");
    r.check(rd.frameCount() < 8, "the incomplete frame at the end is not offered");

    JScopeFrame got;
    r.check(rd.readFrame(0, got), "a recovered frame reads back");
    r.check(got.header.sampleCount == 200, "and has its real shape");

    std::remove(path.c_str());
}

void testSeekByTime(JTestReport& r) {
    const std::string path = scratchPath();
    {
        JCaptureWriter w;
        w.open(path, makeMeta(1));
        JScopeFrame f;
        for (int i = 0; i < 20; ++i) {
            JWaveformFixture::dc(f, 100, 1.0e-5, i * 0.1);
            f.header.sequence = static_cast<uint64_t>(i);
            f.header.timestampSeconds = i * 0.5;      // 0.0 .. 9.5 s
            w.write(f);
        }
        w.close();
    }

    JCaptureReader rd;
    rd.open(path);
    r.check(rd.startTime() == 0.0, "start time is the first frame's");
    r.check(rd.endTime() == 9.5,   "end time is the last frame's");
    r.check(rd.frameAtTime(0.0) == 0,  "seeking to the start finds frame 0");
    r.check(rd.frameAtTime(2.6) == 5,  "seeking mid-record finds the frame at or before");
    r.check(rd.frameAtTime(100.0) == 19, "seeking past the end clamps to the last frame");
    r.check(rd.frameAtTime(-5.0) == 0,   "seeking before the start clamps to the first");

    std::remove(path.c_str());
}

void testRejectsNonCaptures(JTestReport& r) {
    const std::string path = scratchPath();
    {
        std::FILE* fp = std::fopen(path.c_str(), "wb");
        const char junk[] = "this is not a capture file at all, not even close";
        std::fwrite(junk, 1, sizeof junk, fp);
        std::fclose(fp);
    }
    JCaptureReader rd;
    r.check(!rd.open(path), "a file that is not a capture is refused");
    r.check(!rd.open("/nonexistent/path/capture.jscope"), "a missing file is refused");
    std::remove(path.c_str());
}

void testMultiChannel(JTestReport& r) {
    const std::string path = scratchPath();
    {
        JCaptureWriter w;
        w.open(path, makeMeta(4));
        JScopeFrame f;
        f.provision(4, 100);
        f.shape(4, 100);
        f.header.sampleInterval = 1.0e-5;
        for (uint8_t p = 0; p < 4; ++p) {
            f.header.channelIds[p]       = p;
            f.header.countsToVolts[p]    = 0.001f;
            f.header.zeroOffsetCounts[p] = 2048.0f;
            for (uint32_t s = 0; s < 100; ++s)
                f.plane(p)[s] = static_cast<int16_t>(2048 + p * 100 + s);
        }
        w.write(f);
        w.close();
    }

    JCaptureReader rd;
    rd.open(path);
    JScopeFrame got;
    r.check(rd.readFrame(0, got), "a four-channel frame reads back");
    r.check(got.header.channelCount == 4, "with all four planes");

    bool planesMatch = true;
    for (uint8_t p = 0; p < 4; ++p)
        for (uint32_t s = 0; s < 100; ++s)
            if (got.plane(p)[s] != static_cast<int16_t>(2048 + p * 100 + s))
                planesMatch = false;
    r.check(planesMatch, "every plane is intact and in the right order");

    std::remove(path.c_str());
}

} // namespace

int main() {
    JTestReport r("JCaptureWriter and JCaptureReader");
    testRoundTripIsExact(r);
    testTruncatedFileRecovers(r);
    testSeekByTime(r);
    testRejectsNonCaptures(r);
    testMultiChannel(r);
    return r.result();
}
