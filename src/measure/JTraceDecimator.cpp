#include "JTraceDecimator.h"
#include <algorithm>
#include <cmath>

inline namespace jf {

void JTraceDecimator::reserve(JPoints& out, float pixelWidth) {
    const size_t columns = static_cast<size_t>(std::max(1.0f, std::ceil(pixelWidth)));
    out.reserve(columns * 2 + 2);
}

float JTraceDecimator::voltsToY(double volts, const JTraceViewport& vp) {
    const double span = static_cast<double>(vp.verticalDivisions) * vp.voltsPerDiv;
    if (span <= 0.0) return vp.y + vp.height * 0.5f;
    double v = volts + vp.offsetVolts;
    if (vp.inverted) v = -v;
    // Screen y grows downward; positive volts go up.
    const double frac = 0.5 - (v / span);
    return static_cast<float>(vp.y + frac * vp.height);
}

double JTraceDecimator::yToVolts(float y, const JTraceViewport& vp) {
    const double span = static_cast<double>(vp.verticalDivisions) * vp.voltsPerDiv;
    if (vp.height <= 0.0f) return 0.0;
    const double frac = (static_cast<double>(y) - vp.y) / vp.height;
    double v = (0.5 - frac) * span;
    if (vp.inverted) v = -v;
    return v - vp.offsetVolts;
}

JTraceDecimator::JPath JTraceDecimator::decimate(const int16_t* samples, size_t sampleTotal,
                                                 float countsToVolts, float zeroOffsetCounts,
                                                 const JTraceViewport& vp, JPoints& out) {
    out.clear();
    if (!samples || sampleTotal == 0 || vp.width <= 0.0f || vp.sampleCount == 0)
        return JPath::Empty;

    const size_t first = std::min(vp.firstSample, sampleTotal);
    const size_t count = std::min(vp.sampleCount, sampleTotal - first);
    if (count == 0) return JPath::Empty;

    const size_t columns = static_cast<size_t>(std::max(1.0f, std::floor(vp.width)));

    auto yOf = [&](int16_t c) {
        return voltsToY((static_cast<double>(c) - zeroOffsetCounts) * countsToVolts, vp);
    };

    if (count <= columns) {
        // Fewer samples than pixels: one vertex each, at sub-pixel x. No
        // decimation artefacts where none are needed.
        const double dx = (count > 1) ? (static_cast<double>(vp.width) / (count - 1)) : 0.0;
        for (size_t i = 0; i < count; ++i)
            out.push_back({ static_cast<float>(vp.x + i * dx), yOf(samples[first + i]) });
        return JPath::Interpolated;
    }

    // More samples than pixels: min/max envelope, one column at a time.
    //
    // The ORDER the two extremes are emitted in matters, and getting it wrong is
    // visible. A fixed max-then-min order chains correctly across a falling edge
    // (high, high -> high, low -> low, low) but backtracks across a rising one
    // (low, low -> high, low -> high, high): the path climbs to the top, drops
    // straight back down, then climbs again, drawing a spurious diagonal that
    // fights the real edge and shimmers as the waveform advances between frames.
    //
    // So each column emits the extreme NEAREST the previous column's exit point
    // first. The polyline then enters a column where the last one left off and
    // leaves from the far side — no backtracking, and edges of both directions
    // draw as one clean vertical.
    bool  havePrev = false;
    float prevY    = 0.0f;

    for (size_t c = 0; c < columns; ++c) {
        const size_t lo = first + (count * c)       / columns;
        size_t       hi = first + (count * (c + 1)) / columns;
        if (hi <= lo) hi = lo + 1;
        if (hi > first + count) hi = first + count;

        int16_t mn = samples[lo], mx = samples[lo];
        for (size_t i = lo + 1; i < hi; ++i) {     // integer compares only
            const int16_t v = samples[i];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }

        // Screen y is inverted relative to counts: the maximum count is the
        // topmost point, so yTop comes from mx.
        const float px   = vp.x + static_cast<float>(c);
        const float yTop = yOf(mx);
        const float yBot = yOf(mn);

        const bool topFirst = !havePrev
                            || std::abs(yTop - prevY) <= std::abs(yBot - prevY);
        if (topFirst) {
            out.push_back({ px, yTop });
            out.push_back({ px, yBot });
            prevY = yBot;
        } else {
            out.push_back({ px, yBot });
            out.push_back({ px, yTop });
            prevY = yTop;
        }
        havePrev = true;
    }
    return JPath::Envelope;
}

} // inline namespace jf
