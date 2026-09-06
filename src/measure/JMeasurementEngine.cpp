#include "JMeasurementEngine.h"

#include "scope/JScopeLog.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// The 10% and 90% points a rise time is defined between.
constexpr double kRiseLow  = 0.10;
constexpr double kRiseHigh = 0.90;

struct JPlaneView {
    const int16_t* samples{nullptr};
    size_t         count{0};
    float          countsToVolts{0.0f};
    float          zeroOffsetCounts{0.0f};
    double         sampleInterval{0.0};
    bool           valid{false};

    double volts(size_t i) const {
        return (static_cast<double>(samples[i]) - zeroOffsetCounts) * countsToVolts;
    }
};

JPlaneView viewOf(const JScopeFrame& f, uint8_t plane, JMeasurementEngine::JWindow w) {
    JPlaneView v;
    if (plane >= f.header.channelCount || f.header.sampleCount == 0) return v;

    const size_t total = f.header.sampleCount;
    const size_t first = std::min(w.first, total);
    const size_t count = (w.count == 0) ? (total - first) : std::min(w.count, total - first);
    if (count == 0) return v;

    v.samples          = f.plane(plane) + first;
    v.count            = count;
    v.countsToVolts    = f.header.countsToVolts[plane];
    v.zeroOffsetCounts = f.header.zeroOffsetCounts[plane];
    v.sampleInterval   = f.header.sampleInterval;
    v.valid            = true;
    return v;
}

// The time at which an edge crosses `level`, searched OUTWARD from the edge's
// midpoint: backward for the level it leaves, forward for the one it reaches.
//
// Searching forward from the midpoint for both — the obvious way to write it —
// finds the 10% point of the NEXT edge, because by the midpoint this edge is
// already past its own. That reports a rise time spanning most of a cycle.
bool crossTimeBefore(const JPlaneView& v, size_t midIndex, double level, bool rising,
                     double& outTime) {
    for (size_t i = midIndex; i >= 1; --i) {
        const double a = v.volts(i - 1), b = v.volts(i);
        // Walking backwards, the edge still crosses in the forward sense.
        const bool hit = rising ? (a <= level && b > level) : (a >= level && b < level);
        if (!hit) continue;
        const double denom = b - a;
        const double frac  = (denom != 0.0) ? (level - a) / denom : 0.0;
        outTime = (static_cast<double>(i - 1) + frac) * v.sampleInterval;
        return true;
    }
    return false;
}

bool crossTimeAfter(const JPlaneView& v, size_t fromIndex, double level, bool rising,
                    double& outTime) {
    for (size_t i = std::max<size_t>(fromIndex, 1); i < v.count; ++i) {
        const double a = v.volts(i - 1), b = v.volts(i);
        const bool hit = rising ? (a < level && b >= level) : (a > level && b <= level);
        if (!hit) continue;
        const double denom = b - a;
        const double frac  = (denom != 0.0) ? (level - a) / denom : 0.0;
        outTime = (static_cast<double>(i - 1) + frac) * v.sampleInterval;
        return true;
    }
    return false;
}

} // namespace

std::vector<JMeasurementResult>
JMeasurementEngine::measureAll(const JScopeFrame& frame, uint8_t plane, JWindow window) {
    std::vector<JMeasurementResult> out;
    out.reserve(static_cast<size_t>(JMeasurementKind::Count_));

    const JPlaneView v = viewOf(frame, plane, window);
    if (!v.valid) {
        for (int k = 0; k < static_cast<int>(JMeasurementKind::Count_); ++k)
            out.push_back(JMeasurementResult::invalid(static_cast<JMeasurementKind>(k)));
        return out;
    }

    // ---- amplitude, in one integer pass over the counts ----------------------
    int16_t mnC = v.samples[0], mxC = v.samples[0];
    double  sum = 0.0, sumSq = 0.0;
    for (size_t i = 0; i < v.count; ++i) {
        const int16_t c = v.samples[i];
        mnC = std::min(mnC, c);
        mxC = std::max(mxC, c);
        const double d = static_cast<double>(c) - v.zeroOffsetCounts;
        sum   += d;
        sumSq += d * d;
    }
    const double vMin = mnC * 1.0;
    const double vMax = mxC * 1.0;
    const double volMin = (vMin - v.zeroOffsetCounts) * v.countsToVolts;
    const double volMax = (vMax - v.zeroOffsetCounts) * v.countsToVolts;
    const double n      = static_cast<double>(v.count);
    const double avg    = (sum / n) * v.countsToVolts;
    // The transform is linear, so rms(volts) = |countsToVolts| * rms(counts-zero)
    // exactly — no need to convert every sample first.
    const double rms    = std::sqrt(sumSq / n) * std::abs(v.countsToVolts);

    out.push_back(JMeasurementResult::of(JMeasurementKind::Vmax, volMax));
    out.push_back(JMeasurementResult::of(JMeasurementKind::Vmin, volMin));
    out.push_back(JMeasurementResult::of(JMeasurementKind::Vpp,  volMax - volMin));
    out.push_back(JMeasurementResult::of(JMeasurementKind::Vavg, avg));
    out.push_back(JMeasurementResult::of(JMeasurementKind::Vrms, rms));

    // ---- reference levels ----------------------------------------------------
    const JEdgeDetector::JLevels lv =
        JEdgeDetector::findLevels(v.samples, v.count, v.countsToVolts, v.zeroOffsetCounts);
    if (lv.valid) {
        out.push_back(JMeasurementResult::of(JMeasurementKind::Top,       lv.top));
        out.push_back(JMeasurementResult::of(JMeasurementKind::Base,      lv.base));
        out.push_back(JMeasurementResult::of(JMeasurementKind::Amplitude, lv.amplitude()));
    } else {
        out.push_back(JMeasurementResult::invalid(JMeasurementKind::Top));
        out.push_back(JMeasurementResult::invalid(JMeasurementKind::Base));
        out.push_back(JMeasurementResult::invalid(JMeasurementKind::Amplitude));
    }

    // ---- timing --------------------------------------------------------------
    const double span = lv.valid ? lv.amplitude() : (volMax - volMin);
    const double mid  = lv.valid ? lv.mid() : (volMax + volMin) * 0.5;
    const double hyst = span * JEdgeDetector::kDefaultHysteresisFraction;

    const auto crossings = JEdgeDetector::findCrossings(
        v.samples, v.count, v.countsToVolts, v.zeroOffsetCounts,
        v.sampleInterval, mid, hyst);

    // A period needs two crossings of the SAME direction. Fewer than that means
    // the record does not contain a full cycle, and the honest answer is that
    // there is no frequency here — not a number derived from half a wave.
    std::vector<double> risingTimes, fallingTimes;
    for (const auto& c : crossings)
        (c.rising ? risingTimes : fallingTimes).push_back(c.time);

    // MEDIAN of the intervals, not the mean, and not (last - first) / (n - 1).
    //
    // A single glitch destroys both of those. On the bench a 1 kHz square gave
    // rising edges at 477, 1477, 2477, 3478, 4478 us — textbook — plus one
    // spurious pair 0.78 us wide in the middle of a high period. Spanning first
    // to last and dividing by the count treats that glitch as a whole extra
    // cycle: 1000 us becomes 800, and the display reads a confident 1.25 kHz.
    // A mean over the intervals fails the same way.
    //
    // The median does not care. Half the intervals would have to be spurious
    // before it moved, and a signal that noisy has no period worth reporting.
    auto medianInterval = [](const std::vector<double>& times) -> double {
        if (times.size() < 2) return 0.0;
        std::vector<double> gaps;
        gaps.reserve(times.size() - 1);
        for (size_t i = 1; i < times.size(); ++i) gaps.push_back(times[i] - times[i - 1]);
        std::sort(gaps.begin(), gaps.end());
        const size_t mid = gaps.size() / 2;
        return (gaps.size() % 2) ? gaps[mid] : (gaps[mid - 1] + gaps[mid]) * 0.5;
    };

    double period = 0.0;
    bool   havePeriod = false;
    const std::vector<double>& ref = (risingTimes.size() >= 2) ? risingTimes : fallingTimes;
    if (ref.size() >= 2) {
        period     = medianInterval(ref);
        havePeriod = period > 0.0;
    }

    if (havePeriod) {
        out.push_back(JMeasurementResult::of(JMeasurementKind::Period,    period));
        out.push_back(JMeasurementResult::of(JMeasurementKind::Frequency, 1.0 / period));
    } else {
        out.push_back(JMeasurementResult::invalid(JMeasurementKind::Period));
        out.push_back(JMeasurementResult::invalid(JMeasurementKind::Frequency));
    }

    // Duty needs a rising edge followed by a falling one within the same cycle.
    // Duty takes the median high time for the same reason: the first rise/fall
    // pair is one sample of a noisy quantity, and a glitch landing on it sets
    // the reading outright. The bench case reported 0.1 % duty on a visibly
    // even square, because the pair it happened to pick was a 0.78 us glitch.
    bool haveDuty = false;
    double duty = 0.0;
    if (havePeriod && !risingTimes.empty() && !fallingTimes.empty()) {
        std::vector<double> highTimes;
        highTimes.reserve(risingTimes.size());
        for (double rise : risingTimes) {
            const auto fall = std::find_if(fallingTimes.begin(), fallingTimes.end(),
                                           [rise](double f) { return f > rise; });
            if (fall == fallingTimes.end()) continue;
            const double high = *fall - rise;
            // A "high time" longer than a period is a missed falling edge, not a
            // measurement.
            if (high > 0.0 && high < period) highTimes.push_back(high);
        }
        if (!highTimes.empty()) {
            std::sort(highTimes.begin(), highTimes.end());
            const size_t mid = highTimes.size() / 2;
            const double medianHigh = (highTimes.size() % 2)
                ? highTimes[mid] : (highTimes[mid - 1] + highTimes[mid]) * 0.5;
            duty = medianHigh / period * 100.0;
            haveDuty = (duty > 0.0 && duty < 100.0);
        }
    }
    out.push_back(haveDuty ? JMeasurementResult::of(JMeasurementKind::DutyCycle, duty)
                           : JMeasurementResult::invalid(JMeasurementKind::DutyCycle));

    // Rise and fall between the 10% and 90% points of top and base — which is
    // why the histogram levels matter: measured against max and min instead, an
    // overshooting edge reports a rise time that is consistently too short.
    const double lowLevel  = (lv.valid ? lv.base : volMin) + span * kRiseLow;
    const double highLevel = (lv.valid ? lv.base : volMin) + span * kRiseHigh;

    auto edgeTime = [&](bool rising) -> JMeasurementResult {
        const auto& times = rising ? risingTimes : fallingTimes;
        if (times.empty() || span <= 0.0)
            return JMeasurementResult::invalid(rising ? JMeasurementKind::RiseTime
                                                      : JMeasurementKind::FallTime);
        // From the edge's midpoint, walk BACK to the level it leaves and FORWARD
        // to the one it reaches — the two ends of this edge, not the next one's.
        const size_t mid = std::min<size_t>(
            static_cast<size_t>(times.front() / v.sampleInterval), v.count - 1);
        double t1 = 0.0, t2 = 0.0;
        const bool a = crossTimeBefore(v, mid, rising ? lowLevel  : highLevel, rising, t1);
        const bool b = crossTimeAfter (v, mid, rising ? highLevel : lowLevel,  rising, t2);
        if (!a || !b || t2 <= t1)
            return JMeasurementResult::invalid(rising ? JMeasurementKind::RiseTime
                                                      : JMeasurementKind::FallTime);
        return JMeasurementResult::of(rising ? JMeasurementKind::RiseTime
                                             : JMeasurementKind::FallTime, t2 - t1);
    };
    out.push_back(edgeTime(true));
    out.push_back(edgeTime(false));

    return out;
}

JMeasurementResult JMeasurementEngine::measure(JMeasurementKind kind,
                                               const JScopeFrame& frame, uint8_t plane,
                                               JWindow window) {
    for (const JMeasurementResult& r : measureAll(frame, plane, window))
        if (r.kind == kind) return r;
    return JMeasurementResult::invalid(kind);
}

} // inline namespace jf
