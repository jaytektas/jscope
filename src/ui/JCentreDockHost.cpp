#include "JCentreDockHost.h"

#include "scope/JScopeLog.h"

#include <j/core/DockRegistry.h>

inline namespace jf {

JCentreDockHost::JCentreDockHost(JSceneGraph& graph) : JWidget(graph, "JCentreDockHost") {}

void JCentreDockHost::addDock(JDockWidget* dock) {
    if (!dock) return;
    // Before it is placed, not after: the dock's own title bar reads this to
    // decide whether a title drag should begin a tear-out at all.
    dock->setFloatable(false);
    m_host.addDock(dock);
    _layoutToBounds();
}

void JCentreDockHost::_layoutToBounds() {
    const JRect b = bounds();
    // The host recomputes its whole tree, so only do it when the rect actually
    // moved -- this is called from render, which is every frame.
    if (b.x == m_lastRect.x && b.y == m_lastRect.y &&
        b.width == m_lastRect.width && b.height == m_lastRect.height)
        return;
    m_lastRect = b;
    m_host.computeLayout(b);
    JDockRegistry::instance().registerHost(m_host, m_screenX + static_cast<int>(b.x),
                                           m_screenY + static_cast<int>(b.y),
                                           static_cast<uint32_t>(b.width),
                                           static_cast<uint32_t>(b.height));
}

void JCentreDockHost::refreshRegistration(int windowScreenX, int windowScreenY) {
    m_screenX = windowScreenX;
    m_screenY = windowScreenY;
    m_lastRect = {};      // force the registration to be reissued at the new origin
    _layoutToBounds();
}

void JCentreDockHost::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    _layoutToBounds();
    m_host.populateRenderPrimitives(buf);
}

void JCentreDockHost::handleMouseMove(float mx, float my) {
    m_host.handleMouse(mx, my, /*pressed=*/false, /*released=*/false);
}

void JCentreDockHost::handleMousePress(float mx, float my) {
    // Close is the only event that can arrive: these docks are not floatable, so
    // the host never asks for a tear-out.
    if (auto ev = m_host.handleMouse(mx, my, /*pressed=*/true, /*released=*/false))
        if (ev->type == JDockHost::JDockEvent::JType::CloseRequested && ev->dock) {
            m_host.removeDock(ev->dock);
            JLOGC(JScopeLog::kUi, JLogLevel::Info)
                << "centre dock closed: " << ev->dock->title();
        }
}

void JCentreDockHost::handleMouseRelease(float mx, float my) {
    m_host.handleMouse(mx, my, /*pressed=*/false, /*released=*/true);
}

} // inline namespace jf
