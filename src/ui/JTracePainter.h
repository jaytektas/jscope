#pragma once

#include "JScopeTheme.h"
#include "measure/JTraceDecimator.h"
#include "scope/JScopeFrame.h"
#include <j/graphics/VectorGraphics.h>
#include <j/core/SceneGraph.h>
#include <string>

// Strokes one channel's trace, plus its ground marker.
//
// Not a widget, for the same reason JGraticule is not: it owns no geometry. It
// is handed a plane, a viewport and a scratch buffer, and it emits exactly one
// strokePolyline. The scratch buffer belongs to the caller and is reused between
// frames, which is what keeps the render path free of heap traffic.

inline namespace jf {

class JTracePainter {
public:
    // `plane` is the index within the frame, not the device channel id — the
    // colour is chosen from header.channelIds[plane] so CH3 stays magenta even
    // when CH1 and CH2 are switched off and it is the only plane present.
    static void paint(JVectorCanvas& canvas, const JScopeFrame& frame, uint8_t plane,
                      const JTraceViewport& vp, const JScopeTheme& theme,
                      bool focused, JTraceDecimator::JPoints& scratch);

    // A diagonal trace, stroked in runs broken at sharp reversals so a miter
    // join can never throw a spike into empty space. See the .cpp for why.
    static void paintInterpolated(JVectorCanvas& canvas, const JTraceDecimator::JPoints& pts,
                                  const JColor& color, float width, const JScopeTheme& theme);

    // An envelope is a run of vertical bars, one per pixel column. Drawing it as
    // rects rather than stroking it as a polyline is both cheaper and steadier:
    // no per-vertex miter maths, and a bar snapped to whole pixels covers the
    // same pixels every frame instead of alternating between one and two.
    static void paintEnvelope(JVectorCanvas& canvas, const JTraceDecimator::JPoints& columns,
                              const JColor& color, float width);

    // A LABELLED BADGE marking a level on the graticule's edge — the shape the
    // DSO2000 manual calls a "Channel Marker" (its callout 11) and a "Trigger
    // level" (callout 12): a small filled tab carrying the channel digit or a
    // "T", in the channel's colour, sitting to the LEFT of `edgeX` with a tab
    // pointing into the grid at the exact height that level maps to.
    //
    // Labelled rather than a bare arrow because with eight channels a row of
    // identical triangles says nothing about which trace each one moves, and
    // because these are the handles you grab — an unlabelled one is a guess.
    //
    // Returns the rectangle it occupies, which is also its hit target: the thing
    // drawn and the thing grabbed are then the same shape by construction.
    //
    // The LABEL is not drawn here. JVectorCanvas holds geometry and glyphs go
    // straight to the buffer, and flush() re-emits everything the canvas has
    // accumulated rather than draining it — so text pushed before the view's
    // single flush ends up buried under a second copy of every badge. Callers
    // keep the returned rect and call paintBadgeLabel after they flush.
    static JRect paintLevelBadge(JVectorCanvas& canvas, float edgeX, float y,
                                 const JColor& color, const JScopeTheme& theme,
                                 bool active, bool clampedOffscreen);

    // ONE CHANNEL'S LEGEND CHIP: a name box in the channel's own colour, then
    // its volts/div in that colour on a dark ground — the OEM software's footer,
    // which is where a user looks to see what a trace is measured at without
    // leaving the display.
    //
    // Split like the badges, and for the same reason: the canvas is flushed
    // between the two halves, and a flush re-emits rather than drains, so text
    // drawn with the geometry would be buried under a second copy of it.
    static float legendChipWidth(const std::string& detail, const JScopeTheme& theme);
    static float paintLegendChip(JVectorCanvas& canvas, float x, float y, float height,
                                 const std::string& detail,
                                 const JColor& channelColour, const JScopeTheme& theme);
    static void  paintLegendChipText(JPrimitiveBuffer& buf, const JRect& chip,
                                     const std::string& name, const std::string& detail,
                                     const JColor& channelColour, const JScopeTheme& theme);

    // The digit or "T" that names a badge, centred in its body.
    static void paintBadgeLabel(JPrimitiveBuffer& buf, const JRect& badge,
                                const std::string& label, const JScopeTheme& theme);

    // Where a badge WOULD be drawn, without drawing it. Hit-testing uses this so
    // a marker cannot be grabbable anywhere other than where it appears.
    static JRect levelBadgeRect(float edgeX, float y, const JScopeTheme& theme);

    // Where the trigger sits horizontally within the record: the same badge
    // turned through ninety degrees, sitting on the graticule's top edge with
    // its point on the trigger's column. Unlabelled — there is only ever one,
    // so nothing needs distinguishing.
    static JRect paintTriggerPosition(JVectorCanvas& canvas, float x, float topY,
                                      const JColor& color, const JScopeTheme& theme,
                                      bool active);

    static JRect triggerPositionRect(float x, float topY, const JScopeTheme& theme);
};

} // inline namespace jf
