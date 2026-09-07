// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeTheme.h"
#include "measure/JCursorModel.h"
#include "measure/JTraceViewport.h"

#include <j/graphics/VectorGraphics.h>

inline namespace jf {

// Draws the measurement cursors over the trace, and hit-tests them.
//
// Not a widget, for the same reason JGraticule is not: it owns no geometry. It
// is handed the plot rect and the time/volt mapping, and the cursors it draws
// live in signal units, so they stay on the feature they were placed against
// through any amount of zooming and panning.
class JCursorOverlay {
public:
    // `secondsPerPixel` and `tStart` map the plot's x axis onto record time.
    struct JMapping {
        float  x{0.0f}, width{0.0f};
        double tStart{0.0};
        double secondsPerPixel{0.0};

        float  timeToX(double t) const {
            return (secondsPerPixel > 0.0)
                 ? static_cast<float>(x + (t - tStart) / secondsPerPixel) : x;
        }
        double xToTime(float px) const { return tStart + (px - x) * secondsPerPixel; }
    };

    static void draw(JVectorCanvas& canvas, const JCursorModel& cursors,
                     const JMapping& map, const JTraceViewport& vp,
                     const JScopeTheme& theme);

    // Which handle, if any, is under the pointer. Returns None when nothing is
    // within the theme's hit slop.
    static JCursorModel::JHandle hitTest(const JCursorModel& cursors, float mx, float my,
                                         const JMapping& map, const JTraceViewport& vp,
                                         const JScopeTheme& theme);
};

} // inline namespace jf
