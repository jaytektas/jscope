// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/graphics/VectorGraphics.h>
#include "scope/JScopeLimits.h"
#include <cstdint>
#include <string>

// Every visual constant the scope display uses.
//
// JFramework's rule is that no widget carries a dimension or colour of its own —
// it all resolves through the theme. JStyle is that theme, but it is a fixed
// struct in a shipped SDK and this app must not edit the framework, so the scope
// vocabulary JStyle has no words for (eight trace colours, a graticule, cursors,
// trigger markers) lives here instead. Same pattern, same guarantee:
// JScopeTheme::current(), seeded from JStyle roles wherever one already exists so
// the two palettes cannot drift, and reseeded when the app theme changes.
//
// NO FILE UNDER src/ui/ CONTAINS A NUMERIC LITERAL. If a widget needs a number,
// it is a field here.

inline namespace jf {

struct JScopeTheme {
    // ---- surface ----
    JColor background;
    JColor graticuleMajor;
    JColor graticuleMinor;
    JColor graticuleCentre;
    JColor graticuleBorder;

    // ---- traces ----
    // One per channel. Chosen to stay distinguishable at a one-pixel stroke and
    // to survive the common forms of colour blindness in adjacent pairs.
    JColor channelTrace[JScopeLimits::kMaxChannels];
    float  traceWidth;
    float  traceWidthFocused;

    // Anti-aliasing is split because the two trace paths need different amounts
    // of it. JVectorCanvas emits TWO extra gradient quads per segment when AA is
    // on, tripling the geometry and adding four square roots per segment — which
    // measured at 13ms of a 28ms frame for four traces. An Envelope trace is
    // axis-aligned (every segment vertical, or a one-pixel horizontal step), so
    // AA buys it nothing and costs it everything; an Interpolated trace is
    // diagonal and genuinely needs it. Chrome keeps its own value.
    float  traceAntiAliasPixels;      // Interpolated traces (samples <= pixels)
    float  envelopeAntiAliasPixels;   // Envelope traces (samples > pixels)
    float  chromeAntiAliasPixels;     // graticule, markers, cursors

    // How sharp a turn has to be before the trace is broken rather than joined.
    // Stored as the cosine of the turn measured from straight-on, so 0.7 breaks
    // at about 135 degrees and above.
    float  traceMiterReversalCosine;

    // ---- markers ----
    // Levels are marked by pentagonal badges in the gutter OUTSIDE the
    // graticule's left edge, all in one lane — the DSO2000's own arrangement:
    // one per enabled channel carrying its digit at 0 V, and a "T" badge at the
    // trigger level in its source channel's colour.
    //
    // markerGutterLeft must hold a badge; markerGutterRight is only breathing
    // room, so a trace at the right edge is not flush with it.
    float  markerGutterLeft;
    float  markerGutterRight;
    float  markerBadgeWidth;    // body plus point
    float  markerBadgeHeight;
    float  markerBadgePoint;    // width of the triangular tip alone
    float  markerEmphasisWidth; // the drag outline and the clamped-off-screen bar
    JColor markerLabelText;     // the digit or "T" drawn on a badge

    // Channel badges take their channel's trace colour, and so does an armed
    // trigger badge; triggerMarker is the neutral used for the horizontal
    // trigger-position arrow and for a trigger that is not armed.
    JColor triggerMarker;
    float  markerStrokeWidth;

    // ---- reference trace ----
    // The modelled "what a healthy one looks like" overlay. Dimmer than any
    // channel colour and drawn thin, because it must never be mistaken for a
    // measurement -- the eye should read it as the paper behind the trace.
    JColor referenceTrace;
    JColor referenceLabelText;
    float  referenceWidth;
    float  referenceInsetFraction;   // margin above and below, so peaks clear the grid border

    // ---- cursors ----
    JColor cursorX;
    JColor cursorY;
    JColor cursorLabelText;
    JColor cursorLabelFill;
    float  cursorWidth;
    float  cursorHitSlopPixels;
    float  cursorLabelPadding;
    float  cursorLabelHeight;

    // ---- readouts ----
    JColor readoutText;
    JColor readoutMutedText;
    JColor readoutWarnText;
    float  readoutPadding;
    float  readoutLineHeight;

    // THE LEGEND, in the OEM software's arrangement: a row of per-channel chips
    // under the graticule saying what each trace is measured at, and a strip
    // above it carrying the sweep state, the timebase and the trigger. Reading
    // volts/div off the side panel means looking away from the trace; every
    // scope puts it on the display for that reason.
    float  legendHeight;        // the footer row of channel chips
    float  legendChipGap;       // between one chip and the next
    float  legendChipPadding;   // inside a chip, around its text
    float  legendNameWidth;     // the channel-coloured name box, e.g. "CH1"
    JColor legendChipBackground;
    JColor legendNameText;      // drawn ON the channel colour, so it contrasts
    JColor legendHeaderText;    // the neutral used for the header's own labels
    JColor legendTimebaseText;  // "Time:" — the OEM puts this in its own colour
    JColor legendStateText;     // "Auto" / "Trig'd" / "Stop"

    // ---- control panels ----
    // Panels are widgets too, so their dimensions are theme values like any
    // other. A control that computed its own row height would be exactly the
    // hardcoded constant the project rules forbid.
    float panelPadding;
    float panelGap;
    float panelRowHeight;
    float panelLabelWidth;
    float panelFieldWidth;
    float panelSwatchSize;      // the colour block identifying a channel
    float panelIndent;

    // ---- geometry ----
    float   viewPadding;          // inset from the widget rect to the graticule
    uint8_t minorPerMajor;        // minor ticks between major graticule lines
    float   centreTickLength;     // length of the centre-axis tick marks

    // The live theme. Mutate it, or assign a whole one with apply().
    static JScopeTheme& current();
    static void apply(JScopeTheme t);

    // Rebuild the JStyle-derived roles from the framework's current palette.
    // Called at startup and whenever a stylesheet is reloaded, so a swap of the
    // app theme carries the scope display with it.
    static void reseedFromStyle();

    // Trace colour for a channel index, wrapping if there are more channels than
    // colours — better a repeated colour than an out-of-bounds read.
    const JColor& traceColor(uint8_t channel) const {
        return channelTrace[channel % JScopeLimits::kMaxChannels];
    }
};

} // inline namespace jf
