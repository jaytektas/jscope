// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JCursorOverlay.h"

#include "measure/JTraceDecimator.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

void JCursorOverlay::draw(JVectorCanvas& canvas, const JCursorModel& cursors,
                          const JMapping& map, const JTraceViewport& vp,
                          const JScopeTheme& theme) {
    const JPaint xPaint = JPaint::solid(theme.cursorX);
    const JPaint yPaint = JPaint::solid(theme.cursorY);

    if (cursors.xEnabled()) {
        for (double t : { cursors.x1(), cursors.x2() }) {
            const float px = map.timeToX(t);
            if (px < map.x || px > map.x + map.width) continue;
            canvas.drawLine(px, vp.y, px, vp.y + vp.height, theme.cursorWidth, xPaint);
        }
    }

    if (cursors.yEnabled()) {
        for (double v : { cursors.y1(), cursors.y2() }) {
            const float py = JTraceDecimator::voltsToY(v, vp);
            if (py < vp.y || py > vp.y + vp.height) continue;
            canvas.drawLine(map.x, py, map.x + map.width, py, theme.cursorWidth, yPaint);
        }
    }
}

JCursorModel::JHandle JCursorOverlay::hitTest(const JCursorModel& cursors, float mx, float my,
                                              const JMapping& map, const JTraceViewport& vp,
                                              const JScopeTheme& theme) {
    const float slop = theme.cursorHitSlopPixels;

    // X cursors first: they are the ones dragged most, and a vertical line is
    // easier to aim at than a horizontal one on a wide plot.
    if (cursors.xEnabled()) {
        if (std::abs(mx - map.timeToX(cursors.x1())) <= slop) return JCursorModel::JHandle::X1;
        if (std::abs(mx - map.timeToX(cursors.x2())) <= slop) return JCursorModel::JHandle::X2;
    }
    if (cursors.yEnabled()) {
        if (std::abs(my - JTraceDecimator::voltsToY(cursors.y1(), vp)) <= slop)
            return JCursorModel::JHandle::Y1;
        if (std::abs(my - JTraceDecimator::voltsToY(cursors.y2(), vp)) <= slop)
            return JCursorModel::JHandle::Y2;
    }
    return JCursorModel::JHandle::None;
}

} // inline namespace jf
