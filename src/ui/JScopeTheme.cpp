// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JScopeTheme.h"
#include "scope/JScopeLog.h"
#include <j/core/JStyle.h>

inline namespace jf {

namespace {

// The default scope palette. Trace colours follow the convention most bench
// scopes use for the first four channels (yellow, cyan, magenta, green) so the
// display reads correctly to anyone who has used one, then continue with four
// more that stay separable from those and from each other.
JScopeTheme makeDefault() {
    JScopeTheme t{};

    t.background      = rgb (16,  18,  22);
    t.graticuleMajor  = rgba(70,  74,  84,  200);
    t.graticuleMinor  = rgba(46,  49,  56,  140);
    t.graticuleCentre = rgba(104, 110, 124, 230);
    t.graticuleBorder = rgba(88,  93,  104, 255);

    t.channelTrace[0] = rgb (255, 214,  64);   // CH1 yellow
    t.channelTrace[1] = rgb ( 64, 214, 255);   // CH2 cyan
    t.channelTrace[2] = rgb (255, 110, 200);   // CH3 magenta
    t.channelTrace[3] = rgb (120, 230, 120);   // CH4 green
    t.channelTrace[4] = rgb (255, 150,  70);   // CH5 orange
    t.channelTrace[5] = rgb (150, 160, 255);   // CH6 periwinkle
    t.channelTrace[6] = rgb (230, 230, 230);   // CH7 white
    t.channelTrace[7] = rgb (200, 130, 255);   // CH8 violet

    t.traceWidth              = 1.5f;
    t.traceWidthFocused       = 2.0f;
    t.traceAntiAliasPixels    = 1.0f;
    t.envelopeAntiAliasPixels = 0.0f;
    t.chromeAntiAliasPixels   = 1.0f;
    t.traceMiterReversalCosine = 0.7f;

    // Larger than the instrument's own 15 px: the DSO2D15 draws these on an
    // 800x480 panel and you drag them with a finger-sized encoder, whereas here
    // they are a mouse target on a window several times that wide.
    t.markerBadgeWidth  = 22.0f;
    t.markerBadgeHeight = 18.0f;
    t.markerBadgePoint  = 7.0f;
    t.markerEmphasisWidth = 2.0f;
    // Derived rather than typed, so widening a badge cannot silently push it off
    // the edge of the widget.
    t.markerGutterLeft  = t.markerBadgeWidth + 2.0f;
    t.markerGutterRight = 6.0f;
    // Dark text on the channel's own colour, which is what makes a badge
    // readable against eight different trace colours.
    t.markerLabelText   = rgb(16, 18, 22);
    t.triggerMarker     = rgb (255, 160,  40);
    t.markerStrokeWidth = 1.0f;

    // Deliberately desaturated and half-transparent: a reference sitting at full
    // strength competes with the live trace it exists to be compared against.
    t.referenceTrace      = rgba(150, 160, 185, 110);
    t.referenceLabelText  = rgba(150, 160, 185, 200);
    t.referenceWidth      = 1.0f;
    t.referenceInsetFraction = 0.06f;

    t.cursorX             = rgba(255, 255, 255, 150);
    t.cursorY             = rgba(255, 255, 255, 110);
    t.cursorLabelText     = rgb (235, 238, 245);
    t.cursorLabelFill     = rgba( 20,  22,  30, 235);
    t.cursorWidth         = 1.0f;
    t.cursorHitSlopPixels = 6.0f;
    t.cursorLabelPadding  = 4.0f;
    t.cursorLabelHeight   = 16.0f;

    t.readoutText      = rgb (220, 224, 232);
    t.readoutMutedText = rgb (150, 154, 165);
    t.readoutWarnText  = rgb (255, 159,  10);
    t.readoutPadding   = 6.0f;
    t.readoutLineHeight = 16.0f;

    t.legendHeight        = 20.0f;
    t.legendChipGap       = 6.0f;
    t.legendChipPadding   = 5.0f;
    t.legendNameWidth     = 30.0f;
    t.legendChipBackground = rgb(15, 16, 18);
    t.legendNameText       = rgb(13, 14, 16);
    t.legendHeaderText     = rgb(184, 189, 199);
    t.legendTimebaseText   = rgb(242, 158, 48);
    t.legendStateText      = rgb(90, 216, 97);

    t.panelPadding    = 8.0f;
    t.panelGap        = 6.0f;
    // Sized so eight channel strips fit the right dock without a scroll bar.
    // Eight channels x three rows is twenty-four rows of chrome before any
    // signal is drawn, so a few pixels a row is the difference between reaching
    // CH8 by looking and reaching it by scrolling.
    t.panelRowHeight  = 19.0f;
    t.panelLabelWidth = 74.0f;
    t.panelFieldWidth = 116.0f;
    t.panelSwatchSize = 12.0f;
    t.panelIndent     = 10.0f;

    t.viewPadding     = 8.0f;
    t.minorPerMajor   = 5;
    t.centreTickLength = 4.0f;

    return t;
}

} // namespace

JScopeTheme& JScopeTheme::current() {
    static JScopeTheme inst = makeDefault();
    return inst;
}

void JScopeTheme::apply(JScopeTheme t) {
    current() = t;
    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "scope theme applied";
}

void JScopeTheme::reseedFromStyle() {
    JScopeTheme& t = current();
    const JStyle& s = JStyle::current();

    // Only the roles JStyle genuinely owns. Everything else is the scope's own
    // vocabulary and stays as the default (or as a stylesheet set it).
    t.background      = JColor::fromArray(s.ChartBg);
    t.graticuleMinor  = JColor::fromArray(s.GridLine);
    t.readoutText     = JColor::fromArray(s.ChartLegendText);
    t.readoutMutedText= JColor::fromArray(s.ChartAxisText);
    t.readoutWarnText = JColor::fromArray(s.Warning);
    t.cursorLabelText = JColor::fromArray(s.ChartTooltipText);
    t.cursorLabelFill = JColor::fromArray(s.ChartTooltipBg);

    JLOGC(JScopeLog::kUi, JLogLevel::Debug)
        << "scope theme reseeded from JStyle (bg "
        << int(t.background.r) << "," << int(t.background.g) << "," << int(t.background.b) << ")";
}

} // inline namespace jf
