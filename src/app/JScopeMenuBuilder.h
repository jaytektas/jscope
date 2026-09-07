#pragma once

#include <j/app/JAppWindow.h>
#include <j/core/MenuSystem.h>
#include <j/core/SceneGraph.h>

inline namespace jf {

class JScopeApp;
class JScopeDockLayout;

// Builds the menu bar. A class rather than a JScopeApp method so that the app
// stays a wiring object and the menu tree lives somewhere it can be read in one
// sitting.
//
// The menus it creates outlive the call — JAppWindow holds pointers to them —
// so they are owned by a registry inside this translation unit rather than being
// locals that would dangle the moment run() started.
class JScopeMenuBuilder {
public:
    static void build(JAppWindow& window, JSceneGraph& graph, JScopeApp& app);

    // Rebuild the Instrument menu from whatever the OPEN driver publishes.
    //
    // Called when a device opens, because the menu is built before one has been:
    // at that point there is no instrument to ask, and the settings a scope has
    // are not knowable until it answers.
    static void refreshInstrumentMenu(JSceneGraph& graph, JScopeApp& app);

    // Correct the View menu's dock ticks from what the window actually shows.
    // Docks appear and disappear by routes the menu never hears about -- a dock's
    // own close button, a tear-out dropped nowhere -- so the ticks are re-derived
    // rather than remembered.
    static void syncViewMenu(const JScopeDockLayout& docks);
};

} // inline namespace jf
