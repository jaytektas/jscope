#pragma once

#include <j/core/DockManager.h>
#include <j/core/JWidget.h>

#include <functional>

inline namespace jf {

// THE CENTRE OF THE WINDOW, MADE INTO A DOCK HOST.
//
// JDockSpace gives the four edges dock hosts and the centre a single widget, and
// says what to do about it: "the centre is NOT a dock host ... if you actually
// want docks in the centre, opt in by making that widget a dock host." JDockHost
// is not a JWidget, so this is the adapter that makes one look like one.
//
// The point of doing it at all is that the centre's tabs become real docks --
// tabbed, split, and TORN OUT into their own windows like any other. Floating
// windows are created and owned by the runner, which holds the float list, their
// GPU surfaces and the drag state, so a tear-out is reported through onWantsFloat
// and the application hands it to JAppWindow::floatDock.
//
// An embedded host does not get what the runner gives its own areas: no splitter
// grab pad, no resize cursors, and no routing of input into the docked content.
// All three are supplied here, and the last one matters most -- without it the
// widget inside a centre dock receives no mouse at all, so a trace would render
// and then ignore every click.
//
// Modelled on jomnidyno's DashboardArea, which solved this first.
class JCentreDockHost : public JWidget {
public:
    explicit JCentreDockHost(JSceneGraph& graph);

    // Raised when a tab is dragged out of the strip. The application forwards it
    // to JAppWindow::floatDock; only the runner can make the window.
    std::function<void(JDockHost*, JDockWidget*)> onWantsFloat;

    // Raised when a dock's close button is used, so the View menu can follow.
    std::function<void(JDockWidget*)> onDockClosed;

    void addDock(JDockWidget* dock);
    JDockHost& host() { return m_host; }

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;
    void handleMouseMove(float mx, float my) override;
    void handleMousePress(float mx, float my) override;
    void handleMouseRelease(float mx, float my) override;
    bool handleScroll(float mx, float my, float wheel) override;
    bool handleKeyEvent(const JKeyEvent& ke) override;

    // Keep the registry's hit rect on this host in step with the window, so a
    // float dragged over the centre can be dropped back into it. Called every
    // frame because the window can move without this widget hearing about it.
    void refreshRegistration(int windowScreenX, int windowScreenY);

private:
    void _route(float mx, float my, bool pressed, bool released);
    JDockWidget* _activeDockOf(JDockNodeId id);
    JWidget*     _activeContent();

    JDockHost    m_host;
    JDockWidget* m_contentCapture{nullptr};
    JDockWidget* m_hovered{nullptr};
};

} // inline namespace jf
