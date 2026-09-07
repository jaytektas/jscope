// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "drivers/JHantek1008Codec.h"
#include "drivers/JHantek1008Tables.h"
#include "support/JTestReport.h"

#include <cmath>
#include <vector>

using namespace jf;

namespace {

void testShorts(JTestReport& r) {
    // Little-endian, matching the reference's data[i] + data[i+1] * 256.
    const uint8_t bytes[] = { 0x00, 0x08, 0xff, 0x0f, 0x34, 0x12 };
    const auto s = JHantek1008Codec::toShorts(bytes, sizeof bytes);
    r.check(s.size() == 3, "two bytes make one sample");
    r.check(s[0] == 0x0800, "little-endian: 00 08 is 2048");
    r.check(s[1] == 0x0fff, "little-endian: ff 0f is 4095, the top of the 12-bit range");
    r.check(s[2] == 0x1234, "little-endian: 34 12 is 0x1234");

    r.check(JHantek1008Codec::toShorts(nullptr, 10).empty(), "a null buffer yields nothing");
    r.check(JHantek1008Codec::toShorts(bytes, 1).empty(), "a single byte is not a sample");
}

// Getting the stride wrong does not fail loudly: it produces a plausible
// waveform belonging to the wrong channel. That is precisely why this is tested.
// THE WIRE STRIDE IS NOT THE ACTIVE CHANNEL COUNT. Measured on the instrument
// by enabling channels one at a time and inspecting the raw interleave:
//
//     active   1  2  3  4  5  6  7  8
//     stride   1  2  4  4  6  6  8  8
//
// An odd count above one is padded up to even. Reading three channels with a
// stride of three makes every plane a rotation through all four lanes, and a
// 1 kHz square came back as a clean, convincing 66.7 kHz — four times the sample
// rate, which is the signature of exactly this mistake.
void testWireStrideRule(JTestReport& r) {
    const uint8_t expected[9] = { 0, 1, 2, 4, 4, 6, 6, 8, 8 };
    bool ok = true;
    for (uint8_t n = 1; n <= 8; ++n)
        if (JHantek1008Tables::burstStrideFor(n) != expected[n]) ok = false;
    r.check(ok, "burst stride pads an odd channel count up to even");
    r.check(JHantek1008Tables::burstStrideFor(1) == 1,
            "one channel is NOT padded — it really does return a single lane");
    r.check(JHantek1008Tables::burstStrideFor(3) == 4,
            "three channels arrive as four lanes");
    r.check(JHantek1008Tables::burstStrideFor(7) == 8,
            "seven channels arrive as eight lanes");

    // Roll pads differently: exactly one extra lane, the phantom the reference
    // documents.
    r.check(JHantek1008Tables::rollStrideFor(8) == 9, "roll adds one phantom lane");
    r.check(JHantek1008Tables::rollStrideFor(1) == 2, "even for a single channel");
}

// Reading a padded interleave at the wrong stride must produce visibly different
// data — otherwise the test above is asserting a rule nothing depends on.
void testPaddedInterleaveNeedsTheRealStride(JTestReport& r) {
    // Three channels on a four-lane wire: lane 3 is padding.
    std::vector<uint16_t> shorts;
    for (int s = 0; s < 6; ++s) {
        for (int c = 0; c < 3; ++c) shorts.push_back(static_cast<uint16_t>(100 * c + s));
        shorts.push_back(9999);                       // the padding lane
    }

    constexpr size_t cap = 8;
    int16_t right[3 * cap]{}, wrong[3 * cap]{};
    const size_t n = JHantek1008Codec::deinterleave(shorts.data(), shorts.size(), 3,
                                                    JHantek1008Tables::burstStrideFor(3),
                                                    right, cap);
    r.check(n == 6, "six samples per channel at the real stride");

    bool correct = true, padLeaked = false;
    for (int c = 0; c < 3; ++c)
        for (int s = 0; s < 6; ++s) {
            if (right[c * cap + s] != 100 * c + s) correct = false;
            if (right[c * cap + s] == 9999) padLeaked = true;
        }
    r.check(correct, "every channel is intact when the padding lane is accounted for");
    r.check(!padLeaked, "the padding lane never reaches a plane");

    // The same bytes at a stride of three — what the driver used to do.
    JHantek1008Codec::deinterleave(shorts.data(), shorts.size(), 3, 3, wrong, cap);
    bool differs = false, wrongPadLeaked = false;
    for (size_t i = 0; i < 3 * cap; ++i) {
        if (wrong[i] != right[i]) differs = true;
        if (wrong[i] == 9999) wrongPadLeaked = true;
    }
    r.check(differs, "reading at the wrong stride produces different, wrong data");
    r.check(wrongPadLeaked, "and leaks the padding lane into the channels");

    // A stride smaller than the channel count is nonsense and must be refused
    // rather than read out of bounds.
    r.check(JHantek1008Codec::deinterleave(shorts.data(), shorts.size(), 4, 3,
                                           wrong, cap) == 0,
            "a stride narrower than the channel count is refused");
}

void testDeinterleave(JTestReport& r) {
    // Three channels, four samples each, interleaved c0 c1 c2 c0 c1 c2 ...
    std::vector<uint16_t> shorts;
    for (int s = 0; s < 4; ++s)
        for (int c = 0; c < 3; ++c)
            shorts.push_back(static_cast<uint16_t>(100 * c + s));

    constexpr size_t stride = 8;
    int16_t planes[3 * stride]{};
    const size_t n = JHantek1008Codec::deinterleave(shorts.data(), shorts.size(), 3,
                                                    /*wireStride=*/3, planes, stride);
    r.check(n == 4, "four complete samples per channel");

    bool ok = true;
    for (int c = 0; c < 3; ++c)
        for (int s = 0; s < 4; ++s)
            if (planes[c * stride + s] != 100 * c + s) ok = false;
    r.check(ok, "each plane holds its own channel's samples, in order");
}

// Roll mode interleaves a ninth channel whose values sit around 1742. It
// advances the stride and must be discarded — miss it and every channel's data
// slides by one position each sample.
void testPhantomChannelIsDiscarded(JTestReport& r) {
    std::vector<uint16_t> shorts;
    for (int s = 0; s < 5; ++s) {
        for (int c = 0; c < 2; ++c) shorts.push_back(static_cast<uint16_t>(10 * c + s));
        shorts.push_back(1742);                        // the phantom
    }

    constexpr size_t stride = 8;
    int16_t planes[2 * stride]{};
    const size_t n = JHantek1008Codec::deinterleave(shorts.data(), shorts.size(), 2,
                                                    /*wireStride=*/3, planes, stride);
    r.check(n == 5, "the phantom does not reduce the sample count");

    bool ok = true, phantomLeaked = false;
    for (int c = 0; c < 2; ++c)
        for (int s = 0; s < 5; ++s) {
            if (planes[c * stride + s] != 10 * c + s) ok = false;
            if (planes[c * stride + s] == 1742) phantomLeaked = true;
        }
    r.check(ok, "both real channels are correct with the phantom in the rotation");
    r.check(!phantomLeaked, "the phantom channel never appears in a plane");

    // The same bytes read WITHOUT accounting for the phantom must come out
    // wrong — which is what proves the test is actually testing something.
    int16_t wrong[2 * stride]{};
    JHantek1008Codec::deinterleave(shorts.data(), shorts.size(), 2, 2, wrong, stride);
    bool differs = false;
    for (size_t i = 0; i < 2 * stride; ++i) if (wrong[i] != planes[i]) differs = true;
    r.check(differs, "ignoring the phantom produces different, wrong data");
}

void testSamplesPerChannel(JTestReport& r) {
    r.check(JHantek1008Codec::samplesPerChannel(80, 8) == 10, "8 lanes");
    r.check(JHantek1008Codec::samplesPerChannel(90, 9) == 10, "9 lanes, roll's phantom");
    r.check(JHantek1008Codec::samplesPerChannel(85, 8) == 10, "a partial sample is dropped");
    r.check(JHantek1008Codec::samplesPerChannel(80, 0) == 0,  "no lanes, no samples");
}

// volts = (raw - zeroOffset) * 0.01 * vscale, straight from the reference.
void testVoltsConversion(JTestReport& r) {
    r.check(std::abs(JHantek1008Codec::toVolts(2048, 2048, 1.0)) < 1e-12,
            "a count at the zero offset is 0 V");
    r.check(std::abs(JHantek1008Codec::toVolts(2148, 2048, 1.0) - 1.0) < 1e-12,
            "+100 counts at vscale 1.0 is +1 V");
    r.check(std::abs(JHantek1008Codec::toVolts(2148, 2048, 0.125) - 0.125) < 1e-12,
            "the same counts at vscale 0.125 are one eighth of the volts");
    r.check(std::abs(JHantek1008Codec::toVolts(1948, 2048, 1.0) + 1.0) < 1e-12,
            "-100 counts is -1 V");

    // Full scale at vscale 1.0 is +/-20.48 V — the automotive range this
    // instrument exists for.
    r.check(std::abs(JHantek1008Codec::toVolts(4095, 2048, 1.0) - 20.47) < 1e-9,
            "the top of the range is +20.47 V at vscale 1.0");
}

void testVoltsPerDivLadder(JTestReport& r) {
    // Each hardware range has a maximum volts/div it can show without clipping:
    // 4096 counts x 10 mV x vscale, over eight divisions.
    r.check(std::abs(JHantek1008Tables::maxVoltsPerDivFor(1.0) - 5.12) < 1e-9,
            "the widest range tops out at 5.12 V/div");
    r.check(std::abs(JHantek1008Tables::maxVoltsPerDivFor(0.125) - 0.64) < 1e-9,
            "the middle range at 0.64 V/div");
    r.check(std::abs(JHantek1008Tables::maxVoltsPerDivFor(0.02) - 0.1024) < 1e-9,
            "the narrowest at 0.1024 V/div");

    // What the USER is offered is a 1-2-5 ladder, as any scope has. Each step
    // picks the NARROWEST range that holds it, because narrower means finer
    // resolution.
    bool ascending = true, fits = true, narrowest = true;
    const auto& steps = JHantek1008Tables::kVoltsPerDivSteps;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (i && steps[i] <= steps[i - 1]) ascending = false;
        const double chosen = JHantek1008Tables::vscaleForVoltsPerDiv(steps[i]);
        if (JHantek1008Tables::maxVoltsPerDivFor(chosen) < steps[i]) fits = false;
        // No narrower range would have done.
        for (double f : JHantek1008Tables::kVScaleFactors)
            if (f < chosen && JHantek1008Tables::maxVoltsPerDivFor(f) >= steps[i])
                narrowest = false;
    }
    r.check(ascending, "the ladder is strictly ascending, as the capability contract requires");
    r.check(fits, "every step maps to a range that can show it without clipping");
    r.check(narrowest, "and to the NARROWEST such range, for the finest resolution");

    // 5 V/div is the top: eight divisions of it is 40 V against a 40.96 V span.
    r.check(steps.back() == 5.0, "the ladder stops at 5 V/div");
    r.check(JHantek1008Tables::maxVoltsPerDivFor(1.0) >= steps.back(),
            "which the widest range can still hold");
}

void testVScaleIds(JTestReport& r) {
    r.check(JHantek1008Tables::vscaleId(0.02)  == 1, "vscale ids are 1-based in table order");
    r.check(JHantek1008Tables::vscaleId(0.125) == 2, "0.125 is id 2");
    r.check(JHantek1008Tables::vscaleId(1.0)   == 3, "1.0 is id 3");
    r.check(JHantek1008Tables::vscaleForId(2) == 0.125, "ids map back to factors");
    r.check(JHantek1008Tables::vscaleId(0.7) == 3,
            "an unsupported factor falls back to the widest range, never to a narrower one");
}

// The wire code is an index into a (1|2|5) x 10^n ladder, but only SOME codes
// are distinct and honoured — see kUsableTimeDivIds. What the UI and the driver
// deal in is what one DISPLAY division represents, since the device always hands
// back kDefaultRecordSamples whatever the code's nominal sweep length.
void testTimeDivIds(JTestReport& r) {
    r.check(JHantek1008Tables::nsPerDivForId(0) == 1.0,  "id 0 is 1 ns/div");
    r.check(JHantek1008Tables::nsPerDivForId(1) == 2.0,  "id 1 is 2 ns/div");
    r.check(JHantek1008Tables::nsPerDivForId(2) == 5.0,  "id 2 is 5 ns/div");
    r.check(JHantek1008Tables::nsPerDivForId(3) == 10.0, "id 3 is 10 ns/div");

    // Codes 17..19 are the ones whose label and reality agree exactly, because
    // their record length is the default. Measured on hardware against a 1 kHz
    // reference: 999.97, 999.99, 1000.00 Hz.
    const auto near = [](double a, double b) { return std::abs(a - b) / b < 1.0e-9; };
    r.check(near(JHantek1008Tables::displaySecondsPerDivFor(17), 500.0e-6),
            "code 17 is 500 us per display division");
    r.check(near(JHantek1008Tables::displaySecondsPerDivFor(18), 1.0e-3),
            "code 18 is 1 ms");
    r.check(near(JHantek1008Tables::displaySecondsPerDivFor(19), 2.0e-3),
            "code 19 is 2 ms");

    r.check(JHantek1008Tables::nsPerDivIdFor(500.0e-6) == 17,
            "500 us/div selects code 17");

    // Every usable code must round-trip: asking for what a code gives must
    // select that same code, or the panel and the device disagree.
    bool roundTrips = true;
    for (uint8_t id : JHantek1008Tables::kUsableTimeDivIds)
        if (JHantek1008Tables::nsPerDivIdFor(
                JHantek1008Tables::displaySecondsPerDivFor(id)) != id) roundTrips = false;
    r.check(roundTrips, "every usable timebase round-trips through the selector");

    // An off-step request quantises to the nearest usable step BY RATIO, not by
    // linear distance. 3 us is far below anything this device can do, so it
    // lands on the fastest usable code rather than being refused.
    r.check(JHantek1008Tables::nsPerDivIdFor(3.0e-6) ==
                JHantek1008Tables::kUsableTimeDivIds.front(),
            "a request below the device's range quantises to its fastest step");

    // Code 16 is excluded deliberately: it is accepted on the wire and then run
    // at code 17's rate, which measured 1.4286x fast on the bench.
    bool has16 = false;
    for (uint8_t id : JHantek1008Tables::kUsableTimeDivIds) if (id == 16) has16 = true;
    r.check(!has16, "code 16 is not offered — the device does not honour it");

    // Codes 0..15 share one constant, so exactly one of them stands for the group.
    int fastCodes = 0;
    for (uint8_t id : JHantek1008Tables::kUsableTimeDivIds) if (id <= 15) ++fastCodes;
    r.check(fastCodes == 1, "the sixteen codes that share a rate are offered once");
}

void testChannelMap(JTestReport& r) {
    const auto map = JHantek1008Codec::channelMap({ 0, 2, 7 });
    r.check(map.size() == 8, "the map is always eight bytes");
    r.check(map[0] == 1 && map[2] == 1 && map[7] == 1, "active channels are 1");
    r.check(map[1] == 0 && map[3] == 0 && map[6] == 0, "inactive channels are 0");

    const auto none = JHantek1008Codec::channelMap({});
    bool allZero = true;
    for (uint8_t b : none) if (b) allZero = false;
    r.check(allZero, "no channels gives an all-zero map rather than a short one");
}

} // namespace

int main() {
    JTestReport r("JHantek1008Codec");
    testShorts(r);
    testWireStrideRule(r);
    testPaddedInterleaveNeedsTheRealStride(r);
    testDeinterleave(r);
    testPhantomChannelIsDiscarded(r);
    testSamplesPerChannel(r);
    testVoltsConversion(r);
    testVoltsPerDivLadder(r);
    testVScaleIds(r);
    testTimeDivIds(r);
    testChannelMap(r);
    return r.result();
}
