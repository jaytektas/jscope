// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JReferenceWaveform.h"

#include <cmath>
#include <cstdint>

inline namespace jf {

namespace {

// ---- the models' own numbers -----------------------------------------------
//
// FACTS ABOUT HARDWARE, not visual constants, so they live here rather than in
// JScopeTheme: a coil really does dwell for a few milliseconds and really does
// spike into the hundreds of volts, and changing either would make the picture
// wrong rather than differently styled.

constexpr size_t kSamples          = 1200;   // enough for a clean curve at any width

constexpr double kBatteryVolts     = 13.8;

// Crank, variable reluctance on a 60-2 wheel at a fast idle.
constexpr int    kCrankTeeth       = 60;
constexpr int    kCrankMissing     = 2;
constexpr int    kCrankTeethShown  = 14;     // either side of the gap, not two revs
constexpr double kCrankRpm         = 900.0;
constexpr double kCrankPeakVolts   = 1.35;
constexpr double kCrankRebuildGain = 1.25;   // first tooth after the gap sees a larger step

// Hall switch: an open-collector output pulled up, so the swing does not vary
// with speed and the low is a saturation voltage rather than a true zero.
constexpr double kHallHighVolts    = 5.0;
constexpr double kHallLowVolts     = 0.15;
constexpr double kHallDuty         = 0.5;
constexpr int    kHallPulsesShown  = 8;
constexpr double kHallWindow       = 0.020;

// Coil primary, one cylinder event.
constexpr double kIgnWindow        = 0.020;
constexpr double kIgnDwellStart    = 0.30;   // fractions of the window
constexpr double kIgnFire          = 0.55;
constexpr double kIgnSpikeWidth    = 0.008;
constexpr double kIgnSaturation    = 0.6;    // driver on: coil primary near ground
constexpr double kIgnPeakVolts     = 330.0;
constexpr double kIgnSparkVolts    = 22.0;   // above battery, while the arc burns
constexpr double kIgnRingVolts     = 55.0;
constexpr double kIgnRingDecay     = 2.2;
constexpr double kIgnRingCycles    = 2.6;

// Injector on a saturated driver, one opening.
constexpr double kInjWindow        = 0.020;
constexpr double kInjOpen          = 0.18;
constexpr double kInjClose         = 0.62;
constexpr double kInjPeakAmps      = 0.62;
constexpr double kInjRiseRate      = 5.0;    // L/R time constants across the pulse
constexpr double kInjPintleAt      = 0.16;   // where the armature moves, as a fraction
constexpr double kInjPintleWidth   = 0.06;
// Deep enough to be a genuine local MINIMUM rather than a flat spot. While the
// pintle is moving, the changing inductance opposes the current's rise hard
// enough to turn it around briefly; a notch that merely slows the climb is the
// under-stated version of this signal and would not read as a notch on screen.
constexpr double kInjPintleDip     = 0.100;
constexpr double kInjHoldFrom      = 0.55;

// Zirconia lambda: a Nernst cell, so it sits near its rails and crosses fast.
constexpr double kLambdaLowVolts   = 0.10;
constexpr double kLambdaHighVolts  = 0.85;
constexpr double kLambdaHz         = 1.2;    // a healthy sensor crosses about this often
constexpr double kLambdaWindow     = 6.0;
constexpr double kLambdaSwitch     = 2.6;    // tanh gain: how hard it snaps between rails

// Hot-wire MAF through a snap throttle.
constexpr double kAfmRestVolts     = 1.45;
constexpr double kAfmPeakVolts     = 3.30;
constexpr double kAfmWindow        = 3.0;
constexpr double kAfmDipFraction   = 0.42;   // manifold filling, after the inrush peak

// Throttle potentiometer, swept open then closed by hand.
constexpr double kTpsClosedVolts   = 0.55;
constexpr double kTpsOpenVolts     = 4.35;
constexpr double kTpsWindow        = 3.0;

// Dither. Present because a perfectly smooth line reads as a drawing rather
// than a signal, and because a reference with no noise at all invites the
// conclusion that a real trace is faulty for having some.
constexpr double kDitherCrank      = 0.012;
constexpr double kDitherHall       = 0.020;
constexpr double kDitherIgnition   = 0.350;
constexpr double kDitherInjector   = 0.006;
constexpr double kDitherLambda     = 0.012;
constexpr double kDitherAfm        = 0.020;
constexpr double kDitherTps        = 0.008;

// A fixed sequence, NOT a random one: the same signal must generate the same
// samples every time or a test cannot assert against it and the trace would
// crawl between redraws. Numerical Recipes' LCG constants.
class JDither {
public:
    explicit JDither(uint32_t seed) : m_state(seed) {}
    // Roughly normal, from the mean of four uniforms — enough for a noise floor
    // and far cheaper than a Box-Muller pair nobody would hear the difference in.
    double operator()(double amplitude) {
        double sum = 0.0;
        for (int i = 0; i < 4; ++i) sum += _next() - 0.5;
        return sum * 0.5 * amplitude;
    }
private:
    double _next() {
        m_state = m_state * 1664525u + 1013904223u;
        return static_cast<double>(m_state >> 8) / static_cast<double>(1u << 24);
    }
    uint32_t m_state;
};

double crankWindowSeconds() {
    // One tooth is one 60th of a revolution.
    const double revsPerSecond = kCrankRpm / 60.0;
    return kCrankTeethShown / (kCrankTeeth * revsPerSecond);
}

} // namespace

JReferenceWaveform::JTrace JReferenceWaveform::generate(JReferenceSignal signal) {
    JTrace t;
    if (signal == JReferenceSignal::None) return t;

    t.samples.reserve(kSamples);
    JDither dither(static_cast<uint32_t>(signal) * 2654435761u + 1u);

    switch (signal) {
        case JReferenceSignal::CrankInductive: {
            t.window  = crankWindowSeconds();
            t.caption = "60-2 wheel at 900 rpm — symmetric either side of zero, "
                        "flat across the gap, taller on the tooth after it";
            for (size_t i = 0; i < kSamples; ++i) {
                // Tooth position, with the gap placed mid-window so both the
                // approach and the recovery are visible.
                const double tooth = kCrankTeethShown * static_cast<double>(i) / kSamples;
                const double gapAt = kCrankTeethShown * 0.5;
                double v;
                if (tooth >= gapAt && tooth < gapAt + kCrankMissing) {
                    v = 0.0;                                  // no tooth, no flux change
                } else {
                    v = kCrankPeakVolts * std::sin(2.0 * M_PI * tooth);
                    if (tooth >= gapAt + kCrankMissing && tooth < gapAt + kCrankMissing + 1.0)
                        v *= kCrankRebuildGain;
                }
                t.samples.push_back(static_cast<float>(v + dither(kDitherCrank)));
            }
            break;
        }
        case JReferenceSignal::CrankHall: {
            t.window  = kHallWindow;
            t.caption = "Open-collector Hall switch — square, rail to rail, "
                        "amplitude independent of speed";
            for (size_t i = 0; i < kSamples; ++i) {
                const double phase = std::fmod(kHallPulsesShown * static_cast<double>(i) / kSamples, 1.0);
                const double v = (phase < kHallDuty) ? kHallLowVolts : kHallHighVolts;
                t.samples.push_back(static_cast<float>(v + dither(kDitherHall)));
            }
            break;
        }
        case JReferenceSignal::PrimaryIgnition: {
            t.window  = kIgnWindow;
            t.caption = "Coil primary — battery, dwell, turn-off spike, spark line, ringing";
            for (size_t i = 0; i < kSamples; ++i) {
                const double f = static_cast<double>(i) / kSamples;
                double v;
                if (f < kIgnDwellStart)                     v = kBatteryVolts;
                else if (f < kIgnFire)                      v = kIgnSaturation;
                else if (f < kIgnFire + kIgnSpikeWidth)     v = kIgnPeakVolts;
                else if (f < kIgnFire + 0.20)               v = kBatteryVolts + kIgnSparkVolts;
                else {
                    // The energy left after the arc quits rings the coil against
                    // its own capacitance and dies away.
                    const double d = (f - (kIgnFire + 0.20)) * 28.0;
                    v = kBatteryVolts + kIgnRingVolts * std::exp(-d * kIgnRingDecay)
                                      * std::cos(2.0 * M_PI * d * kIgnRingCycles);
                }
                t.samples.push_back(static_cast<float>(v + dither(kDitherIgnition)));
            }
            break;
        }
        case JReferenceSignal::InjectorCurrent: {
            t.window  = kInjWindow;
            t.unit    = "A";
            t.caption = "Saturated driver — L/R rise, notch as the pintle lifts, "
                        "hold, sharp collapse";
            for (size_t i = 0; i < kSamples; ++i) {
                const double f = static_cast<double>(i) / kSamples;
                double v = 0.0;
                if (f >= kInjOpen && f <= kInjClose) {
                    const double u = (f - kInjOpen) / (kInjClose - kInjOpen);
                    v = kInjPeakAmps * (1.0 - std::exp(-u * kInjRiseRate));
                    if (u > kInjPintleAt && u < kInjPintleAt + kInjPintleWidth) {
                        // The moving armature briefly opposes the current's rise.
                        v -= kInjPintleDip * std::sin((u - kInjPintleAt) / kInjPintleWidth * M_PI);
                    }
                    if (u > kInjHoldFrom) v = kInjPeakAmps * 0.98;
                }
                t.samples.push_back(static_cast<float>(v + dither(kDitherInjector)));
            }
            break;
        }
        case JReferenceSignal::LambdaZirconia: {
            t.window  = kLambdaWindow;
            t.caption = "Nernst cell — switches hard about stoichiometric, "
                        "rich high and lean low, and does not linger in between";
            const double mid = (kLambdaLowVolts + kLambdaHighVolts) * 0.5;
            const double amp = (kLambdaHighVolts - kLambdaLowVolts) * 0.5;
            for (size_t i = 0; i < kSamples; ++i) {
                const double seconds = kLambdaWindow * static_cast<double>(i) / kSamples;
                const double s = std::tanh(kLambdaSwitch * std::sin(2.0 * M_PI * kLambdaHz * seconds));
                t.samples.push_back(static_cast<float>(mid + amp * s + dither(kDitherLambda)));
            }
            break;
        }
        case JReferenceSignal::AirFlowHotWire: {
            t.window  = kAfmWindow;
            t.caption = "Snap throttle — rest, inrush peak, dip as the manifold fills, "
                        "ramp to full flow, fall back to rest";
            const double span = kAfmPeakVolts - kAfmRestVolts;
            for (size_t i = 0; i < kSamples; ++i) {
                const double f = static_cast<double>(i) / kSamples;
                double v;
                if      (f < 0.22) v = kAfmRestVolts;
                else if (f < 0.30) v = kAfmRestVolts + span * std::pow((f - 0.22) / 0.08, 0.6);
                else if (f < 0.38) v = kAfmPeakVolts - span * kAfmDipFraction * ((f - 0.30) / 0.08);
                else if (f < 0.62) v = kAfmRestVolts + span * (0.58 + 0.42 * (f - 0.38) / 0.24);
                else if (f < 0.72) v = kAfmPeakVolts - span * std::pow((f - 0.62) / 0.10, 0.7);
                else               v = kAfmRestVolts;
                t.samples.push_back(static_cast<float>(v + dither(kDitherAfm)));
            }
            break;
        }
        case JReferenceSignal::ThrottlePosition: {
            t.window  = kTpsWindow;
            t.caption = "Potentiometer swept open and closed — smooth and monotonic, "
                        "with no dropouts on the track";
            const double span = kTpsOpenVolts - kTpsClosedVolts;
            for (size_t i = 0; i < kSamples; ++i) {
                const double f = static_cast<double>(i) / kSamples;
                double v;
                if      (f < 0.15) v = kTpsClosedVolts;
                else if (f < 0.45) v = kTpsClosedVolts + span * (f - 0.15) / 0.30;
                else if (f < 0.55) v = kTpsOpenVolts;
                else if (f < 0.85) v = kTpsOpenVolts - span * (f - 0.55) / 0.30;
                else               v = kTpsClosedVolts;
                t.samples.push_back(static_cast<float>(v + dither(kDitherTps)));
            }
            break;
        }
        case JReferenceSignal::None:
            break;
    }

    if (!t.samples.empty()) {
        t.minValue = t.maxValue = t.samples.front();
        for (float s : t.samples) {
            if (s < t.minValue) t.minValue = s;
            if (s > t.maxValue) t.maxValue = s;
        }
    }
    return t;
}

} // inline namespace jf
