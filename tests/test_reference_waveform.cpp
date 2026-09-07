// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "scope/JReferenceWaveform.h"
#include "support/JTestReport.h"

#include <cmath>
#include <set>
#include <string>

using namespace jf;

// A reference trace is only worth anything if its SHAPE is right, so what is
// asserted here is shape: a crank signal that swings both ways and goes flat
// across the missing teeth, an injector that notches on the way up, a lambda
// sensor that crosses rather than drifts. Asserting sample values would only
// restate the constants in the model back at itself.

namespace {

int longestRunNearZero(const std::vector<float>& v, float tolerance) {
    int best = 0, run = 0;
    for (float s : v) {
        if (std::fabs(s) < tolerance) { ++run; if (run > best) best = run; }
        else run = 0;
    }
    return best;
}

size_t countWithin(const std::vector<float>& v, float lo, float hi) {
    size_t n = 0;
    for (float s : v) if (s > lo && s < hi) ++n;
    return n;
}

void testNoneIsEmpty(JTestReport& r) {
    const auto t = JReferenceWaveform::generate(JReferenceSignal::None);
    r.check(t.samples.empty() && t.window == 0.0,
            "None generates an empty trace, so a caller can render unconditionally");
}

void testEverySignalIsComplete(JTestReport& r) {
    bool ok = true;
    for (auto s : kReferenceSignals) {
        if (s == JReferenceSignal::None) continue;
        const auto t = JReferenceWaveform::generate(s);
        ok = ok && !t.samples.empty() && t.window > 0.0
                && !t.caption.empty() && t.maxValue > t.minValue;
    }
    r.check(ok, "every signal yields samples, a window, a caption and a real range");
}

void testReproducible(JTestReport& r) {
    const auto a = JReferenceWaveform::generate(JReferenceSignal::CrankInductive);
    const auto b = JReferenceWaveform::generate(JReferenceSignal::CrankInductive);
    r.check(a.samples == b.samples,
            "the dither is a fixed sequence, so the same signal generates the same samples");

    const auto c = JReferenceWaveform::generate(JReferenceSignal::CrankHall);
    r.check(a.samples != c.samples, "different signals do not share a dither sequence");
}

void testCrankSwingsBothWaysAndHasAGap(JTestReport& r) {
    const auto t = JReferenceWaveform::generate(JReferenceSignal::CrankInductive);

    // A variable-reluctance sensor swings either side of zero. Drawing it as a
    // positive-only pulse train is the commonest way to get this signal wrong,
    // and it is the thing a reference exists to contradict.
    r.check(t.minValue < -1.0 && t.maxValue > 1.0, "the crank signal swings both ways");
    r.check(std::fabs(t.maxValue + t.minValue) < 0.6, "and is roughly symmetric about zero");

    // The missing teeth must read as a flat run much longer than the ordinary
    // zero crossing between two teeth.
    const int flat     = longestRunNearZero(t.samples, 0.08f);
    const int perTooth = static_cast<int>(t.samples.size()) / 14;
    r.check(flat > perTooth, "the missing teeth show as a flat run longer than one tooth");
}

void testHallSitsOnItsRails(JTestReport& r) {
    const auto t = JReferenceWaveform::generate(JReferenceSignal::CrankHall);
    const size_t middling = countWithin(t.samples, 1.0f, 4.0f);
    r.check(middling < t.samples.size() / 20,
            "a Hall switch is on one rail or the other, with only edges in between");
}

void testIgnitionDwellsAndSpikes(JTestReport& r) {
    const auto t = JReferenceWaveform::generate(JReferenceSignal::PrimaryIgnition);
    r.check(t.maxValue > 250.0, "the turn-off spike reaches the hundreds of volts");
    const size_t nearGround = countWithin(t.samples, -1.0f, 2.0f);
    r.check(nearGround > t.samples.size() / 10,
            "the coil is held near ground for the dwell");
}

void testInjectorNotchesOnTheWayUp(JTestReport& r) {
    const auto t = JReferenceWaveform::generate(JReferenceSignal::InjectorCurrent);
    r.check(std::string(t.unit) == "A", "injector current is reported in amps, not volts");

    // The pintle notch is a dip that happens while the overall trend is still
    // rising -- that combination is what distinguishes it from the turn-off.
    bool notched = false;
    const size_t n = t.samples.size();
    for (size_t i = n / 10; i + 40 < n * 6 / 10; ++i) {
        if (t.samples[i] > t.samples[i + 20] + 0.02f && t.samples[i + 40] > t.samples[i]) {
            notched = true;
            break;
        }
    }
    r.check(notched, "current dips as the pintle lifts and then carries on rising");
}

void testLambdaSwitchesRatherThanDrifting(JTestReport& r) {
    const auto t = JReferenceWaveform::generate(JReferenceSignal::LambdaZirconia);
    r.check(t.minValue < 0.2 && t.maxValue > 0.75, "lambda reaches both of its rails");
    r.check(countWithin(t.samples, 0.35f, 0.60f) < t.samples.size() / 4,
            "and crosses quickly rather than sitting mid-range");
}

void testThrottleSweepHasNoDropouts(JTestReport& r) {
    const auto t = JReferenceWaveform::generate(JReferenceSignal::ThrottlePosition);
    // A dropout on the track is exactly what this reference exists to make
    // visible, so the reference itself must not contain one.
    const size_t n = t.samples.size();
    bool monotonic = true;
    for (size_t i = n * 16 / 100; i + 1 < n * 44 / 100; ++i)
        if (t.samples[i + 1] < t.samples[i] - 0.05f) { monotonic = false; break; }
    r.check(monotonic, "the throttle sweep rises without a dropout");
}

void testNamesAreUsableAsMenuEntries(JTestReport& r) {
    std::set<std::string> names;
    bool ok = true;
    for (auto s : kReferenceSignals) {
        const char* n = jReferenceSignalName(s);
        ok = ok && n && *n && names.insert(n).second;
    }
    r.check(ok && names.size() == sizeof(kReferenceSignals) / sizeof(kReferenceSignals[0]),
            "every signal has a name, and no two share one");
}

} // namespace

int main() {
    JTestReport r("reference waveform");
    testNoneIsEmpty(r);
    testEverySignalIsComplete(r);
    testReproducible(r);
    testCrankSwingsBothWaysAndHasAGap(r);
    testHallSitsOnItsRails(r);
    testIgnitionDwellsAndSpikes(r);
    testInjectorNotchesOnTheWayUp(r);
    testLambdaSwitchesRatherThanDrifting(r);
    testThrottleSweepHasNoDropouts(r);
    testNamesAreUsableAsMenuEntries(r);
    return r.result();
}
