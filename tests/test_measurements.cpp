#include "measure/JMeasurementEngine.h"
#include "support/JTestReport.h"
#include "support/JWaveformFixture.h"

#include <cmath>
#include <string>

using namespace jf;

namespace {

double get(const JScopeFrame& f, JMeasurementKind k) {
    return JMeasurementEngine::measure(k, f, 0).value;
}
bool valid(const JScopeFrame& f, JMeasurementKind k) {
    return JMeasurementEngine::measure(k, f, 0).valid;
}

// Tolerances are relative, because the quantisation floor of a 12-bit device is
// what actually limits every one of these, not the algorithm.
bool near(double got, double want, double tolerance) {
    if (want == 0.0) return std::abs(got) <= tolerance;
    return std::abs(got - want) / std::abs(want) <= tolerance;
}

void testSine(JTestReport& r) {
    // 1 kHz, 2 V peak, 20 periods at 100 kSa/s.
    JScopeFrame f;
    JWaveformFixture::sine(f, 2000, 1.0e-5, 1000.0, 2.0);

    r.check(near(get(f, JMeasurementKind::Vpp), 4.0, 0.01),  "sine Vpp is twice the amplitude");
    r.check(near(get(f, JMeasurementKind::Vmax), 2.0, 0.01), "sine Vmax is the amplitude");
    r.check(near(get(f, JMeasurementKind::Vmin), -2.0, 0.01),"sine Vmin is minus the amplitude");
    r.check(near(get(f, JMeasurementKind::Vavg), 0.0, 0.01), "sine Vavg is zero about zero");
    // Closed form: Vrms = A / sqrt(2).
    r.check(near(get(f, JMeasurementKind::Vrms), 2.0 / std::sqrt(2.0), 0.01),
            "sine Vrms is amplitude over root two");
    r.check(near(get(f, JMeasurementKind::Frequency), 1000.0, 0.01), "sine frequency");
    r.check(near(get(f, JMeasurementKind::Period), 1.0e-3, 0.01),    "sine period");
}

void testSineWithOffset(JTestReport& r) {
    JScopeFrame f;
    JWaveformFixture::sine(f, 2000, 1.0e-5, 1000.0, 1.0, 0.5);

    r.check(near(get(f, JMeasurementKind::Vavg), 0.5, 0.02), "offset shows up in Vavg");
    r.check(near(get(f, JMeasurementKind::Vmax), 1.5, 0.02), "offset shifts Vmax");
    r.check(near(get(f, JMeasurementKind::Vmin), -0.5, 0.05),"offset shifts Vmin");
    r.check(near(get(f, JMeasurementKind::Vpp), 2.0, 0.02),  "Vpp is unaffected by offset");
    // The trigger level for timing is derived from the signal's own levels, so a
    // DC offset must not break frequency.
    r.check(near(get(f, JMeasurementKind::Frequency), 1000.0, 0.01),
            "frequency survives a DC offset");
}

void testSquare(JTestReport& r) {
    // 2 kHz, 1 V amplitude, 40 periods at 200 kSa/s.
    JScopeFrame f;
    JWaveformFixture::square(f, 4000, 5.0e-6, 2000.0, 1.0, 0.5);

    r.check(near(get(f, JMeasurementKind::Vpp), 2.0, 0.01), "square Vpp");
    // A 50% duty square about zero has Vrms equal to its amplitude.
    r.check(near(get(f, JMeasurementKind::Vrms), 1.0, 0.02), "square Vrms equals its amplitude");
    r.check(near(get(f, JMeasurementKind::Frequency), 2000.0, 0.02), "square frequency");
    r.check(near(get(f, JMeasurementKind::DutyCycle), 50.0, 0.05),   "square duty is 50%");
    r.check(near(get(f, JMeasurementKind::Top), 1.0, 0.02),  "top is the high level");
    r.check(near(get(f, JMeasurementKind::Base), -1.0, 0.02),"base is the low level");
}

void testAsymmetricDuty(JTestReport& r) {
    JScopeFrame f;
    JWaveformFixture::square(f, 4000, 5.0e-6, 1000.0, 1.0, 0.25);
    r.check(near(get(f, JMeasurementKind::DutyCycle), 25.0, 0.10), "a 25% duty reads as 25%");
}

// The reason top/base come from a histogram rather than from max/min: measured
// against the extremes, an overshooting edge reports a rise time that is
// consistently too short, and it looks entirely plausible.
void testRiseTime(JTestReport& r) {
    // 1 kHz, rise occupying 10% of the period. The 10-90 portion of a linear
    // ramp is 0.8 of that, so 80 us.
    JScopeFrame f;
    JWaveformFixture::trapezoid(f, 20000, 1.0e-6, 1000.0, 1.0, 0.10);

    const double want = 0.8 * 0.10 * 1.0e-3;
    r.check(valid(f, JMeasurementKind::RiseTime), "a trapezoid has a measurable rise time");
    r.check(near(get(f, JMeasurementKind::RiseTime), want, 0.05),
            "rise time is the 10-90 portion of the constructed edge");
    r.check(valid(f, JMeasurementKind::FallTime), "and a measurable fall time");
    r.check(near(get(f, JMeasurementKind::FallTime), want, 0.05),
            "fall time matches a symmetric edge");
}

// An invalid result must say so rather than produce a plausible number. A record
// holding less than a full cycle has no frequency, and inventing one would be a
// fabrication the user cannot spot.
void testInsufficientDataIsInvalid(JTestReport& r) {
    JScopeFrame f;
    // A quarter of one cycle: amplitude is measurable, timing is not.
    JWaveformFixture::sine(f, 250, 1.0e-5, 100.0, 1.0);

    r.check(valid(f, JMeasurementKind::Vpp), "amplitude is still measurable");
    r.check(!valid(f, JMeasurementKind::Frequency),
            "frequency is INVALID with less than one full cycle, not guessed");
    r.check(!valid(f, JMeasurementKind::Period), "period is invalid too");
}

// ONE GLITCH MUST NOT DESTROY THE PERIOD. On the bench a 1 kHz square produced
// rising edges at 477, 1477, 2477, 3478, 4478 us — textbook — plus a single
// spurious pair 0.78 us wide inside one high period. Computing the period as
// (last - first) / (count - 1) counted that glitch as a whole extra cycle and
// reported a confident 1.25 kHz; the duty, taken from the first rise/fall pair
// it found, reported 0.1 % on a visibly even square.
void testGlitchDoesNotDestroyTiming(JTestReport& r) {
    // 1 kHz square, 5 ms, 4000 samples — the exact bench configuration.
    JScopeFrame f;
    JWaveformFixture::square(f, 4000, 1.25e-6, 1000.0, 2.5, 0.5, 0.0005, 2.5);

    // Drop a two-sample notch into the middle of one high period, which is what
    // the instrument actually delivered.
    const uint32_t glitch = 1983;      // inside the third high period
    f.plane(0)[glitch]     = JWaveformFixture::toCounts(0.0);
    f.plane(0)[glitch + 1] = JWaveformFixture::toCounts(0.0);

    const double freq = get(f, JMeasurementKind::Frequency);
    const double duty = get(f, JMeasurementKind::DutyCycle);

    r.check(near(freq, 1000.0, 0.02),
            "a single glitch leaves the frequency at 1 kHz, not 1.25 kHz");
    r.check(near(duty, 50.0, 0.10),
            "and leaves the duty near 50 %, not at the width of the glitch");

    // The same waveform with no glitch must of course still be right.
    JScopeFrame clean;
    JWaveformFixture::square(clean, 4000, 1.25e-6, 1000.0, 2.5, 0.5, 0.0005, 2.5);
    r.check(near(get(clean, JMeasurementKind::Frequency), 1000.0, 0.01),
            "the unglitched waveform is unaffected by the change");
    r.check(near(get(clean, JMeasurementKind::DutyCycle), 50.0, 0.05),
            "and its duty is still 50 %");
}

// ONE OUTLIER MUST NOT DESTROY THE LEVELS. The 1008C produces occasional
// single-sample dropouts to ADC zero, which at the widest range is -21 V. That
// stretches the amplitude histogram so that BOTH real levels fall in its upper
// half, and a mode search that splits the range at the midpoint then returns the
// outlier's own bin as the base.
//
// On the bench that reported Base -21 V and Top -49.5 mV for a clean 0-5 V
// square, and took Amplitude and every timing measurement with it — the mid
// level it implied is never crossed, so there were no crossings at all.
void testOutlierDoesNotDestroyLevels(JTestReport& r) {
    // 0 to 2 V, which fits the fixture's +/-2.047 V range. The bench signal was
    // 0-5 V on an instrument whose range holds it; the shape of the failure is
    // what matters here, not the voltage.
    JScopeFrame f;
    JWaveformFixture::square(f, 4000, 2.5e-6, 1000.0, 1.0, 0.5, 0.001, 1.0);

    // A single-sample dropout to the bottom of the ADC range.
    f.plane(0)[1234] = JWaveformFixture::kCountsMin;

    r.check(near(get(f, JMeasurementKind::Top), 2.0, 0.05),
            "Top is still the high level, not dragged to the outlier");
    r.check(near(get(f, JMeasurementKind::Base), 0.0, 0.10),
            "Base is still the low level");
    r.check(near(get(f, JMeasurementKind::Amplitude), 2.0, 0.05),
            "and the amplitude between them is right");

    // Timing survives, because the mid level is still between the real levels.
    r.check(valid(f, JMeasurementKind::Frequency),
            "timing is still measurable with an outlier present");
    r.check(near(get(f, JMeasurementKind::Frequency), 1000.0, 0.02),
            "and correct");
    r.check(near(get(f, JMeasurementKind::DutyCycle), 50.0, 0.10), "duty too");

    // Vmin and Vpp SHOULD report the outlier: they are the extremes of what was
    // actually sampled, and a scope showing a dropout is telling the truth.
    r.check(get(f, JMeasurementKind::Vmin) < -1.0,
            "Vmin still reports the real extreme — the dropout is genuine data");
}

void testDcIsHandled(JTestReport& r) {
    JScopeFrame f;
    JWaveformFixture::dc(f, 1000, 1.0e-5, 1.234);

    r.check(near(get(f, JMeasurementKind::Vavg), 1.234, 0.01), "DC average");
    r.check(near(get(f, JMeasurementKind::Vrms), 1.234, 0.01), "DC rms equals its level");
    r.check(near(get(f, JMeasurementKind::Vpp), 0.0, 0.002),   "DC has no peak-to-peak");
    r.check(!valid(f, JMeasurementKind::Frequency), "DC has no frequency");
}

// Noise must not manufacture edges. Without hysteresis a signal grazing its own
// threshold produces a burst of spurious crossings and a frequency that is
// wildly wrong rather than slightly wrong.
void testNoiseDoesNotFabricateEdges(JTestReport& r) {
    JScopeFrame f;
    JWaveformFixture::sine(f, 4000, 1.0e-5, 500.0, 1.0);

    // Add noise at 5% of amplitude, well inside the hysteresis band.
    int16_t* p = f.plane(0);
    uint32_t seed = 12345;
    for (uint32_t i = 0; i < f.header.sampleCount; ++i) {
        seed = seed * 1103515245u + 12345u;
        const int jitter = static_cast<int>((seed >> 16) % 101) - 50;   // +/-50 counts = 50 mV
        p[i] = static_cast<int16_t>(p[i] + jitter);
    }
    r.check(near(get(f, JMeasurementKind::Frequency), 500.0, 0.05),
            "frequency survives noise at 5% of amplitude");
}

void testWindowRestrictsTheMeasurement(JTestReport& r) {
    // Two halves at different amplitudes; measuring a window must see only that
    // window. This is the same code path the cursors use.
    JScopeFrame f;
    JWaveformFixture::prepare(f, 2000, 1.0e-5);
    int16_t* p = f.plane(0);
    // Both levels must sit inside the fixture's +/-2.048 V range, or the ADC
    // model clips them and the expected average is not what the test assumes.
    for (uint32_t i = 0; i < 1000; ++i)    p[i] = JWaveformFixture::toCounts(0.5);
    for (uint32_t i = 1000; i < 2000; ++i) p[i] = JWaveformFixture::toCounts(1.5);

    const double all   = JMeasurementEngine::measure(JMeasurementKind::Vavg, f, 0).value;
    const double first = JMeasurementEngine::measure(JMeasurementKind::Vavg, f, 0, {0, 1000}).value;
    const double last  = JMeasurementEngine::measure(JMeasurementKind::Vavg, f, 0, {1000, 1000}).value;

    r.check(near(all,   1.0, 0.01), "the whole record averages both halves");
    r.check(near(first, 0.5, 0.01), "a window over the first half sees only it");
    r.check(near(last,  1.5, 0.01), "a window over the second half sees only it");
}

void testDegenerateInputs(JTestReport& r) {
    JScopeFrame empty;
    const auto results = JMeasurementEngine::measureAll(empty, 0);
    bool allInvalid = !results.empty();
    for (const auto& m : results) if (m.valid) allInvalid = false;
    r.check(allInvalid, "an empty frame yields all-invalid results rather than crashing");

    JScopeFrame f;
    JWaveformFixture::dc(f, 100, 1.0e-5, 0.0);
    r.check(!JMeasurementEngine::measure(JMeasurementKind::Vpp, f, 9).valid,
            "a nonexistent plane is invalid, not out of bounds");
}

} // namespace

int main() {
    JTestReport r("JMeasurementEngine");
    testSine(r);
    testSineWithOffset(r);
    testSquare(r);
    testAsymmetricDuty(r);
    testRiseTime(r);
    testInsufficientDataIsInvalid(r);
    testGlitchDoesNotDestroyTiming(r);
    testOutlierDoesNotDestroyLevels(r);
    testDcIsHandled(r);
    testNoiseDoesNotFabricateEdges(r);
    testWindowRestrictsTheMeasurement(r);
    testDegenerateInputs(r);
    return r.result();
}
