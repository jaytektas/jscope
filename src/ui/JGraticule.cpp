#include "JGraticule.h"

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

    // Minor ticks first, so a major line always draws over them.
    if (theme.minorPerMajor > 1) {
        const float mx = dx / static_cast<float>(theme.minorPerMajor);
        const float my = dy / static_cast<float>(theme.minorPerMajor);
        for (int i = 1; i < divisionsX * theme.minorPerMajor; ++i) {
            if (i % theme.minorPerMajor == 0) continue;      // that is a major line
            const float px = x + i * mx;
            canvas.drawLine(px, y, px, y + h, theme.markerStrokeWidth, minorPaint);
        }
        for (int i = 1; i < divisionsY * theme.minorPerMajor; ++i) {
            if (i % theme.minorPerMajor == 0) continue;
            const float py = y + i * my;
            canvas.drawLine(x, py, x + w, py, theme.markerStrokeWidth, minorPaint);
        }
    }

    for (uint8_t i = 1; i < divisionsX; ++i) {
        const float px = x + i * dx;
        canvas.drawLine(px, y, px, y + h, theme.markerStrokeWidth, majorPaint);
    }
    for (uint8_t i = 1; i < divisionsY; ++i) {
        const float py = y + i * dy;
        canvas.drawLine(x, py, x + w, py, theme.markerStrokeWidth, majorPaint);
    }

    // The centre axes carry the zero reference and are read constantly, so they
    // are their own role rather than a brighter major line.
    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    canvas.drawLine(cx, y, cx, y + h, theme.markerStrokeWidth, centrePaint);
    canvas.drawLine(x, cy, x + w, cy, theme.markerStrokeWidth, centrePaint);

    canvas.strokeRect(x, y, w, h, theme.markerStrokeWidth, borderPaint);
}

} // inline namespace jf
