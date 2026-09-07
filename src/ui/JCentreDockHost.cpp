#include "JCentreDockHost.h"

#include "scope/JScopeLog.h"

#include <j/core/DockRegistry.h>

inline namespace jf {

JCentreDockHost::JCentreDockHost(JSceneGraph& graph) : JWidget(graph, "JCentreDockHost") {
    // The runner supplies a splitter grab pad and resize cursors for its OWN areas.
    // An embedded host gets neither, and the reserved strip alone is a few pixels
    // wide, so the seam between two centre docks would be almost impossible to hit.
    m_host.options().handleHoverPad    = 4.0f;
    m_host.options().showResizeCursors = true;
}

void JCentreDockHost::addDock(JDockWidget* dock) {
    if (!dock) return;
    m_host.addDock(dock);
    m_host.computeLayout(bounds());
}

void JCentreDockHost::refreshRegistration(int windowScreenX, int windowScreenY) {
    const JRect r = bounds();
    // Hit rect and origin are the same thing here: this host is not scrolled or
    // transformed inside the window, it simply sits where its bounds say.
    JDockRegistry::instance().registerHostEx(
        m_host,
        windowScreenX + static_cast<int>(r.x), windowScreenY + static_cast<int>(r.y),
        static_cast<uint32_t>(r.width), static_cast<uint32_t>(r.height),
        windowScreenX + static_cast<int>(r.x), windowScreenY + static_cast<int>(r.y));
}

void JCentreDockHost::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    m_host.computeLayout(bounds());
    m_host.populateRenderPrimitives(buf);
    // The drop-target overlay a drag paints over this host. Without it a dock
    // dragged across the centre shows no indication of where it would land.
    m_host.populateOverlay(buf);
}

void JCentreDockHost::handleMouseMove(float mx, float my) {
    m_host.computeLayout(bounds());
    // The runner applies s_hoverCursor after this dispatch, so setting it here
    // takes effect on the same frame.
    switch (m_host.getHoverCursor(mx, my)) {
        case JDockHost::JHoverCursor::Horiz:
            JWidget::s_hoverCursor = JPlatformCursor::ResizeLeftRight; break;
        case JDockHost::JHoverCursor::Vert:
            JWidget::s_hoverCursor = JPlatformCursor::ResizeUpDown; break;
        default: break;
    }
    _route(mx, my, false, false);
}

void JCentreDockHost::handleMousePress(float mx, float my)   { _route(mx, my, true, false); }
void JCentreDockHost::handleMouseRelease(float mx, float my) { _route(mx, my, false, true); }

void JCentreDockHost::_route(float mx, float my, bool pressed, bool released) {
    m_host.computeLayout(bounds());   // keep the tree's rects current for hit-testing

    if (auto ev = m_host.handleMouse(mx, my, pressed, released)) {
        if (ev->type == JDockHost::JDockEvent::JType::CloseRequested) {
            JDockWidget* closed = ev->dock;
            if (closed == m_contentCapture) m_contentCapture = nullptr;
            m_host.removeDock(closed);
            JLOGC(JScopeLog::kUi, JLogLevel::Info) << "centre dock closed: " << closed->title();
            if (onDockClosed) onDockClosed(closed);
        } else if (ev->type == JDockHost::JDockEvent::JType::WantsFloat && onWantsFloat) {
            onWantsFloat(&m_host, ev->dock);   // only the runner can make the window
        }
        return;
    }

    // Not a structural gesture, so it belongs to whatever is docked here. WITHOUT
    // THIS the content gets no mouse at all: the trace would draw and then ignore
    // every click, drag and cursor.
    //
    // The target is captured on press so a drag that strays over the tab bar still
    // reaches the widget it started in.
    if (pressed) m_contentCapture = m_host.contentDockAt(mx, my);
    JDockWidget* d = m_contentCapture ? m_contentCapture : m_host.contentDockAt(mx, my);

    // TELL THE ONE IT LEFT. A widget only hears about the pointer while it is over
    // it, so a dock the pointer moves off keeps whatever hover state it had --
    // visibly, in the editor's case, as a highlighted cell sitting under nothing.
    // A move far outside any sane bounds is how it is told to drop that.
    if (d != m_hovered && m_hovered && m_hovered->content())
        m_hovered->content()->handleMouseMove(-1.0f, -1.0f);
    m_hovered = d;
    if (d && d->content()) {
        JWidget* c = d->content();
        c->handleMouseMove(mx, my);
        if (pressed)  c->handleMousePress(mx, my);
        if (released) c->handleMouseRelease(mx, my);
    }
    if (released) m_contentCapture = nullptr;
}

bool JCentreDockHost::handleScroll(float mx, float my, float wheel) {
    m_host.computeLayout(bounds());
    if (JDockWidget* d = m_host.contentDockAt(mx, my))
        if (JWidget* c = d->content()) return c->handleScroll(mx, my, wheel);
    return false;
}

bool JCentreDockHost::handleKeyEvent(const JKeyEvent& ke) {
    if (JWidget* c = _activeContent()) return c->handleKeyEvent(ke);
    return false;
}

JWidget* JCentreDockHost::_activeContent() {
    JDockWidget* d = _activeDockOf(m_host.rootId());
    return d ? d->content() : nullptr;
}

// Descend to the first leaf and take its active tab. With a single centre leaf
// that is simply the visible dock; with a split it is the first pane's, which is
// the best answer available without a focus notion this host does not have.
JDockWidget* JCentreDockHost::_activeDockOf(JDockNodeId id) {
    const JDockNode* n = m_host.node(id);
    if (!n) return nullptr;
    if (n->type == JDockNode::JType::Leaf) {
        if (n->activeTab < 0 || n->activeTab >= static_cast<int>(n->tabs.size())) return nullptr;
        return n->tabs[n->activeTab];
    }
    for (JDockNodeId child : n->children)
        if (JDockWidget* d = _activeDockOf(child)) return d;
    return nullptr;
}

} // inline namespace jf
