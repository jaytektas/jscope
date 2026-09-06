#include "JCursorModel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

inline namespace jf {

void JCursorModel::moveHandle(JHandle h, double value) {
    switch (h) {
        case JHandle::X1: m_x1 = value; break;
        case JHandle::X2: m_x2 = value; break;
        case JHandle::Y1: m_y1 = value; break;
        case JHandle::Y2: m_y2 = value; break;
        case JHandle::None: break;
    }
}

bool JCursorModel::sampleWindow(double sampleInterval, uint32_t sampleCount,
                                size_t& firstOut, size_t& countOut) const {
    if (!m_xEnabled || sampleInterval <= 0.0 || sampleCount == 0) return false;

    // The cursors are not ordered — a user drags them past one another all the
    // time — so the window is between them whichever way round they sit.
    const double lo = std::min(m_x1, m_x2);
    const double hi = std::max(m_x1, m_x2);
    if (hi <= lo) return false;

    // ROUND to the nearest sample, do not truncate. 2.0e-3 / 1.0e-5 is
    // 199.99999999999997 in binary, and a cast turns that into 199 — the cursor
    // silently lands one sample before where it was placed. The same truncation
    // trap that made the ADC model flicker between adjacent codes.
    const double last = static_cast<double>(sampleCount);
    const double a = std::clamp(std::round(lo / sampleInterval), 0.0, last);
    const double b = std::clamp(std::round(hi / sampleInterval), 0.0, last);

    firstOut = static_cast<size_t>(a);
    countOut = static_cast<size_t>(b - a);
    return countOut > 0;
}

} // inline namespace jf
