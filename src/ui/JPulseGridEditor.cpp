#include "JPulseGridEditor.h"

#include "scope/JScopeLog.h"

#include <j/core/JTextHelper.h>
#include <j/graphics/VectorGraphics.h>

#include <algorithm>
#include <cstdio>
#include <string>

inline namespace jf {

namespace {

// The gutter that carries the channel number tags, and the strip under the grid
// that carries the 0 / 720 degree labels. Both are chrome, sized from the theme's
// row height so they track the font rather than being pixel counts of their own.
float gutterWidth(const JScopeTheme& t) { return t.panelRowHeight * 1.4f; }
float axisHeight (const JScopeTheme& t) { return t.panelRowHeight; }

// The strip above the grid that carries the cursor handles. Pressing there grabs
// a cursor; pressing inside the grid flips a cell. Separating them is what the
// OEM does too -- its handles sit outside the graticule -- and without it every
// attempt to move a cursor would edit the pattern underneath it.
float cursorRailHeight(const JScopeTheme& t) { return t.panelRowHeight * 0.7f; }

// How near the handle a press has to land, in pixels.
constexpr float kCursorGrabPx = 10.0f;

// A lane's waveform does not fill its lane: a gap top and bottom keeps adjacent
// channels visibly separate, which is the whole reason for stacking them.
constexpr float kLaneFill = 0.62f;

} // namespace

JPulseGridEditor::JPulseGridEditor(JSceneGraph& graph) : JWidget(graph, "JPulseGridEditor") {}

void JPulseGridEditor::setPattern(const std::vector<uint8_t>& pattern, uint8_t channels) {
    m_pattern  = pattern;
    m_channels = channels ? channels : 1;
}

JRect JPulseGridEditor::_gridRect() const {
    const JScopeTheme& t = JScopeTheme::current();
    const JRect b = bounds();
    const float g = gutterWidth(t), a = axisHeight(t);
    const float rail = cursorRailHeight(t);
    return JRect{ b.x + g, b.y + t.panelGap + rail,
                  std::max(1.0f, b.width - g - t.panelGap),
                  std::max(1.0f, b.height - a - rail - t.panelGap * 2.0f) };
}

double JPulseGridEditor::cursorDegrees(int which) const {
    return m_cursorDegrees[(which == 1) ? 1 : 0];
}

float JPulseGridEditor::_xForDegrees(double degrees) const {
    const JRect g = _gridRect();
    return g.x + g.width * static_cast<float>(degrees / kCycleDegrees);
}

double JPulseGridEditor::_degreesForX(float mx) const {
    const JRect g = _gridRect();
    if (g.width <= 0.0f) return 0.0;
    const double d = kCycleDegrees * double(mx - g.x) / double(g.width);
    return std::clamp(d, 0.0, kCycleDegrees);
}

int JPulseGridEditor::_cursorHandleAt(float mx, float my) const {
    const JScopeTheme& t = JScopeTheme::current();
    const JRect g = _gridRect();
    // Only in the rail above the grid, so a press on the pattern is never a
    // cursor grab.
    if (my < g.y - cursorRailHeight(t) || my >= g.y) return -1;

    int best = -1;
    float bestDistance = kCursorGrabPx;
    for (int i = 0; i < 2; ++i) {
        const float d = std::abs(mx - _xForDegrees(m_cursorDegrees[i]));
        if (d <= bestDistance) { bestDistance = d; best = i; }
    }
    return best;
}

int JPulseGridEditor::_columnAt(float mx) const {
    if (m_pattern.empty()) return -1;
    const JRect g = _gridRect();
    if (mx < g.x || mx >= g.x + g.width) return -1;
    const int c = static_cast<int>((mx - g.x) / (g.width / float(m_pattern.size())));
    return (c >= 0 && c < static_cast<int>(m_pattern.size())) ? c : -1;
}

int JPulseGridEditor::_laneAt(float my) const {
    const JRect g = _gridRect();
    if (my < g.y || my >= g.y + g.height) return -1;
    const int l = static_cast<int>((my - g.y) / (g.height / float(m_channels)));
    return (l >= 0 && l < m_channels) ? l : -1;
}

void JPulseGridEditor::handleMouseMove(float mx, float my) {
    if (m_dragCursor >= 0) {
        m_cursorDegrees[m_dragCursor] = _degreesForX(mx);
        return;                       // a drag is not a hover
    }
    m_hoverColumn = _columnAt(mx);
    m_hoverLane   = _laneAt(my);
}

void JPulseGridEditor::handleMouseRelease(float, float) { m_dragCursor = -1; }

void JPulseGridEditor::handleMousePress(float mx, float my) {
    // A cursor handle first: the rail is outside the grid, so this can never
    // swallow a press meant for a cell.
    if (const int cursor = _cursorHandleAt(mx, my); cursor >= 0) {
        m_dragCursor = cursor;
        m_cursorDegrees[cursor] = _degreesForX(mx);
        return;
    }

    const int col = _columnAt(mx), lane = _laneAt(my);
    if (col < 0 || lane < 0) return;

    // Flip this channel's level at this pulse. One bit, in the device's own
    // format, so what is edited IS what gets sent.
    m_pattern[static_cast<size_t>(col)] ^= static_cast<uint8_t>(1u << lane);
    JLOGC(JScopeLog::kUi, JLogLevel::Debug)
        << "pulse " << (col + 1) << " CH" << (lane + 1) << " -> "
        << ((m_pattern[static_cast<size_t>(col)] & (1u << lane)) ? "high" : "low");
    onPatternEdited.emit(m_pattern);
}

void JPulseGridEditor::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JScopeTheme& t = JScopeTheme::current();
    const JRect b = bounds();
    const JRect g = _gridRect();

    // Anti-aliasing is a property of the canvas, not of each call.
    JVectorCanvas canvas(t.chromeAntiAliasPixels);
    using JVec2 = JVectorCanvas::JVec2;
    const auto line = [&canvas](float x0, float y0, float x1, float y1,
                                float w, const JPaint& p) {
        canvas.strokePolyline(std::vector<JVec2>{ {x0, y0}, {x1, y1} }, w, p, false);
    };
    canvas.fillRect(b.x, b.y, b.width, b.height, JPaint::solid(t.background));

    if (m_pattern.empty() || g.width <= 1.0f) { canvas.flush(buf); return; }

    const size_t columns   = m_pattern.size();
    const float  columnW   = g.width / float(columns);
    const float  laneH     = g.height / float(m_channels);
    const float  waveH     = laneH * kLaneFill;

    // Column grid. Every pulse boundary gets a line where they are far enough
    // apart to be distinguishable; past that they would be a solid wash, so the
    // grid falls back to the ten divisions of the cycle.
    const bool perColumn = columnW >= 4.0f;
    const size_t stride  = perColumn ? 1 : std::max<size_t>(1, columns / 10);
    for (size_t c = 0; c <= columns; c += stride) {
        const float x = g.x + columnW * float(c);
        line(x, g.y, x, g.y + g.height, 1.0f, JPaint::solid(t.graticuleMinor));
    }

    // Lane separators and each channel's waveform.
    for (uint8_t ch = 0; ch < m_channels; ++ch) {
        const float top    = g.y + laneH * float(ch);
        const float mid    = top + laneH * 0.5f;
        const float high   = mid - waveH * 0.5f;
        const float low    = mid + waveH * 0.5f;
        const JColor colour = t.channelTrace[ch % JScopeLimits::kMaxChannels];

        line(g.x, top, g.x + g.width, top, 1.0f, JPaint::solid(t.graticuleMajor));

        // The lane's own mid-line, dashed in the OEM. A faint solid line reads the
        // same at this size and costs no dash machinery.
        line(g.x, mid, g.x + g.width, mid, 1.0f, JPaint::solid(t.graticuleMinor));

        // One polyline per lane, walking the columns and stepping at each change:
        // a square wave is exactly the level line plus a vertical at every edge.
        std::vector<JVec2> points;
        points.reserve(columns * 3 + 2);
        const uint8_t bit = static_cast<uint8_t>(1u << ch);
        float previous = (m_pattern[0] & bit) ? high : low;
        points.push_back({ g.x, previous });
        for (size_t c = 0; c < columns; ++c) {
            const float y  = (m_pattern[c] & bit) ? high : low;
            const float x0 = g.x + columnW * float(c);
            if (y != previous) {                 // the edge itself, drawn vertically
                points.push_back({ x0, previous });
                points.push_back({ x0, y });
                previous = y;
            }
            points.push_back({ x0 + columnW, y });
        }
        canvas.strokePolyline(points, t.traceWidth, JPaint::solid(colour), false);
    }
    canvas.strokeRect(g.x, g.y, g.width, g.height, 1.0f, JPaint::solid(t.graticuleBorder));

    // The cell under the pointer, so it is obvious what a click will flip.
    if (m_hoverColumn >= 0 && m_hoverLane >= 0)
        canvas.strokeRect(g.x + columnW * float(m_hoverColumn),
                          g.y + laneH * float(m_hoverLane), columnW, laneH,
                          1.0f, JPaint::solid(t.readoutText));

    // The two angle cursors, drawn over the pattern with a handle in the rail
    // above it. L1 takes the trigger colour and L2 the neutral readout colour, so
    // which is which is readable without consulting the numbers.
    const float rail = cursorRailHeight(t);
    for (int i = 0; i < 2; ++i) {
        const float cx = _xForDegrees(m_cursorDegrees[i]);
        const JColor colour = (i == 0) ? t.triggerMarker : t.readoutText;
        line(cx, g.y - rail, cx, g.y + g.height, 1.0f, JPaint::solid(colour));
        // A handle wide enough to hit: the line alone is one pixel.
        canvas.fillRect(cx - kCursorGrabPx * 0.5f, g.y - rail,
                        kCursorGrabPx, rail, JPaint::solid(colour));
    }

    canvas.flush(buf);

    // TEXT AFTER FLUSH. JVectorCanvas::flush re-emits what it holds rather than
    // draining it, so glyphs pushed before it would be emitted again underneath
    // the geometry.
    const float gw = gutterWidth(t);
    for (uint8_t ch = 0; ch < m_channels; ++ch)
        JTextHelper::pushTextAligned(buf, b.x, g.y + laneH * (float(ch) + 0.5f) - t.panelRowHeight * 0.5f,
                                     gw, t.panelRowHeight, std::to_string(ch + 1),
                                     t.channelTrace[ch % JScopeLimits::kMaxChannels].data(),
                                     JTextHelper::Align::Center, 0.0f);

    // The cycle runs 0 to 720 degrees: two crank revolutions, one four-stroke
    // cycle. Labelling it in degrees is the point of the whole page.
    const float ay = g.y + g.height + t.panelGap;
    JTextHelper::pushTextAligned(buf, g.x, ay, g.width * 0.25f, axisHeight(t), "0\xC2\xB0",
                                 t.legendHeaderText.data(), JTextHelper::Align::Left, 0.0f);
    JTextHelper::pushTextAligned(buf, g.x + g.width * 0.75f, ay, g.width * 0.25f, axisHeight(t),
                                 "720\xC2\xB0", t.legendHeaderText.data(),
                                 JTextHelper::Align::Right, 0.0f);

    // L1, L2 and the angle between them -- the measurement the cursors exist for,
    // and the OEM's own readout. A cam event is specified as so many degrees from
    // a crank reference, so the difference is the number actually wanted.
    const double l1 = m_cursorDegrees[0], l2 = m_cursorDegrees[1];
    char cursors[96];
    std::snprintf(cursors, sizeof cursors,
                  "L1 %.0f\xC2\xB0   L2 %.0f\xC2\xB0   \xCE\x94 %.0f\xC2\xB0",
                  l1, l2, l2 - l1);
    JTextHelper::pushTextAligned(buf, g.x, b.y, g.width, rail, cursors,
                                 t.readoutText.data(), JTextHelper::Align::Center, 0.0f);

    // What the pointer is over, in the OEM's own terms: which channel, which
    // pulse, and where that pulse sits in the cycle.
    if (m_hoverColumn >= 0 && m_hoverLane >= 0) {
        const double degrees = kCycleDegrees * (double(m_hoverColumn) + 0.5) / double(columns);
        const std::string readout = "CH" + std::to_string(m_hoverLane + 1) +
                                    "  Pulse " + std::to_string(m_hoverColumn + 1) +
                                    " of " + std::to_string(columns) +
                                    "  " + std::to_string(static_cast<int>(degrees)) + "\xC2\xB0";
        JTextHelper::pushTextAligned(buf, g.x + g.width * 0.25f, ay, g.width * 0.5f, axisHeight(t),
                                     readout, t.readoutText.data(),
                                     JTextHelper::Align::Center, 0.0f);
    }
}

} // inline namespace jf
