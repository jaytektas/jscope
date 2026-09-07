#include "JScopeDockLayout.h"

#include <algorithm>

#include "scope/JScopeDriver.h"
#include "scope/JScopeLog.h"
#include "ui/JScopeTheme.h"

inline namespace jf {

namespace {
// Dock geometry. These are the window's furniture rather than the scope's, so
// they sit here beside the docks they size rather than in JScopeTheme, which
// describes the instrument display.
constexpr float kDockWidth      = 260.0f;
constexpr float kDockHeight     = 240.0f;
constexpr float kDockMinWidth   = 180.0f;
constexpr float kDockMinHeight  = 90.0f;
constexpr float kRightAreaWidth = 280.0f;
constexpr float kLeftAreaWidth  = 210.0f;
constexpr float kBottomAreaHeight = 200.0f;
}

JScopeDockLayout::JScopeDockLayout(JAppWindow& window, JSceneGraph& graph,
                                   JScopeActions& actions, JCursorModel& cursors) {
    m_channelPanel  = std::make_unique<JChannelPanel>(graph, actions);
    m_timebasePanel = std::make_unique<JTimebasePanel>(graph, actions);
    m_triggerPanel  = std::make_unique<JTriggerPanel>(graph, actions);
    m_measurementPanel = std::make_unique<JMeasurementPanel>(graph);
    m_cursorPanel      = std::make_unique<JCursorPanel>(graph, cursors);
    m_replayBar        = std::make_unique<JReplayBar>(graph);
    m_generatorPanel   = std::make_unique<JGeneratorPanel>(graph, actions);

    m_channelDock  = std::make_unique<JDockWidget>("Channels",  0.f, 0.f, kDockWidth, kDockHeight);
    m_timebaseDock = std::make_unique<JDockWidget>("Timebase",  0.f, 0.f, kDockWidth, kDockHeight);
    m_triggerDock  = std::make_unique<JDockWidget>("Trigger",   0.f, 0.f, kDockWidth, kDockHeight);
    m_measurementDock = std::make_unique<JDockWidget>("Measure", 0.f, 0.f, kDockWidth, kDockHeight);
    m_cursorDock      = std::make_unique<JDockWidget>("Cursors", 0.f, 0.f, kDockWidth, kDockHeight);
    m_replayDock      = std::make_unique<JDockWidget>("Replay",  0.f, 0.f, kDockWidth, kDockHeight);
    m_generatorDock   = std::make_unique<JDockWidget>("Generator", 0.f, 0.f, kDockWidth, kDockHeight);

    m_channelDock->setMinSize(kDockMinWidth, kDockMinHeight);
    m_timebaseDock->setMinSize(kDockMinWidth, kDockMinHeight);
    m_triggerDock->setMinSize(kDockMinWidth, kDockMinHeight);
    m_measurementDock->setMinSize(kDockMinWidth, kDockMinHeight);
    m_cursorDock->setMinSize(kDockMinWidth, kDockMinHeight);
    m_replayDock->setMinSize(kDockMinWidth, kDockMinHeight);
    m_generatorDock->setMinSize(kDockMinWidth, kDockMinHeight);

    m_channelDock->setContent(m_channelPanel.get());
    m_timebaseDock->setContent(m_timebasePanel.get());
    m_triggerDock->setContent(m_triggerPanel.get());
    m_measurementDock->setContent(m_measurementPanel.get());
    m_cursorDock->setContent(m_cursorPanel.get());
    m_replayDock->setContent(m_replayBar.get());
    m_generatorDock->setContent(m_generatorPanel.get());

    JDockSpace& space = window.dockSpace();
    space.setRightWidth(kRightAreaWidth);
    space.setLeftWidth(kLeftAreaWidth);   // without this the left area has no width

    // Channels on the right on its own — it is the tallest and the most used.
    // Timebase and Trigger share the left area as tabs: they are consulted when
    // setting up a measurement and then largely left alone.
    space.right().addDock(m_channelDock.get());
    space.left().addDock(m_timebaseDock.get());
    space.left().addDock(m_triggerDock.get());     // second dock in an area tabs
    // The Generator is NOT added here. It belongs to the left area and tabs
    // alongside these two, but only once an instrument that has one is open --
    // rebuild() puts it there. Adding it now would show an empty panel to anyone
    // who has not connected anything, which is every user at startup.

    // Measure and Cursors go together at the bottom: they are read side by side
    // while probing, and they are the two panels that want horizontal room for
    // their readout columns rather than vertical room for controls.
    space.setBottomHeight(kBottomAreaHeight);
    // Cursors first so that Measure, added last, is the tab that comes up — the
    // readouts are useful the moment a trace appears, whereas cursors need
    // placing before they say anything.
    space.bottom().addDock(m_cursorDock.get());
    space.bottom().addDock(m_measurementDock.get());

    m_space = &space;

    // Built here, where each dock's home area is still in front of us. Replay is
    // left out on purpose -- see dockToggles().
    m_toggles = {
        { m_channelDock.get(),     &space.right(),  "Channels" },
        { m_timebaseDock.get(),    &space.left(),   "Timebase" },
        { m_triggerDock.get(),     &space.left(),   "Trigger"  },
        { m_cursorDock.get(),      &space.bottom(), "Cursors"  },
        { m_measurementDock.get(), &space.bottom(), "Measure"  },
        { m_generatorDock.get(),   &space.left(),   "Generator", true },
    };

    JLOGC(JScopeLog::kUi, JLogLevel::Info)
        << "dock layout built: Channels right, Timebase+Trigger left, "
           "Measure+Cursors bottom (Replay and Generator appear when a capture "
           "or an instrument with a generator is open)";
}

void JScopeDockLayout::setDockVisible(const JScopeDockToggle& t, bool on) {
    if (!t.dock || !t.home || on == isDockVisible(t)) return;
    if (on) {
        t.home->addDock(t.dock);
    } else if (JDockHost* owner = t.dock->placedIn()) {
        // Not t.home: a dock that was torn out and re-docked elsewhere is owned by
        // whoever holds it now, and asking its home area to remove it would do nothing.
        owner->removeDock(t.dock);
    }
    JLOGC(JScopeLog::kUi, JLogLevel::Info)
        << "dock '" << t.title << "' " << (on ? "shown" : "hidden");
}

void JScopeDockLayout::setReplayVisible(bool on) {
    if (!m_space || on == m_replayVisible) return;
    if (on) m_space->bottom().addDock(m_replayDock.get());
    else    m_space->bottom().removeDock(m_replayDock.get());
    m_replayVisible = on;
    JLOGC(JScopeLog::kUi, JLogLevel::Info)
        << "replay transport " << (on ? "shown" : "hidden");
}

bool JScopeDockLayout::isDockAvailable(const JScopeDockToggle& t) const {
    if (t.needsGenerator || t.dock == m_generatorDock.get()) return m_generatorAvailable;
    return true;
}

void JScopeDockLayout::registerToggle(const JScopeDockToggle& t) {
    if (t.dock && t.home) m_toggles.push_back(t);
}

void JScopeDockLayout::rebuild(const JScopeCapabilities& caps) {
    m_channelPanel->rebuild(caps);
    m_generatorPanel->rebuild(caps);

    // Withdraw the Generator when this instrument has none. It is a tab in the
    // left area, so leaving it in place would put an empty panel one click away
    // from Timebase and invite the reasonable conclusion that it is broken.
    const bool hadGenerator = m_generatorAvailable;
    m_generatorAvailable = (caps.generator.kind != JScopeGeneratorKind::None);
    if (m_generatorAvailable != hadGenerator) {
        const auto it = std::find_if(m_toggles.begin(), m_toggles.end(),
            [this](const JScopeDockToggle& t) { return t.dock == m_generatorDock.get(); });
        if (it != m_toggles.end()) setDockVisible(*it, m_generatorAvailable);
        JLOGC(JScopeLog::kUi, JLogLevel::Info)
            << "generator dock " << (m_generatorAvailable ? "shown" : "withdrawn")
            << ": this instrument "
            << (m_generatorAvailable ? "has a generator" : "has none");
    }
    m_timebasePanel->rebuild(caps);
    m_triggerPanel->rebuild(caps);
    m_measurementPanel->rebuild(caps);
    m_cursorPanel->rebuild(caps);
}

void JScopeDockLayout::syncFrom(const JScopeDriver& driver) {
    m_generatorPanel->syncFrom(driver);
    m_channelPanel->syncFrom(driver);
    m_timebasePanel->syncFrom(driver);
    m_triggerPanel->syncFrom(driver);
}

} // inline namespace jf
