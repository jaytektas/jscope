// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeTheme.h"
#include <j/graphics/VectorGraphics.h>
#include <cstdint>

// Draws the division grid a scope is read against.
//
// Not a widget: it has no geometry of its own and no input. It is given a
// rectangle and a canvas and it strokes into them, which is what lets the trace
// view, a future XY view and a future zoom pane all draw the same grid from the
// same code rather than three near-identical copies.

inline namespace jf {

class JGraticule {
public:
    // `divisionsX` / `divisionsY` come from the driver's capabilities, so the
    // grid always matches the graticule the device's V/div and s/div are defined
    // against rather than assuming the usual eight-by-ten.
    static void draw(JVectorCanvas& canvas, float x, float y, float w, float h,
                     uint8_t divisionsX, uint8_t divisionsY, const JScopeTheme& theme);
};

} // inline namespace jf
