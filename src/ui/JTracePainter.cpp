#include "JTracePainter.h"

#include <j/core/JTextHelper.h>
#include "scope/JScopeLog.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

void JTracePainter::paint(JVectorCanvas& canvas, const JScopeFrame& frame, uint8_t plane,
                          const JTraceViewport& vp, const JScopeTheme& theme,
                          bool focused, JTraceDecimator::JPoints& scratch) {
    if (plane >= frame.header.channelCount) return;

    const JTraceDecimator::JPath path =
        JTraceDecimator::decimate(frame.plane(plane), frame.header.sampleCount,
                                  frame.header.countsToVolts[plane],
                                  frame.header.zeroOffsetCounts[plane],
                                  vp, scratch);
    if (scratch.size() < 2) return;

    // An envelope is axis-aligned, so anti-aliasing it adds two gradient quads
    // per segment for no visible gain. An interpolated trace is diagonal and
    // needs them. Set per trace, before the geometry is generated.
    canvas.setAntiAlias(path == JTraceDecimator::JPath::Envelope
                            ? theme.envelopeAntiAliasPixels
                            : theme.traceAntiAliasPixels);

    const JColor color = theme.traceColor(frame.header.channelIds[plane]);
    const float width  = focused ? theme.traceWidthFocused : theme.traceWidth;

    if (path == JTraceDecimator::JPath::Envelope) {
        paintEnvelope(canvas, scratch, color, width);
    } else {
        paintInterpolated(canvas, scratch, color, width, theme);
    }

    JLOGC(JScopeLog::kTrace, JLogLevel::Trace)
        << "plane " << int(plane) << " (CH" << int(frame.header.channelIds[plane] + 1) << "): "
        << frame.header.sampleCount << " samples -> " << scratch.size() << " vertices"
        << (path == JTraceDecimator::JPath::Envelope ? " (envelope, no AA)"
                                                     : " (interpolated, AA)");
}

void JTracePainter::paintInterpolated(JVectorCanvas& canvas,
                                      const JTraceDecimator::JPoints& pts,
                                      const JColor& color, float width,
                                      const JScopeTheme& theme) {
    // Stroked in RUNS, broken wherever the path doubles back on itself.
    //
    // A miter join's outer point sits on the bisector, at a distance that grows
    // without bound as the turn approaches 180 degrees. Undersampling produces
    // exactly that: at the slowest timebase a 1 kHz square is eight samples per
    // cycle, so one sample landing mid-edge makes a single-sample V, and the
    // join at its apex throws a sub-pixel spike tens of pixels away from the
    // trace. It rasterises as a dotted diagonal ghost sitting in empty space,
    // which is what the bench saw and what disabling anti-aliasing hid.
    //
    // Breaking the run ends both segments with a butt cap instead of a join, so
    // there is no bisector to run away with. The visible difference at a genuine
    // reversal is a square end rather than a point, which is what a reversal
    // looks like anyway.
    size_t runStart = 0;
    JTraceDecimator::JPoints run;

    const auto flush = [&](size_t endExclusive) {
        if (endExclusive - runStart < 2) return;
        run.assign(pts.begin() + static_cast<long>(runStart),
                   pts.begin() + static_cast<long>(endExclusive));
        canvas.strokePolyline(run, width, JPaint::solid(color), /*closed=*/false,
                              JLineCap::Butt);
    };

    for (size_t i = 1; i + 1 < pts.size(); ++i) {
        const float ax = pts[i].x - pts[i - 1].x, ay = pts[i].y - pts[i - 1].y;
        const float bx = pts[i + 1].x - pts[i].x, by = pts[i + 1].y - pts[i].y;
        const float la = std::sqrt(ax * ax + ay * ay);
        const float lb = std::sqrt(bx * bx + by * by);
        if (la <= 0.0f || lb <= 0.0f) continue;
        // cos of the turn: -1 is a full reversal, +1 is straight on.
        const float dot = (ax * bx + ay * by) / (la * lb);
        if (dot > -theme.traceMiterReversalCosine) continue;
        flush(i + 1);
        runStart = i;
    }
    flush(pts.size());
}

void JTracePainter::paintEnvelope(JVectorCanvas& canvas, const JTraceDecimator::JPoints& columns,
                                  const JColor& color, float width) {
    if (columns.size() < 2) return;

    const JPaint paint = JPaint::solid(color);
    const float  half  = width * 0.5f;

    // Each pair of vertices is one column's two extremes. Bars are extended to
    // reach the previous column's exit value so that a step between columns is
    // bridged — without that a fast edge would leave a gap between two bars that
    // do not overlap in value.
    bool  havePrev = false;
    float prevExit = 0.0f;

    for (size_t i = 0; i + 1 < columns.size(); i += 2) {
        const float x  = columns[i].x;
        float lo = std::min(columns[i].y, columns[i + 1].y);
        float hi = std::max(columns[i].y, columns[i + 1].y);
        if (havePrev) {
            lo = std::min(lo, prevExit);
            hi = std::max(hi, prevExit);
        }
        prevExit = columns[i + 1].y;
        havePrev = true;

        // Snap to whole pixels so the bar covers the same columns every frame.
        // A 1.5px line drawn at a fractional edge alternates between one and two
        // covered pixels as the trigger jitters, which reads as flicker.
        const float x0 = std::round(x - half);
        const float w  = std::max(1.0f, std::round(width));
        canvas.fillRect(x0, lo, w, std::max(w, hi - lo), paint);
    }
}

JRect JTracePainter::levelBadgeRect(float edgeX, float y, const JScopeTheme& theme) {
    const float w = theme.markerBadgeWidth;
    const float h = theme.markerBadgeHeight;
    // The badge hangs off the LEFT of edgeX so its point lands exactly on the
    // graticule edge without any of it covering the grid.
    return JRect{ edgeX - w, y - h * 0.5f, w, h };
}

JRect JTracePainter::paintLevelBadge(JVectorCanvas& canvas, float edgeX, float y,
                                     const JColor& color, const JScopeTheme& theme,
                                     bool active, bool clampedOffscreen) {
    const JRect r = levelBadgeRect(edgeX, y, theme);
    const float bodyRight = r.x + r.width - theme.markerBadgePoint;
    const float emph = theme.markerEmphasisWidth;

    // A home-plate pentagon: a body carrying the label, and a full-height
    // triangular point whose tip is the level. Full height rather than a small
    // tab because that is what makes the marker readable as ONE object at
    // fifteen pixels — a tab reads as a separate arrow that happens to sit near
    // a box.
    canvas.beginPath();
    canvas.moveTo(r.x, r.y);
    canvas.lineTo(bodyRight, r.y);
    canvas.lineTo(r.x + r.width, y);
    canvas.lineTo(bodyRight, r.y + r.height);
    canvas.lineTo(r.x, r.y + r.height);
    canvas.close();
    canvas.fill(JPaint::solid(color));

    // Under the pointer: an outline, so which of several adjacent badges is
    // being dragged is never in doubt.
    if (active)
        canvas.strokeRect(r.x - emph, r.y - emph, r.width + emph * 2.0f,
                          r.height + emph * 2.0f, emph, JPaint::solid(theme.readoutText));

    // Clamped to the edge because the level itself is off-screen: a bar down the
    // outer side says "further this way", rather than leaving it looking like a
    // level that happens to sit exactly on the boundary.
    if (clampedOffscreen)
        canvas.drawLine(r.x, r.y, r.x, r.y + r.height, emph,
                        JPaint::solid(theme.readoutText));

    return r;
}

float JTracePainter::legendChipWidth(const std::string& detail, const JScopeTheme& theme) {
    return theme.legendNameWidth
         + JTextHelper::measureWidth(detail) + theme.legendChipPadding * 2.0f;
}

float JTracePainter::paintLegendChip(JVectorCanvas& canvas,
                                     float x, float y, float height,
                                     const std::string& detail,
                                     const JColor& channelColour, const JScopeTheme& theme) {
    // GEOMETRY ONLY. The text goes on after the canvas has been flushed —
    // flush() re-emits what the canvas holds rather than draining it, so glyphs
    // pushed now end up buried under a second copy of these rectangles. Exactly
    // the trap the badge labels already document.
    const float total = legendChipWidth(detail, theme);
    canvas.fillRect(x, y, total, height, JPaint::solid(theme.legendChipBackground));
    canvas.fillRect(x, y, theme.legendNameWidth, height, JPaint::solid(channelColour));
    return total;
}

void JTracePainter::paintLegendChipText(JPrimitiveBuffer& buf, const JRect& chip,
                                        const std::string& name, const std::string& detail,
                                        const JColor& channelColour,
                                        const JScopeTheme& theme) {
    // The name sits on the channel's own colour, which is what makes the row
    // readable at a glance — the colour, not the text, is matched to the trace.
    JTextHelper::pushTextAligned(buf, chip.x, chip.y, theme.legendNameWidth, chip.height,
                                 name, theme.legendNameText.data(),
                                 JTextHelper::Align::Center, 0.0f);
    JTextHelper::pushTextAligned(buf, chip.x + theme.legendNameWidth, chip.y,
                                 chip.width - theme.legendNameWidth, chip.height,
                                 detail, channelColour.data(),
                                 JTextHelper::Align::Center, 0.0f);
}

void JTracePainter::paintBadgeLabel(JPrimitiveBuffer& buf, const JRect& badge,
                                    const std::string& label, const JScopeTheme& theme) {
    // Centred in the BODY, not the whole badge: including the point would push
    // the digit left of centre by half the tip.
    JTextHelper::pushTextAligned(buf, badge.x, badge.y,
                                 badge.width - theme.markerBadgePoint, badge.height,
                                 label, theme.markerLabelText.data(),
                                 JTextHelper::Align::Center, 0.0f);
}

JRect JTracePainter::triggerPositionRect(float x, float topY, const JScopeTheme& theme) {
    const float h = theme.markerBadgeHeight;
    // Hangs ABOVE the grid, so none of it covers the trace it points at.
    return JRect{ x - h * 0.5f, topY - h, h, h };
}

JRect JTracePainter::paintTriggerPosition(JVectorCanvas& canvas, float x, float topY,
                                          const JColor& color, const JScopeTheme& theme,
                                          bool active) {
    const JRect r = triggerPositionRect(x, topY, theme);
    const float shoulder = topY - theme.markerBadgePoint;
    const float emph = theme.markerEmphasisWidth;

    canvas.beginPath();
    canvas.moveTo(r.x, r.y);
    canvas.lineTo(r.x + r.width, r.y);
    canvas.lineTo(r.x + r.width, shoulder);
    canvas.lineTo(x, topY);
    canvas.lineTo(r.x, shoulder);
    canvas.close();
    canvas.fill(JPaint::solid(color));

    if (active)
        canvas.strokeRect(r.x - emph, r.y - emph, r.width + emph * 2.0f,
                          r.height + emph * 2.0f, emph, JPaint::solid(theme.readoutText));

    return r;
}

} // inline namespace jf
