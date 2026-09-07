// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstddef>
#include <cstdint>

inline namespace jf {

// The two pairs of measurement cursors, in signal units rather than pixels.
//
// Seconds and volts, not screen coordinates, so a cursor stays on the feature it
// was placed against when the view is zoomed, panned or resized. Converting to
// pixels is the renderer's job and happens at draw time.
class JCursorModel {
public:
    enum class JHandle : uint8_t { None, X1, X2, Y1, Y2 };

    void setXEnabled(bool on) { m_xEnabled = on; }
    void setYEnabled(bool on) { m_yEnabled = on; }
    bool xEnabled() const { return m_xEnabled; }
    bool yEnabled() const { return m_yEnabled; }

    void setX(double t1, double t2) { m_x1 = t1; m_x2 = t2; }
    void setY(double v1, double v2) { m_y1 = v1; m_y2 = v2; }

    double x1() const { return m_x1; }
    double x2() const { return m_x2; }
    double y1() const { return m_y1; }
    double y2() const { return m_y2; }

    void moveHandle(JHandle h, double value);

    // The channel the Y cursors are read against. Y is in volts, and volts mean
    // different screen positions per channel, so a Y readout without a channel
    // is meaningless.
    void    setYChannel(uint8_t ch) { m_yChannel = ch; }
    uint8_t yChannel() const { return m_yChannel; }

    double deltaX() const { return m_x2 - m_x1; }
    double deltaY() const { return m_y2 - m_y1; }

    // 1/dX — the frequency a period between the cursors corresponds to, which is
    // the reason anyone places X cursors on two edges in the first place.
    bool   haveFrequency() const { return deltaX() != 0.0; }
    double frequency() const { return haveFrequency() ? 1.0 / deltaX() : 0.0; }

    // The sample window the X cursors bracket, for measuring between them.
    // Returns false when the cursors are disabled or degenerate.
    bool sampleWindow(double sampleInterval, uint32_t sampleCount,
                      size_t& firstOut, size_t& countOut) const;

private:
    bool    m_xEnabled{false};
    bool    m_yEnabled{false};
    double  m_x1{0.0}, m_x2{0.0};
    double  m_y1{0.0}, m_y2{0.0};
    uint8_t m_yChannel{0};
};

} // inline namespace jf
