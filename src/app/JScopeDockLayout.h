// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "ui/JChannelPanel.h"
#include "ui/JCursorPanel.h"
#include "ui/JGeneratorPanel.h"
#include "ui/JMeasurementPanel.h"
#include "ui/JReplayBar.h"
#include "ui/JTimebasePanel.h"
#include "ui/JTriggerPanel.h"

#include <j/app/JAppWindow.h>
#include <j/core/DockWidget.h>
#include <j/core/SceneGraph.h>

#include <memory>
#include <vector>

inline namespace jf {

class JScopeActions;
class JScopeDriver;

// A dock the View menu can switch on and off, paired with the area it belongs to.
//
// The home area is remembered rather than looked up, because by the time a dock is
// switched back on it may have been torn out and re-docked somewhere else entirely,
// or be nowhere at all. "Where it came from" is the only answer that stays true.
struct JScopeDockToggle {
    JDockWidget* dock{nullptr};
    JDockHost*   home{nullptr};
    const char*  title{nullptr};
    // Offered only by an instrument that has a generator. The dock is real either
    // way; what changes is whether there is anything for it to show.
    bool         needsGenerator{false};
};

// Places the control panels in the window's dock space, each in its own dock so
// they can be tabbed, resized, moved between areas or torn off — a scope gets
// driven differently depending on what is being probed, and a fixed sidebar
// would make that choice once for everybody.
//
// The docks are members here rather than locals because JDockSpace holds raw
// pointers to them for the lifetime of the window.
class JScopeDockLayout {
public:
    JScopeDockLayout(JAppWindow& window, JSceneGraph& graph, JScopeActions& actions,
                     JCursorModel& cursors);

    // Rebuild every panel for a newly opened device, then show what it applied.
    void rebuild(const JScopeCapabilities& caps);
    void syncFrom(const JScopeDriver& driver);

    JChannelPanel&     channels()     { return *m_channelPanel; }
    JTimebasePanel&    timebase()     { return *m_timebasePanel; }
    JTriggerPanel&     trigger()      { return *m_triggerPanel; }
    JMeasurementPanel& measurements() { return *m_measurementPanel; }
    JCursorPanel&      cursors()      { return *m_cursorPanel; }
    JReplayBar&        replayBar()    { return *m_replayBar; }
    JGeneratorPanel&   generator()    { return *m_generatorPanel; }

    // Show the transport only when a capture is what is open. A Replay dock on a
    // live instrument would be a control with nothing to control.
    void setReplayVisible(bool on);

    // The docks the user may show or hide. Replay is deliberately absent: it is
    // governed by whether a capture is open, and offering a switch that the next
    // device change would silently overrule is worse than offering none.
    const std::vector<JScopeDockToggle>& dockToggles() const { return m_toggles; }

    // Whether the OPEN device can fill this dock at all. A panel built from
    // capabilities the instrument does not have is an empty box with a name on
    // it, which reads as a broken feature rather than an absent one -- so the
    // dock is withdrawn and its menu entry greyed instead of being offered.
    bool isDockAvailable(const JScopeDockToggle& t) const;

    // Hiding removes the dock from WHEREVER it currently is, which need not be its
    // home area — a floated dock is still placed, just placed somewhere else.
    void setDockVisible(const JScopeDockToggle& t, bool on);

    // Add a dock this class does not own to the View menu's list. The centre's
    // docks live in the central host rather than in the dock space, but a dock the
    // user can close is a dock the user needs a way back to, wherever it lives --
    // and closing one with no route back is how the centre ended up empty.
    void registerToggle(const JScopeDockToggle& t);
    static bool isDockVisible(const JScopeDockToggle& t) { return t.dock && t.dock->isPlaced(); }

private:
    std::unique_ptr<JChannelPanel>  m_channelPanel;
    std::unique_ptr<JTimebasePanel> m_timebasePanel;
    std::unique_ptr<JTriggerPanel>     m_triggerPanel;
    std::unique_ptr<JMeasurementPanel> m_measurementPanel;
    std::unique_ptr<JCursorPanel>      m_cursorPanel;
    std::unique_ptr<JReplayBar>        m_replayBar;
    std::unique_ptr<JGeneratorPanel>   m_generatorPanel;

    std::unique_ptr<JDockWidget> m_channelDock;
    std::unique_ptr<JDockWidget> m_timebaseDock;
    std::unique_ptr<JDockWidget> m_triggerDock;
    std::unique_ptr<JDockWidget> m_measurementDock;
    std::unique_ptr<JDockWidget> m_cursorDock;
    std::unique_ptr<JDockWidget> m_replayDock;
    std::unique_ptr<JDockWidget> m_generatorDock;
    JDockSpace*                  m_space{nullptr};
    bool                         m_replayVisible{false};
    std::vector<JScopeDockToggle> m_toggles;
    bool                          m_generatorAvailable{false};
};

} // inline namespace jf
