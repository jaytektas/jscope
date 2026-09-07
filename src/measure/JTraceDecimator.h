// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JTraceViewport.h"
#include <j/graphics/VectorGraphics.h>
#include <cstdint>
#include <vector>

// Reduce a plane of samples to the vertices a polyline needs, and no more.
//
// A scope routinely holds far more samples than the display has pixels — a DSO
// deep record is millions against a couple of thousand. Drawing every one is
// both ruinous and pointless. But STRIDE SAMPLING IS WRONG: a glitch narrower
// than one pixel column would simply not be picked, and a scope that hides
// glitches is not a scope. So each pixel column emits its span's minimum and
// maximum, which draws a vertical bar covering everything that happened in that
// column. That is the difference between an instrument and a plot, and it is the
// first thing test_decimator asserts.
//
// Cost is two vertices per column regardless of record length: a 4000-pixel view
// of an 8 Mpt record is 8000 vertices. The counts->volts->y conversion is applied
// only to those, never to every sample; the inner loop compares int16_t.
//
// Below one sample per column it passes samples straight through, so a short
// record gets no decimation artefacts it did not need.
//
// Never allocates once `out` has been reserved to 2 * width — reserve() does that
// and the caller keeps the buffer between frames.

inline namespace jf {

class JTraceDecimator {
public:
    using JPoints = std::vector<JVectorCanvas::JVec2>;

    // Room for the worst case at this pixel width. Call when the width changes.
    static void reserve(JPoints& out, float pixelWidth);

    // Which path decimate() took. It matters to the caller because an Envelope
    // is axis-aligned — every segment vertical or a one-pixel horizontal step —
    // so anti-aliasing it buys nothing, while Interpolated traces are diagonal
    // and genuinely need it.
    enum class JPath { Empty, Interpolated, Envelope };

    // Clears `out` (keeping capacity) and fills it with the polyline for one
    // plane. `countsToVolts` and `zeroOffsetCounts` come from the frame header.
    static JPath decimate(const int16_t* samples, size_t sampleTotal,
                          float countsToVolts, float zeroOffsetCounts,
                          const JTraceViewport& vp, JPoints& out);

    // Screen y for one voltage under this viewport. Shared with the graticule and
    // the cursors so a trace and its readout can never disagree about where a
    // volt is.
    static float voltsToY(double volts, const JTraceViewport& vp);

    // The inverse, for hit-testing a dragged cursor back into volts.
    static double yToVolts(float y, const JTraceViewport& vp);
};

} // inline namespace jf
