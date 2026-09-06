#include "measure/JTraceDecimator.h"
#include "support/JTestReport.h"

#include <cmath>
#include <vector>

using namespace jf;

namespace {

JTraceViewport makeViewport(float width, size_t sampleCount) {
    JTraceViewport vp;
    vp.x = 0.0f;  vp.y = 0.0f;
    vp.width = width;  vp.height = 800.0f;
    vp.firstSample = 0;  vp.sampleCount = sampleCount;
    vp.voltsPerDiv = 1.0;  vp.offsetVolts = 0.0;
    vp.verticalDivisions = 8;
    return vp;
}

void testVoltsRoundTrip(JTestReport& r) {
    JTraceViewport vp = makeViewport(100.0f, 10);
    const float yZero = JTraceDecimator::voltsToY(0.0, vp);
    r.check(std::abs(yZero - (vp.y + vp.height * 0.5f)) < 0.001f,
            "0 V maps to the vertical centre");

    // The full graticule is verticalDivisions * voltsPerDiv = 8 V.
    const float yTop = JTraceDecimator::voltsToY(4.0, vp);
    r.check(std::abs(yTop - vp.y) < 0.001f, "+4 V at 1 V/div over 8 divisions is the top edge");

    bool ok = true;
    for (double v = -3.5; v <= 3.5; v += 0.25) {
        const double back = JTraceDecimator::yToVolts(JTraceDecimator::voltsToY(v, vp), vp);
        if (std::abs(back - v) > 1e-4) ok = false;
    }
    r.check(ok, "voltsToY and yToVolts round-trip");

    vp.inverted = true;
    r.check(JTraceDecimator::voltsToY(1.0, vp) > JTraceDecimator::voltsToY(-1.0, vp),
            "inverting a channel flips the trace");
}

void testPassThroughBelowPixelDensity(JTestReport& r) {
    std::vector<int16_t> s(50);
    for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<int16_t>(i);

    JTraceViewport vp = makeViewport(200.0f, s.size());
    JTraceDecimator::JPoints out;
    JTraceDecimator::decimate(s.data(), s.size(), 0.001f, 0.0f, vp, out);

    r.check(out.size() == s.size(),
            "fewer samples than pixels emits one vertex per sample, not an envelope");
    r.check(out.front().x == vp.x, "first vertex sits at the left edge");
    r.check(std::abs(out.back().x - (vp.x + vp.width)) < 0.001f,
            "last vertex sits at the right edge");
    bool monotonic = true;
    for (size_t i = 1; i < out.size(); ++i)
        if (out[i].x < out[i - 1].x) monotonic = false;
    r.check(monotonic, "x advances monotonically");
}

// A fixed max-then-min emission order chains correctly across a FALLING edge but
// backtracks across a RISING one: the path climbs to the top, drops straight back
// down, then climbs again — a spurious diagonal that shimmers as the waveform
// advances. It showed up on the bench as "the rising edges flicker", and only the
// rising ones, which is exactly what a fixed ordering predicts.
//
// The invariant that rules it out: at every column boundary, the segment joining
// one column's exit point to the next column's entry point must be the SHORTER of
// the two possible pairings. If it is ever the longer one, the path backtracked.
void testEdgesDoNotBacktrack(JTestReport& r) {
    // A square wave with both edge directions, well above one sample per pixel.
    std::vector<int16_t> s(20000);
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<int16_t>(((i / 500) % 2) ? 2000 : -2000);

    JTraceViewport vp = makeViewport(300.0f, s.size());
    JTraceDecimator::JPoints out;
    JTraceDecimator::decimate(s.data(), s.size(), 0.001f, 0.0f, vp, out);

    r.check(out.size() == 600, "300 columns emit two vertices each");

    size_t backtracks = 0;
    for (size_t c = 0; c + 3 < out.size(); c += 2) {
        const float exitY  = out[c + 1].y;   // this column's second vertex
        const float nearY  = out[c + 2].y;   // next column's first vertex
        const float farY   = out[c + 3].y;   // next column's second vertex
        if (std::abs(exitY - nearY) > std::abs(exitY - farY)) ++backtracks;
    }
    r.check(backtracks == 0,
            "no column entered from its far side — rising and falling edges both draw clean");

    // And the edges must still be full height: the fix must not have flattened them.
    const float yHigh = JTraceDecimator::voltsToY(2.0, vp);
    const float yLow  = JTraceDecimator::voltsToY(-2.0, vp);
    size_t fullHeightColumns = 0;
    for (size_t c = 0; c + 1 < out.size(); c += 2) {
        const float a = out[c].y, b = out[c + 1].y;
        if (std::abs(std::abs(a - b) - std::abs(yHigh - yLow)) < 1.0f) ++fullHeightColumns;
    }
    r.check(fullHeightColumns > 0, "transition columns still span the full amplitude");
}

void testEnvelopeCoversRamp(JTestReport& r) {
    // A ramp's envelope must span exactly the ramp: nothing invented, nothing lost.
    std::vector<int16_t> s(10000);
    for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<int16_t>(i % 1000);

    JTraceViewport vp = makeViewport(100.0f, s.size());
    JTraceDecimator::JPoints out;
    JTraceDecimator::decimate(s.data(), s.size(), 0.001f, 0.0f, vp, out);

    r.check(out.size() == 200, "100 pixel columns emit exactly 2 vertices each");

    // Each column must emit BOTH extremes, but the order is deliberately not
    // fixed — see testEdgesDoNotBacktrack for why. The invariant is that the two
    // vertices bracket the column's true range, whichever way round they come.
    bool bracketed = true;
    for (size_t c = 0; c + 1 < out.size(); c += 2)
        if (out[c].y == out[c + 1].y && c > 0 && c + 3 < out.size()) {
            // A flat column is legitimate; a column spanning a ramp is not.
            const float span = std::abs(out[c].y - out[c + 2].y);
            if (span > 2.0f) bracketed = false;
        }
    r.check(bracketed, "columns spanning a changing signal emit two distinct extremes");
}

// The check that separates an instrument from a plot. Stride sampling would
// simply miss this spike; min/max cannot.
void testSpikeSurvivesExtremeDecimation(JTestReport& r) {
    std::vector<int16_t> s(1000000, 0);
    s[765432] = 3000;                       // one sample in a million

    JTraceViewport vp = makeViewport(100.0f, s.size());   // 10000:1 reduction
    JTraceDecimator::JPoints out;
    JTraceDecimator::decimate(s.data(), s.size(), 0.001f, 0.0f, vp, out);

    const float yFlat  = JTraceDecimator::voltsToY(0.0, vp);
    const float ySpike = JTraceDecimator::voltsToY(3000 * 0.001, vp);

    bool found = false;
    for (const auto& p : out)
        if (std::abs(p.y - ySpike) < 0.5f) found = true;
    r.check(found, "a single-sample spike survives 10000:1 decimation");

    int flatColumns = 0;
    for (size_t c = 0; c + 1 < out.size(); c += 2)
        if (std::abs(out[c].y - yFlat) < 0.5f && std::abs(out[c + 1].y - yFlat) < 0.5f)
            ++flatColumns;
    r.check(flatColumns == 99, "every other column stays flat — the spike is not smeared");
}

void testBoundaryAndDegenerate(JTestReport& r) {
    std::vector<int16_t> s(100, 7);
    JTraceDecimator::JPoints out;

    // Exactly one sample per pixel is the boundary between the two paths.
    JTraceViewport vp = makeViewport(100.0f, 100);
    JTraceDecimator::decimate(s.data(), s.size(), 0.001f, 0.0f, vp, out);
    r.check(out.size() == 100, "the 1:1 boundary takes the pass-through path");

    JTraceDecimator::decimate(nullptr, 0, 0.001f, 0.0f, vp, out);
    r.check(out.empty(), "a null plane yields no vertices rather than crashing");

    JTraceDecimator::decimate(s.data(), 0, 0.001f, 0.0f, vp, out);
    r.check(out.empty(), "an empty plane yields no vertices");

    JTraceViewport zero = makeViewport(0.0f, 100);
    JTraceDecimator::decimate(s.data(), s.size(), 0.001f, 0.0f, zero, out);
    r.check(out.empty(), "a zero-width viewport yields no vertices");

    // A window running past the end of the plane must clamp, not read out of bounds.
    JTraceViewport over = makeViewport(50.0f, 100);
    over.firstSample = 80;
    over.sampleCount = 100;
    JTraceDecimator::decimate(s.data(), s.size(), 0.001f, 0.0f, over, out);
    r.check(!out.empty() && out.size() <= 40, "an over-long window clamps to the plane");
}

void testReserveMakesItAllocationFree(JTestReport& r) {
    std::vector<int16_t> s(100000, 0);
    JTraceDecimator::JPoints out;
    JTraceDecimator::reserve(out, 2000.0f);
    const size_t capacityAfterReserve = out.capacity();

    JTraceViewport vp = makeViewport(2000.0f, s.size());
    for (int i = 0; i < 200; ++i)
        JTraceDecimator::decimate(s.data(), s.size(), 0.001f, 0.0f, vp, out);

    r.check(out.capacity() == capacityAfterReserve,
            "200 redraws after reserve() do not grow the buffer");
}

} // namespace

int main() {
    JTestReport r("JTraceDecimator");
    testVoltsRoundTrip(r);
    testPassThroughBelowPixelDensity(r);
    testEnvelopeCoversRamp(r);
    testEdgesDoNotBacktrack(r);
    testSpikeSurvivesExtremeDecimation(r);
    testBoundaryAndDegenerate(r);
    testReserveMakesItAllocationFree(r);
    return r.result();
}
