#pragma once

#include "ui/JChannelPanel.h"
#include "ui/JCursorPanel.h"
#include "ui/JMeasurementPanel.h"
#include "ui/JReplayBar.h"
#include "ui/JTimebasePanel.h"
#include "ui/JTriggerPanel.h"

#include <j/app/JAppWindow.h>
#include <j/core/DockWidget.h>
#include <j/core/SceneGraph.h>

#include <memory>

inline namespace jf {

class JScopeActions;
class JScopeDriver;

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

    // Show the transport only when a capture is what is open. A Replay dock on a
    // live instrument would be a control with nothing to control.
    void setReplayVisible(bool on);

private:
    std::unique_ptr<JChannelPanel>  m_channelPanel;
    std::unique_ptr<JTimebasePanel> m_timebasePanel;
    std::unique_ptr<JTriggerPanel>     m_triggerPanel;
    std::unique_ptr<JMeasurementPanel> m_measurementPanel;
    std::unique_ptr<JCursorPanel>      m_cursorPanel;
    std::unique_ptr<JReplayBar>        m_replayBar;

    std::unique_ptr<JDockWidget> m_channelDock;
    std::unique_ptr<JDockWidget> m_timebaseDock;
    std::unique_ptr<JDockWidget> m_triggerDock;
    std::unique_ptr<JDockWidget> m_measurementDock;
    std::unique_ptr<JDockWidget> m_cursorDock;
    std::unique_ptr<JDockWidget> m_replayDock;
    JDockSpace*                  m_space{nullptr};
    bool                         m_replayVisible{false};
};

} // inline namespace jf
