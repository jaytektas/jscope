// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JTraceView.h"

#include "JScopeFormat.h"

#include <j/core/JTextHelper.h>
#include "JCursorOverlay.h"
#include "JGraticule.h"
#include "JTracePainter.h"
#include "scope/JScopeLog.h"

#include <algorithm>
#include <chrono>
#include <cmath>

inline namespace jf {

namespace {
// How much one wheel notch zooms. A device fact about input, not a visual one:
// it describes the mouse, not the appearance of anything.
constexpr double kZoomInFactor  = 0.80;
constexpr double kZoomOutFactor = 1.25;
constexpr double kMinViewSpan   = 1.0e-4;   // do not zoom past 10000:1

// The view window may hang off either END of the record, which is how the
// horizontal position control moves the trigger across the screen: the record
// slides sideways and blank appears behind it, exactly as on a bench scope.
// Without the overhang there is nowhere to slide TO whenever the window already
// spans the whole record — which is the default — and the marker looks broken.
//
// This is the stop: at least this much of the screen keeps showing record, so
// the trace can be pushed nearly off but never completely lost.
constexpr double kMinVisibleFraction = 0.15;

// How far one wheel notch moves a level, as a fraction of a division. A tenth
// gives a knob you can land on a value with while still crossing the screen in
// a couple of flicks — the same trade a real scope's trigger encoder makes.
constexpr double kNotchDivisions = 0.1;
}

JTraceView::JTraceView(JSceneGraph& graph) : JWidget(graph, "JTraceView") {
    for (auto& c : m_channels) c = JChannelView{};
    JLOGC(JScopeLog::kTrace, JLogLevel::Debug) << "JTraceView constructed";
}

JRect JTraceView::_plotRect() const {
    const JScopeTheme& t = JScopeTheme::current();
    // The graticule, inset by the gutters the markers live in. Markers outside
    // the grid is the convention on every scope, and it keeps them off the trace
    // they refer to.
    const JRect b = bounds();
    const float left  = t.viewPadding + t.markerGutterLeft;
    const float right = t.viewPadding + t.markerGutterRight;
    return JRect{ b.x + left,
                  b.y + t.viewPadding + t.markerBadgeHeight,
                  std::max(0.0f, b.width  - left - right),
                  std::max(0.0f, b.height - t.viewPadding * 2.0f
                                          - t.markerBadgeHeight - t.legendHeight) };
}

double JTraceView::_triggerFraction() const {
    // Negative when the frame carries no trigger — roll mode, or a sweep that
    // ran out in Auto. Out of every view window, so no marker is drawn.
    if (m_frame.header.triggerSampleIndex < 0 || m_frame.header.sampleCount == 0)
        return -1.0;
    return static_cast<double>(m_frame.header.triggerSampleIndex)
         / static_cast<double>(m_frame.header.sampleCount);
}

bool JTraceView::_inBadge(const JRect& r, float mx, float my) {
    // The badge rectangle exactly, with no slop: badges are 15 px tall and share
    // one lane, so a slop band would only make a neighbour grabbable through the
    // gap between them.
    return mx >= r.x && mx <= r.x + r.width && my >= r.y && my <= r.y + r.height;
}

JTraceViewport JTraceView::_viewportFor(uint8_t channelId, const JRect& plot) const {
    const JChannelView& cv = m_channels[channelId % JScopeLimits::kMaxChannels];
    JTraceViewport vp;
    vp.x = plot.x;  vp.y = plot.y;
    vp.width = plot.width;  vp.height = plot.height;
    vp.voltsPerDiv = cv.voltsPerDiv;
    // The device's own offset (where it has one) plus the display position.
    vp.offsetVolts = cv.offsetVolts + cv.positionVolts;
    vp.inverted    = cv.inverted;
    vp.verticalDivisions = m_divisionsY;

    // The window may hang off either end of the record. The decimator maps
    // [firstSample, firstSample + sampleCount) onto the whole of [x, x + width),
    // so rather than teach it about overhang, hand it the SUB-RECTANGLE the
    // record actually covers: clip the window to [0, 1] and move x and width to
    // match. The scissor set in populateRenderPrimitives does the rest.
    const double from = std::max(m_viewStart, 0.0);
    const double to   = std::min(m_viewStart + m_viewSpan, 1.0);
    if (to <= from) { vp.sampleCount = 0; return vp; }

    vp.x     = plot.x + static_cast<float>((from - m_viewStart) / m_viewSpan * plot.width);
    vp.width = static_cast<float>((to - from) / m_viewSpan * plot.width);

    const size_t total = m_frame.header.sampleCount;
    vp.firstSample = static_cast<size_t>(from * total);
    vp.sampleCount = static_cast<size_t>((to - from) * total);
    if (vp.firstSample > total) vp.firstSample = total;
    if (vp.sampleCount == 0 && total > 0) vp.sampleCount = 1;
    return vp;
}

JCursorOverlay::JMapping JTraceView::_mappingFor(const JRect& plot) const {
    JCursorOverlay::JMapping m;
    m.x     = plot.x;
    m.width = plot.width;
    if (!m_hasFrame || plot.width <= 0.0f) return m;

    // The visible window in record time, so a cursor placed on a feature stays
    // on it through a zoom.
    const double total = m_frame.header.sampleCount * m_frame.header.sampleInterval;
    m.tStart          = m_viewStart * total;
    m.secondsPerPixel = (m_viewSpan * total) / plot.width;
    return m;
}

void JTraceView::resetCursorsToFrame() {
    if (!m_hasFrame) return;
    const double total = m_frame.header.sampleCount * m_frame.header.sampleInterval;
    m_cursors.setX(total * 0.25, total * 0.75);

    // Y cursors bracket a division either side of centre on their channel, which
    // is a visible separation at any vertical scale.
    const uint8_t ch = m_cursors.yChannel();
    const double  vdiv = m_channels[ch % JScopeLimits::kMaxChannels].voltsPerDiv;
    m_cursors.setY(-vdiv, vdiv);

    JLOGC(JScopeLog::kTrace, JLogLevel::Debug)
        << "cursors reset: X " << m_cursors.x1() << ".." << m_cursors.x2() << "s"
        << " Y " << m_cursors.y1() << ".." << m_cursors.y2() << "V";
    onCursorsChanged.emit();
    invalidate();
}

void JTraceView::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JScopeTheme& theme = JScopeTheme::current();
    const JRect b    = bounds();
    const JRect plot = _plotRect();
    if (b.width <= 0.0f || b.height <= 0.0f) return;

    m_canvas.clear();
    m_canvas.setAntiAlias(theme.chromeAntiAliasPixels);

    m_canvas.fillRect(b.x, b.y, b.width, b.height, JPaint::solid(theme.background));
    JGraticule::draw(m_canvas, plot.x, plot.y, plot.width, plot.height,
                     m_divisionsX, m_divisionsY, theme);

    // The background and the graticule go down unclipped and the canvas is
    // emptied, because everything after this is clipped to the grid and a clip
    // set on the graticule would shave the outer half of its own border.
    m_canvas.flush(buf);
    m_canvas.clear();

    if (m_hasFrame) {
        // Reserve once per width change, not per frame: the buffers then keep
        // their capacity and the render path stops allocating entirely.
        if (plot.width != m_reservedWidth) {
            for (auto& s : m_scratch) JTraceDecimator::reserve(s, plot.width);
            m_reservedWidth = plot.width;
            JLOGC(JScopeLog::kTrace, JLogLevel::Debug)
                << "reserved vertex buffers for " << plot.width << "px";
        }

        // Section timing for the render path. A scope redraws constantly, so
        // knowing which third of this function costs what — decimation,
        // tessellation, or the flush into the primitive buffer — is the
        // difference between fixing a frame-rate problem and guessing at it.
        const auto tPaintStart = std::chrono::steady_clock::now();

        size_t vertices = 0;
        for (uint8_t p = 0; p < m_frame.header.channelCount; ++p) {
            const uint8_t id = m_frame.header.channelIds[p];
            if (!m_channels[id % JScopeLimits::kMaxChannels].enabled) continue;

            const JTraceViewport vp = _viewportFor(id, plot);
            const bool focused = (m_focusedChannel == static_cast<int>(id));
            JTracePainter::paint(m_canvas, m_frame, p, vp, theme, focused, m_scratch[p]);
            vertices += m_scratch[p].size();
        }
        const auto tPaintEnd = std::chrono::steady_clock::now();
        m_paintMs = std::chrono::duration<double, std::milli>(tPaintEnd - tPaintStart).count();
        m_vertices = vertices;

        JLOGC(JScopeLog::kTrace, JLogLevel::Trace)
            << "frame " << m_frame.header.sequence << " drawn: " << vertices << " vertices"
            << " plot=" << plot.x << "," << plot.y << " " << plot.width << "x" << plot.height
            << " window=" << m_viewStart << "+" << m_viewSpan
            << " samples=" << m_frame.header.sampleCount;

        // Traces are SCISSORED to the graticule. A signal bigger than the screen
        // has to stop at the grid edge the way it does on the instrument — the
        // vertical position control is meaningless if you can always see the
        // whole trace anyway, and without this a trace at 8 V/div is drawn
        // straight across the toolbar and the panels.
        buf.pushClip(plot.x, plot.y, plot.width, plot.height);
        m_canvas.flush(buf);
        buf.popClip();
        m_canvas.clear();
    }

    // Markers over the traces, in the gutter outside the graticule. Drawn after
    // the traces so a marker is never buried under one.
    m_canvas.setAntiAlias(theme.chromeAntiAliasPixels);
    m_badgeCount = 0;
    if (m_hasFrame) {
        for (uint8_t id = 0; id < JScopeLimits::kMaxChannels; ++id) {
            if (!m_channels[id].enabled) continue;
            const JTraceViewport vp = _viewportFor(id, plot);
            const float raw = JTraceDecimator::voltsToY(0.0, vp);
            const float y   = std::clamp(raw, plot.y, plot.y + plot.height);
            _recordBadge(JTracePainter::paintLevelBadge(m_canvas, plot.x, y,
                                                        theme.traceColor(id), theme,
                                                        m_dragMarker == static_cast<int>(id),
                                                        raw != y),
                         static_cast<char>('1' + id));
        }

        // The trigger level, against its source channel's scale, in that
        // channel's colour: "T" says what the badge is and the colour says what
        // it triggers on, which is the question you actually have with more than
        // one trace up. Drawn LAST and hit-tested FIRST — the badges share one
        // lane, so where a trigger level sits on a channel's ground, the one you
        // can see is the one you can grab.
        const JTraceViewport tvp = _viewportFor(m_triggerSource, plot);
        const float rawT = JTraceDecimator::voltsToY(m_triggerLevel, tvp);
        const float ty   = std::clamp(rawT, plot.y, plot.y + plot.height);
        _recordBadge(JTracePainter::paintLevelBadge(m_canvas, plot.x, ty,
                                                    theme.traceColor(m_triggerSource), theme,
                                                    m_dragMarker == kDragTrigger, rawT != ty),
                     'T');

        // And where in the record it happened, on the top edge. Dragging it
        // scrolls the view so the trigger lands under the pointer — the same
        // gesture as the instrument's horizontal position control, and a display
        // operation, so it works on a device (like the 1008C) whose hardware
        // fixes the trigger at the middle of the record.
        m_triggerPosRect = JRect{};
        const double frac = _triggerFraction();
        if (frac >= m_viewStart && frac <= m_viewStart + m_viewSpan) {
            const float tx = plot.x + static_cast<float>(
                (frac - m_viewStart) / m_viewSpan * plot.width);
            m_triggerPosRect = JTracePainter::paintTriggerPosition(
                m_canvas, tx, plot.y, theme.triggerMarker, theme,
                m_dragMarker == kDragTriggerPos);
        }
    }

    // Cursors last, over the traces, in chrome anti-aliasing.
    if (m_hasFrame && (m_cursors.xEnabled() || m_cursors.yEnabled())) {
        m_canvas.setAntiAlias(theme.chromeAntiAliasPixels);
        JCursorOverlay::draw(m_canvas, m_cursors, _mappingFor(plot),
                             _viewportFor(m_cursors.yChannel(), plot), theme);
    }

    _paintLegend(plot, theme);

    const auto tFlushStart = std::chrono::steady_clock::now();
    m_canvas.flush(buf);
    m_flushMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - tFlushStart).count();

    // Badge labels only now. flush() re-emits everything the canvas holds rather
    // than draining it, so glyphs pushed while the badges were being drawn would
    // be buried under a second copy of them.
    for (size_t i = 0; i < m_badgeCount; ++i)
        JTracePainter::paintBadgeLabel(buf, m_badges[i].rect, m_badges[i].label, theme);

    _paintLegendText(buf, plot, theme);
}

void JTraceView::setLegend(const std::string& sweepState, double secondsPerDiv,
                           uint8_t triggerChannel, double triggerLevelVolts) {
    m_sweepState           = sweepState;
    m_legendSecondsPerDiv  = secondsPerDiv;
    m_legendTriggerChannel = triggerChannel;
    m_legendTriggerLevel   = triggerLevelVolts;
}


// The OEM software's legend: sweep state and the timebase/trigger readout above
// the graticule, a row of per-channel chips below it. A user reads volts/div off
// the display, not off a side panel they have to look away to find.
//
// GEOMETRY ONLY — the text is pushed by _paintLegendText after the flush.
void JTraceView::_paintLegend(const JRect& plot, const JScopeTheme& theme) {
    m_legendChipCount = 0;

    float x = plot.x;
    const float y = plot.y + plot.height + theme.legendChipGap;
    const float h = theme.legendHeight - theme.legendChipGap;

    // Only ENABLED channels. The row says what is on screen, and a chip for a
    // trace that is not drawn is noise.
    for (uint8_t c = 0; c < JScopeLimits::kMaxChannels; ++c) {
        if (!m_channels[c].enabled) continue;
        if (m_legendChipCount >= m_legendChips.size()) break;

        // Coupling beside the scale, which is what the instrument's own footer
        // shows. Spelled rather than drawn as the AC/DC glyphs: those are
        // outside the font atlas the framework builds, so they would come out
        // blank.
        const std::string detail = std::string(jScopeCouplingName(m_channels[c].coupling))
                                 + " " + jScopeFormatVolts(m_channels[c].voltsPerDiv);
        const float w = JTracePainter::legendChipWidth(detail, theme);
        if (x + w > plot.x + plot.width) break;              // no room left

        JTracePainter::paintLegendChip(m_canvas, x, y, h, detail,
                                       theme.traceColor(c), theme);
        m_legendChips[m_legendChipCount].rect    = JRect{ x, y, w, h };
        m_legendChips[m_legendChipCount].name    = "CH" + std::to_string(c + 1);
        m_legendChips[m_legendChipCount].detail  = detail;
        m_legendChips[m_legendChipCount].channel = c;
        ++m_legendChipCount;

        x += w + theme.legendChipGap;
    }
}

void JTraceView::_paintLegendText(JPrimitiveBuffer& buf, const JRect& plot,
                                  const JScopeTheme& theme) {
    const JScopeTheme& t = theme;
    const float headerY = bounds().y + t.viewPadding;

    if (!m_sweepState.empty())
        JTextHelper::pushTextAligned(buf, plot.x, headerY, plot.width, t.markerBadgeHeight,
                                     m_sweepState, t.legendStateText.data(),
                                     JTextHelper::Align::Left, 0.0f);

    // Timebase and trigger together on the right. The CENTRE of this lane is
    // where the trigger-position badge sits, so anything drawn there collides
    // with it.
    if (m_legendSecondsPerDiv > 0.0) {
        const std::string right =
            "Time: " + jScopeFormatSeconds(m_legendSecondsPerDiv)
          + "    T CH" + std::to_string(m_legendTriggerChannel + 1)
          + " " + jScopeFormatVolts(m_legendTriggerLevel);
        JTextHelper::pushTextAligned(buf, plot.x, headerY, plot.width, t.markerBadgeHeight,
                                     right, t.legendTimebaseText.data(),
                                     JTextHelper::Align::Right, 0.0f);
    }

    for (size_t i = 0; i < m_legendChipCount; ++i)
        JTracePainter::paintLegendChipText(buf, m_legendChips[i].rect,
                                           m_legendChips[i].name, m_legendChips[i].detail,
                                           theme.traceColor(m_legendChips[i].channel), theme);
}

void JTraceView::_recordBadge(const JRect& rect, char label) {
    if (m_badgeCount >= m_badges.size()) return;
    m_badges[m_badgeCount].rect  = rect;
    m_badges[m_badgeCount].label = std::string(1, label);
    ++m_badgeCount;
}

void JTraceView::setFrame(const JScopeFrame& frame) {
    // Provision on first use, and again if the source's shape grew. The pool
    // frame this came from is released back to the acquisition thread the moment
    // we return, so a copy is not optional.
    if (m_frame.maxChannels() < frame.header.channelCount ||
        m_frame.maxSamples()  < frame.header.sampleCount) {
        m_frame.provision(std::max<uint8_t>(frame.header.channelCount, JScopeLimits::kMaxChannels),
                          frame.header.sampleCount);
        JLOGC(JScopeLog::kTrace, JLogLevel::Debug)
            << "display frame provisioned for " << frame.header.sampleCount << " samples/ch";
    }
    m_frame.shape(frame.header.channelCount, frame.header.sampleCount);
    m_frame.copyHeaderAndSamplesFrom(frame);
    m_hasFrame = true;

    // A window requested before any record existed can be computed now.
    if (m_pendingTimeWindow > 0.0) setTimeWindow(m_pendingTimeWindow);

    // And recompute it whenever the record's own span changes, which happens
    // whenever the timebase moves the device to a different rate code. Without
    // this the window keeps the previous record's scaling and the trace is drawn
    // short of the graticule — measured on the bench at 20 ms/div, where a
    // 223.8 ms record was being drawn into a window sized for a 108 ms one and
    // filled 54% of the width.
    const double recordSeconds =
        m_frame.header.sampleCount * m_frame.header.sampleInterval;
    if (recordSeconds > 0.0 && m_timeWindowSeconds > 0.0 &&
        std::abs(recordSeconds - m_lastRecordSeconds) > recordSeconds * 1.0e-6) {
        m_lastRecordSeconds = recordSeconds;
        setTimeWindow(m_timeWindowSeconds);
    }

    // Per-plane checksum of the samples actually put on screen. If a trace moves
    // between frames, this says immediately whether the acquisition changed or
    // the renderer did — which is the difference between two very different bugs.
    if (JLog::instance().enabled(JScopeLog::kTrace, JLogLevel::Trace)) {
        std::string sums;
        for (uint8_t p = 0; p < m_frame.header.channelCount; ++p) {
            uint64_t h = 1469598103934665603ull;
            const int16_t* pl = m_frame.plane(p);
            for (uint32_t i = 0; i < m_frame.header.sampleCount; ++i)
                h = (h ^ static_cast<uint16_t>(pl[i])) * 1099511628211ull;
            sums += " CH" + std::to_string(m_frame.header.channelIds[p] + 1) + "=";
            sums += std::to_string(h & 0xffffff);
        }
        JLOGC(JScopeLog::kTrace, JLogLevel::Trace) << "plane checksums:" << sums;
    }

    invalidate();
}

void JTraceView::clearFrame() {
    m_hasFrame = false;
    invalidate();
}

void JTraceView::setChannelView(uint8_t channelId, bool enabled, double voltsPerDiv,
                                double offsetVolts, bool inverted,
                                JScopeCoupling coupling) {
    if (channelId >= JScopeLimits::kMaxChannels) return;
    JChannelView& c = m_channels[channelId];
    c.enabled     = enabled;
    c.voltsPerDiv = voltsPerDiv;
    c.offsetVolts = offsetVolts;
    c.inverted    = inverted;
    c.coupling    = coupling;
    invalidate();
}

void JTraceView::setChannelPosition(uint8_t channelId, double volts) {
    if (channelId >= JScopeLimits::kMaxChannels) return;
    m_channels[channelId].positionVolts = volts;
    invalidate();
}

double JTraceView::channelPosition(uint8_t channelId) const {
    return m_channels[channelId % JScopeLimits::kMaxChannels].positionVolts;
}

void JTraceView::setTriggerLevel(double volts, uint8_t sourceChannel) {
    m_triggerLevel  = volts;
    m_triggerSource = sourceChannel;
    invalidate();
}

void JTraceView::setGraticule(uint8_t divisionsX, uint8_t divisionsY) {
    if (divisionsX == 0 || divisionsY == 0) return;
    m_divisionsX = divisionsX;
    m_divisionsY = divisionsY;
    JLOGC(JScopeLog::kTrace, JLogLevel::Debug)
        << "graticule set to " << int(divisionsX) << "x" << int(divisionsY);
    invalidate();
}

void JTraceView::setViewWindow(double startFraction, double spanFraction) {
    // The upper bound is NOT 1: a window can legitimately be longer than the
    // record now that s/div sets it, and clamping there would silently stretch a
    // short capture across the full width and mislabel its time axis.
    m_viewSpan  = std::max(spanFraction, kMinViewSpan);
    // Deliberately NOT [0, 1 - span]: see kMinVisibleFraction. That clamp is
    // empty at span == 1, which silently pinned the window and left the
    // horizontal trigger marker unable to move at all.
    const double slack = m_viewSpan * (1.0 - kMinVisibleFraction);
    m_viewStart = std::clamp(startFraction, -slack, 1.0 - m_viewSpan + slack);
    onViewWindowChanged.emit(m_viewStart, m_viewSpan);
    invalidate();
}

void JTraceView::resetViewWindow() { setViewWindow(0.0, 1.0); }

void JTraceView::setTimeWindow(double secondsPerDiv) {
    if (!(secondsPerDiv > 0.0)) return;
    m_timeWindowSeconds = secondsPerDiv;

    const double recordSeconds =
        m_frame.header.sampleCount * m_frame.header.sampleInterval;
    if (!(recordSeconds > 0.0)) { m_pendingTimeWindow = secondsPerDiv; return; }
    m_pendingTimeWindow = 0.0;

    // THE WHOLE RECORD, always. Not a window cut out of it.
    //
    // Cutting a window and centring it on the trigger meant that moving the
    // trigger moved the window with it: at anything but centre the window hung
    // off the end of the record and the graticule was blank down one side, and
    // dragging the marker fought itself because every move re-centred.
    //
    // The instrument does not work that way and neither does the OEM software.
    // The device captures one sweep, 0xac decides where the trigger sits inside
    // it, and the screen shows all of it — the marker moves, the picture does
    // not. The seconds/div the driver reports is the real span over the
    // graticule, so the axis stays honest without cropping anything.
    setViewWindow(0.0, 1.0);
}

JScopeViewState JTraceView::viewState() const {
    JScopeViewState v;
    for (uint8_t c = 0; c < JScopeLimits::kMaxChannels; ++c)
        v.positionVolts[c] = m_channels[c].positionVolts;
    v.cursorXEnabled = m_cursors.xEnabled();
    v.cursorYEnabled = m_cursors.yEnabled();
    v.cursorX1       = m_cursors.x1();
    v.cursorX2       = m_cursors.x2();
    v.cursorY1       = m_cursors.y1();
    v.cursorY2       = m_cursors.y2();
    v.cursorYChannel = m_cursors.yChannel();
    return v;
}

void JTraceView::applyViewState(const JScopeViewState& v) {
    for (uint8_t c = 0; c < JScopeLimits::kMaxChannels; ++c)
        m_channels[c].positionVolts = v.positionVolts[c];

    m_cursors.setXEnabled(v.cursorXEnabled);
    m_cursors.setYEnabled(v.cursorYEnabled);
    m_cursors.setX(v.cursorX1, v.cursorX2);
    m_cursors.setY(v.cursorY1, v.cursorY2);
    m_cursors.setYChannel(v.cursorYChannel);
    onCursorsChanged.emit();
    invalidate();
}

bool JTraceView::handleScroll(float mx, float my, float wheel) {
    const JRect plot = _plotRect();
    if (wheel == 0.0f || plot.width <= 0.0f) return false;

    // Over a badge, the wheel is that badge's knob — the trigger level or a
    // channel's position. Dragging is for getting somewhere; the wheel is for
    // the last few millivolts, which is exactly the split a bench scope makes
    // between grabbing a marker and turning the encoder next to it.
    if (mx < plot.x) {
        const JScopeTheme& t = JScopeTheme::current();
        const double step = wheel > 0.0f ? kNotchDivisions : -kNotchDivisions;

        const JTraceViewport tvp = _viewportFor(m_triggerSource, plot);
        const float ty = std::clamp(JTraceDecimator::voltsToY(m_triggerLevel, tvp),
                                    plot.y, plot.y + plot.height);
        if (_inBadge(JTracePainter::levelBadgeRect(plot.x, ty, t), mx, my)) {
            m_triggerLevel += step * tvp.voltsPerDiv;
            onTriggerLevelDragged.emit(m_triggerLevel);
            invalidate();
            return true;
        }

        for (uint8_t id = 0; id < JScopeLimits::kMaxChannels; ++id) {
            if (!m_channels[id].enabled) continue;
            const JTraceViewport vp = _viewportFor(id, plot);
            const float y = std::clamp(JTraceDecimator::voltsToY(0.0, vp),
                                       plot.y, plot.y + plot.height);
            if (!_inBadge(JTracePainter::levelBadgeRect(plot.x, y, t), mx, my)) continue;
            m_channels[id].positionVolts += step * vp.voltsPerDiv;
            onChannelPositionDragged.emit(id, m_channels[id].positionVolts);
            invalidate();
            return true;
        }
        return false;
    }

    if (mx > plot.x + plot.width || my < plot.y || my > plot.y + plot.height)
        return false;

    // Zoom about the cursor, so the sample under the pointer stays put.
    const double anchor = m_viewStart
                        + m_viewSpan * ((mx - plot.x) / static_cast<double>(plot.width));
    const double span   = std::clamp(m_viewSpan * (wheel > 0 ? kZoomInFactor : kZoomOutFactor),
                                     kMinViewSpan, 1.0);
    const double start  = anchor - (anchor - m_viewStart) * (span / m_viewSpan);

    setViewWindow(start, span);
    JLOGC(JScopeLog::kTrace, JLogLevel::Debug)
        << "zoom: span=" << m_viewSpan << " start=" << m_viewStart;
    return true;
}

void JTraceView::handleMousePress(float mx, float my) {
    const JRect plot = _plotRect();
    const JScopeTheme& t = JScopeTheme::current();

    // The top edge: the horizontal trigger-position badge.
    if (my < plot.y && _inBadge(m_triggerPosRect, mx, my)) {
        m_dragMarker = kDragTriggerPos;
        return;
    }

    // The gutter first: markers live OUTSIDE the graticule, so a press there
    // never reaches the pan-and-cursor logic below.
    //
    // Hit-tested against the badge rectangles the painter actually draws, not
    // against a slop band around the level. The shape you see is then the shape
    // you can grab, which is the whole reason for drawing a badge instead of a
    // hairline in the first place.
    if (mx < plot.x) {
        const JTraceViewport tvp = _viewportFor(m_triggerSource, plot);
        const float ty = std::clamp(JTraceDecimator::voltsToY(m_triggerLevel, tvp),
                                    plot.y, plot.y + plot.height);
        if (_inBadge(JTracePainter::levelBadgeRect(plot.x, ty, t), mx, my)) {
            m_dragMarker = kDragTrigger;
            return;
        }
        for (uint8_t id = 0; id < JScopeLimits::kMaxChannels; ++id) {
            if (!m_channels[id].enabled) continue;
            const JTraceViewport vp = _viewportFor(id, plot);
            const float y = std::clamp(JTraceDecimator::voltsToY(0.0, vp),
                                       plot.y, plot.y + plot.height);
            if (_inBadge(JTracePainter::levelBadgeRect(plot.x, y, t), mx, my)) {
                m_dragMarker = static_cast<int>(id);
                return;
            }
        }
        return;
    }

    if (mx < plot.x || mx > plot.x + plot.width || my < plot.y || my > plot.y + plot.height)
        return;

    const JScopeTheme& theme = JScopeTheme::current();

    // A cursor under the pointer is what the user is reaching for; panning is
    // what they get otherwise. Checking the cursor first is why a cursor can be
    // grabbed at all on a view that also pans on drag.
    m_dragHandle = JCursorOverlay::hitTest(m_cursors, mx, my, _mappingFor(plot),
                                           _viewportFor(m_cursors.yChannel(), plot),
                                           theme);
    if (m_dragHandle != JCursorModel::JHandle::None) return;

    m_panning        = true;
    m_panAnchorX     = mx;
    m_panAnchorStart = m_viewStart;
}

void JTraceView::handleMouseMove(float mx, float my) {
    const JRect plot = _plotRect();

    if (m_dragMarker != kDragNone) {
        if (m_dragMarker == kDragTriggerPos) {
            if (plot.width > 0.0f) {
                // Tell the INSTRUMENT where to put the trigger, rather than
                // scrolling the view to bring it under the pointer. The device
                // captures the same sweep either way; this moves the trigger
                // inside it, so the graticule stays full.
                const double at = std::clamp((mx - plot.x) / static_cast<double>(plot.width),
                                             0.0, 1.0);
                JLOGC(JScopeLog::kTrace, JLogLevel::Debug)
                    << "trigger position drag: mx=" << mx << " plot.x=" << plot.x
                    << " w=" << plot.width << " -> " << at;
                onTriggerPositionDragged.emit(at);
            }
            invalidate();
            return;
        }
        if (m_dragMarker == kDragTrigger) {
            const JTraceViewport tvp = _viewportFor(m_triggerSource, plot);
            m_triggerLevel = JTraceDecimator::yToVolts(my, tvp);
            onTriggerLevelDragged.emit(m_triggerLevel);
        } else {
            const uint8_t id = static_cast<uint8_t>(m_dragMarker);
            const JTraceViewport vp = _viewportFor(id, plot);
            // The marker marks 0 V, so dropping it at a height means "put zero
            // here". yToVolts gives the signal voltage currently at that height,
            // which is how far zero has to MOVE to land there — so it adds to
            // the position rather than subtracting from it.
            //
            // Getting that sign wrong does not merely invert the drag: because
            // the viewport is recomputed from the new position on every mouse
            // move, the error compounds and the trace leaves the screen in a few
            // pixels of movement.
            m_channels[id].positionVolts += JTraceDecimator::yToVolts(my, vp);
            onChannelPositionDragged.emit(id, m_channels[id].positionVolts);
        }
        invalidate();
        return;
    }

    if (m_dragHandle != JCursorModel::JHandle::None) {
        const bool isX = (m_dragHandle == JCursorModel::JHandle::X1 ||
                          m_dragHandle == JCursorModel::JHandle::X2);
        // Cursors live in signal units, so a drag converts pixels back to
        // seconds or volts rather than storing a screen position.
        const double value = isX
            ? _mappingFor(plot).xToTime(mx)
            : JTraceDecimator::yToVolts(my, _viewportFor(m_cursors.yChannel(), plot));
        m_cursors.moveHandle(m_dragHandle, value);
        onCursorsChanged.emit();
        invalidate();
        return;
    }

    if (!m_panning || plot.width <= 0.0f) return;
    const double dx = (m_panAnchorX - mx) / static_cast<double>(plot.width) * m_viewSpan;
    setViewWindow(m_panAnchorStart + dx, m_viewSpan);
}

void JTraceView::handleMouseRelease(float /*mx*/, float /*my*/) {
    m_panning    = false;
    m_dragHandle = JCursorModel::JHandle::None;
    m_dragMarker = kDragNone;
}

} // inline namespace jf
