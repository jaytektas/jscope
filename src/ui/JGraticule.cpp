// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JGraticule.h"
#include <j/graphics/VectorGraphics.h>

inline namespace jf {

void JGraticule::draw(JVectorCanvas& canvas, float x, float y, float w, float h,
                      uint8_t divisionsX, uint8_t divisionsY, const JScopeTheme& theme) {
    if (w <= 0.0f || h <= 0.0f || divisionsX == 0 || divisionsY == 0) return;

    const float dx = w / static_cast<float>(divisionsX);
    const float dy = h / static_cast<float>(divisionsY);
    const JPaint majorPaint  = JPaint::solid(theme.graticuleMajor);
    const JPaint minorPaint  = JPaint::solid(theme.graticuleMinor);
    const JPaint centrePaint = JPaint::solid(theme.graticuleCentre);
    const JPaint borderPaint = JPaint::solid(theme.graticuleBorder);
    const float sw           = theme.markerStrokeWidth;

    // Minor ticks first, so a major line always draws over them.
    if (theme.minorPerMajor > 1) {
        const float mx = dx / static_cast<float>(theme.minorPerMajor);
        const float my = dy / static_cast<float>(theme.minorPerMajor);

        // Batch all vertical minor ticks into one stroke call.
        // Each tick is its own subpath so there are no connecting lines.
        canvas.beginPath();
        for (int i = 1; i < divisionsX * theme.minorPerMajor; ++i) {
            if (i % theme.minorPerMajor == 0) continue;   // that is a major line
            const float px = x + i * mx;
            canvas.moveTo(px, y);
            canvas.lineTo(px, y + h);
            canvas.close();
        }
        canvas.stroke(sw, minorPaint, JLineCap::Butt, JLineJoin::Miter);

        // Batch all horizontal minor ticks.
        canvas.beginPath();
        for (int i = 1; i < divisionsY * theme.minorPerMajor; ++i) {
            if (i % theme.minorPerMajor == 0) continue;
            const float py = y + i * my;
            canvas.moveTo(x, py);
            canvas.lineTo(x + w, py);
            canvas.close();
        }
        canvas.stroke(sw, minorPaint, JLineCap::Butt, JLineJoin::Miter);
    }

    // Batch all vertical major ticks.
    canvas.beginPath();
    for (uint8_t i = 1; i < divisionsX; ++i) {
        const float px = x + i * dx;
        canvas.moveTo(px, y);
        canvas.lineTo(px, y + h);
        canvas.close();
    }
    canvas.stroke(sw, majorPaint, JLineCap::Butt, JLineJoin::Miter);

    // Batch all horizontal major ticks.
    canvas.beginPath();
    for (uint8_t i = 1; i < divisionsY; ++i) {
        const float py = y + i * dy;
        canvas.moveTo(x, py);
        canvas.lineTo(x + w, py);
        canvas.close();
    }
    canvas.stroke(sw, majorPaint, JLineCap::Butt, JLineJoin::Miter);

    // The centre axes carry the zero reference and are read constantly, so they
    // are their own role rather than a brighter major line.
    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    canvas.drawLine(cx, y, cx, y + h, sw, centrePaint);
    canvas.drawLine(x, cy, x + w, cy, sw, centrePaint);

    canvas.strokeRect(x, y, w, h, sw, borderPaint);
}

} // inline namespace jf
