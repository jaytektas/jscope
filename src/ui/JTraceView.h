// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JCursorOverlay.h"
#include "JScopeTheme.h"
#include "measure/JCursorModel.h"
#include "scope/JScopeViewState.h"
#include "measure/JTraceDecimator.h"
#include "scope/JScopeCapabilities.h"
#include "scope/JScopeCoupling.h"
#include "scope/JScopeFrame.h"
#include "scope/JScopeLimits.h"

#include <j/core/JWidget.h>
#include <j/graphics/VectorGraphics.h>

#include <array>
#include <vector>

// The scope display: graticule, traces, markers.
//
// A JWidget rather than a JChart wrapper, and deliberately. JChart::addPoint
// erases from the front of a vector on every windowed trim, carries one X range
// and two Y axes where a scope needs eight independent vertical mappings on one
// grid, and draws its own axes and legend with its own layout. It is the right
// tool for the roll/FFT strip charts and the wrong one for a trace.
//
// This widget owns no geometry — the dock space sizes it through setBounds() and
// JAppWindow routes render, mouse, key and scroll to it as the central widget.
// It holds one persistent vertex buffer per channel, reserved to the pixel
// width, so a redraw allocates nothing.

inline namespace jf {

class JTraceView : public JWidget {
public:
    explicit JTraceView(JSceneGraph& graph);

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

    bool handleScroll(float mx, float my, float wheel) override;
    void handleMouseMove(float mx, float my) override;
    void handleMousePress(float mx, float my) override;
    void handleMouseRelease(float mx, float my) override;

    // The frame on display. Copied, not referenced: the pool frame it came from
    // goes straight back to the acquisition thread, and a scope must keep showing
    // the last acquisition after a stop.
    void setFrame(const JScopeFrame& frame);
    void clearFrame();
    bool hasFrame() const { return m_hasFrame; }

    // Channel presentation. Sourced from the driver's config by JScopeApp, so the
    // view never invents a V/div of its own.
    void setChannelView(uint8_t channelId, bool enabled, double voltsPerDiv,
                        double offsetVolts, bool inverted, JScopeCoupling coupling);

    void setGraticule(uint8_t divisionsX, uint8_t divisionsY);

    // What the on-screen legend reports besides the channels: the sweep state,
    // the timebase, and which channel the trigger is on and at what level.
    // Pushed by JScopeApp from the driver, like the channel views — the display
    // reports what the instrument is set to and never invents any of it.
    void setLegend(const std::string& sweepState, double secondsPerDiv,
                   uint8_t triggerChannel, double triggerLevelVolts);

    // ---- vertical position ----
    // Where a channel's 0 V sits on the graticule, in volts, dragged with the
    // channel marker in the left gutter.
    //
    // Applied at DRAW TIME. The 1008C has no vertical offset in hardware at all,
    // and on an instrument whose ranges are coarse that would leave no way to
    // move a trace off another one. An instrument that does have a hardware
    // offset still shifts the samples themselves, and the two compose: the
    // hardware moves the signal within the ADC's range, this moves where it is
    // drawn.
    void   setChannelPosition(uint8_t channelId, double volts);
    double channelPosition(uint8_t channelId) const;

    // ---- trigger ----
    // The level the marker in the right gutter shows, and the channel it is
    // measured against.
    void setTriggerLevel(double volts, uint8_t sourceChannel);

    // Dragged by the user, so the app can push it to the instrument.
    JSignal<double>          onTriggerLevelDragged;
    JSignal<uint8_t, double> onChannelPositionDragged;

    // The horizontal trigger badge was dragged, as a fraction of the record.
    //
    // This is an INSTRUMENT setting, not a display one: the device captures a
    // fixed sweep and this moves where the trigger sits inside it, which is what
    // the OEM software does — one 0xac with a different pre/post split at a
    // constant sum. Panning the view instead slid the record sideways and left
    // the graticule half empty.
    JSignal<double> onTriggerPositionDragged;
    void setFocusedChannel(int channelId) { m_focusedChannel = channelId; invalidate(); }

    // Horizontal zoom/pan, as a fraction of the record. Kept here rather than
    // pushed to the device: zooming into an acquisition already in memory must
    // not require a re-acquire.
    void setViewWindow(double startFraction, double spanFraction);

    // Show exactly `secondsPerDiv * divisionsX` of the record, centred on the
    // trigger. This is what makes s/div mean the same thing here as on any other
    // scope: the device's capture is whatever it is, and the graticule shows the
    // window that was asked for out of it.
    //
    // A window LONGER than the capture is allowed and is drawn short of the full
    // width, because that is the truth — there is no more data.
    void setTimeWindow(double secondsPerDiv);
    double viewStartFraction() const { return m_viewStart; }
    double viewSpanFraction()  const { return m_viewSpan; }
    void   resetViewWindow();

    // Emitted when the user zooms or pans, so a status bar can follow along.
    JSignal<double, double> onViewWindowChanged;

    // ---- cursors ----
    JCursorModel&       cursors()       { return m_cursors; }
    const JCursorModel& cursors() const { return m_cursors; }

    // Place both X cursors at sensible defaults for the frame on display —
    // a quarter and three quarters across — so switching them on puts them
    // somewhere useful rather than both at zero, stacked on the left edge.
    void resetCursorsToFrame();

    // A cursor moved, so the readouts can recompute.
    JSignal<> onCursorsChanged;

    // ---- display state, for persistence ----
    // Gathered into a plain struct rather than read field by field, so the
    // settings layer stays headless and a save cannot silently miss one.
    JScopeViewState viewState() const;
    void            applyViewState(const JScopeViewState& v);

    // Last frame's render cost, split by stage. Read by the app's frame-timing
    // report so a slow redraw can be attributed rather than guessed at.
    double paintMs()  const { return m_paintMs; }
    double flushMs()  const { return m_flushMs; }
    size_t vertices() const { return m_vertices; }

private:
    struct JChannelView {
        bool   enabled{true};
        double voltsPerDiv{1.0};
        double offsetVolts{0.0};
        bool   inverted{false};
        double positionVolts{0.0};   // display-side vertical position
        JScopeCoupling coupling{JScopeCoupling::DC};
    };

    JTraceViewport          _viewportFor(uint8_t channelId, const JRect& plot) const;
    JRect                   _plotRect() const;
    void _paintLegend(const JRect& plot, const JScopeTheme& theme);
    void _paintLegendText(JPrimitiveBuffer& buf, const JRect& plot,
                          const JScopeTheme& theme);

    struct JLegendChip {
        JRect       rect;
        std::string name;
        std::string detail;
        uint8_t     channel{0};
    };
    std::array<JLegendChip, JScopeLimits::kMaxChannels> m_legendChips;
    size_t m_legendChipCount{0};
    JCursorOverlay::JMapping _mappingFor(const JRect& plot) const;

    JScopeFrame m_frame;
    bool        m_hasFrame{false};

    std::array<JChannelView, JScopeLimits::kMaxChannels> m_channels;

    std::string m_sweepState;
    double      m_legendSecondsPerDiv{0.0};
    uint8_t     m_legendTriggerChannel{0};
    double      m_legendTriggerLevel{0.0};
    uint8_t m_divisionsX{10};
    uint8_t m_divisionsY{8};
    int     m_focusedChannel{-1};

    double m_viewStart{0.0};
    double m_viewSpan{1.0};

    JCursorModel            m_cursors;
    JCursorModel::JHandle   m_dragHandle{JCursorModel::JHandle::None};

    // Which marker, if any, is being dragged. -1 for none, -2 for the trigger
    // level, otherwise the channel whose ground marker was grabbed.
    static bool _inBadge(const JRect& r, float mx, float my);

    // Where the trigger sits in the record, 0..1, or negative when the frame
    // carries no trigger.
    double _triggerFraction() const;

    // One badge that was drawn this frame, kept so its label can be pushed after
    // the canvas flush — see populateRenderPrimitives. Reused between frames so
    // the render path stays free of heap traffic.
    struct JBadge {
        JRect       rect;
        std::string label;
    };
    void _recordBadge(const JRect& rect, char label);

    std::array<JBadge, JScopeLimits::kMaxChannels + 1> m_badges{};
    size_t m_badgeCount{0};

    // The horizontal trigger badge as last drawn — empty when the trigger is
    // outside the view window, which is also what makes it unhittable then.
    JRect m_triggerPosRect{};

    // The requested window, and whether it still has to be applied. s/div can be
    // set before any frame has arrived — at device open it always is — and the
    // window cannot be computed without knowing how long a record is, so the
    // request waits for the first frame.
    //
    // Cleared by applyViewState, because a window restored from settings is an
    // explicit choice and must not be overwritten by the default for the
    // timebase it was saved at.
    double m_pendingTimeWindow{0.0};

    // The requested seconds/div, kept so the window can be recomputed when the
    // RECORD changes under it. Changing the timebase moves the device to a
    // different rate code, whose record covers a different span — and a window
    // worked out against the old one then draws the new record short of the
    // graticule, or overflowing it.
    double m_timeWindowSeconds{0.0};
    double m_lastRecordSeconds{0.0};

    static constexpr int kDragNone    = -1;
    static constexpr int kDragTrigger    = -2;
    static constexpr int kDragTriggerPos = -3;
    int  m_dragMarker{kDragNone};

    double  m_triggerLevel{0.0};
    uint8_t m_triggerSource{0};

    bool  m_panning{false};
    float m_panAnchorX{0.0f};
    double m_panAnchorStart{0.0};

    JVectorCanvas m_canvas;
    std::array<JTraceDecimator::JPoints, JScopeLimits::kMaxChannels> m_scratch;
    float m_reservedWidth{0.0f};

    double m_paintMs{0.0};
    double m_flushMs{0.0};
    size_t m_vertices{0};
};

} // inline namespace jf
