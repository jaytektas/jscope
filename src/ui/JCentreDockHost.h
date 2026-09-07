#pragma once

#include <j/core/DockManager.h>
#include <j/core/JWidget.h>

inline namespace jf {

// THE CENTRE OF THE WINDOW, MADE INTO A DOCK HOST.
//
// JDockSpace gives the four edges dock hosts and the centre a single widget, and
// says so in as many words: "The centre is NOT a dock host — it holds one widget
// directly ... If you actually want docks in the centre, opt in by making that
// widget a dock host." This is that opt-in.
//
// JDockHost is not a JWidget -- it is a standalone thing with computeLayout,
// populateRenderPrimitives and handleMouse -- so this is the adapter that makes
// one look like a widget. It owns the host and forwards.
//
// TEAR-OUT DOES NOT WORK FROM HERE, and the docks placed in it are marked
// non-floatable to say so honestly. Floating is driven by JAppWindow::spawnFloat,
// which is private and wired only to the four edge hosts through JDockSpace; an
// app cannot reach it. A dock that looked draggable and then did nothing when
// dragged would be worse than one that plainly is not. Docks can still be tabbed
// and split in here, and -- because the host registers with JDockRegistry -- a
// float torn from an edge can be dropped into it.
class JCentreDockHost : public JWidget {
public:
    explicit JCentreDockHost(JSceneGraph& graph);

    // Docks added here are made non-floatable first; see the note above.
    void addDock(JDockWidget* dock);

    JDockHost& host() { return m_host; }

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;
    void handleMouseMove(float mx, float my) override;
    void handleMousePress(float mx, float my) override;
    void handleMouseRelease(float mx, float my) override;

    // Keep the registry's idea of where this host sits in line with the window,
    // so a float dragged over the centre finds it. Called when the window moves.
    void refreshRegistration(int windowScreenX, int windowScreenY);

private:
    void _layoutToBounds();

    JDockHost m_host;
    JRect     m_lastRect{};
    int       m_screenX{0};
    int       m_screenY{0};
};

} // inline namespace jf
