#pragma once

#include "JScopeTheme.h"
#include "scope/JScopeCapabilities.h"

#include <j/core/JCheckBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/JSpinBox.h>

#include <cstdint>
#include <vector>

inline namespace jf {

class JScopeActions;
class JScopeDriver;

// The 1008C's eight digital outputs, presented as the TOOTHED WHEEL they exist to
// imitate rather than as 62 raw bytes.
//
// The device takes an arbitrary pattern, one byte per step, and a raw editor
// would expose that faithfully and be almost unusable: 62 steps across 8 lines is
// nearly 500 switches, and nobody arrives wanting to set switch 293. What people
// arrive wanting is a crank or cam signal, which is a wheel with a tooth count and
// a gap where some teeth are missing — the 60-2 that most engines use. So the
// wheel is what this asks for, and the pattern is derived.
//
// A tooth is two steps, high then low, because a sensor sees an edge at each side
// of it. That is why the step budget is halved: 62 steps is 31 teeth.
//
// SPEED IS SHOWN TWICE, deliberately. The device advances one step per whole tick
// of a fixed clock, so most speeds cannot be hit exactly, and the achieved one
// also moves when the tooth count changes. Showing only what was asked for would
// misreport the signal on the wire. The OEM shows both, labelled "Set Speed" and
// "Real Speed", for exactly this reason.
class JGeneratorPanel : public JContainer {
public:
    JGeneratorPanel(JSceneGraph& graph, JScopeActions& actions);

    void rebuild(const JScopeCapabilities& caps);
    void syncFrom(const JScopeDriver& driver);

private:
    void _pushPattern();
    void _showSpeeds();

    JSceneGraph&   m_graph;
    JScopeActions& m_actions;

    JScopeCapabilities m_caps;

    JCheckBox* m_output{nullptr};
    JSpinBox*  m_rpm{nullptr};
    JLabel*    m_realRpm{nullptr};
    JLabel*    m_maxRpm{nullptr};
    JSpinBox*  m_pulses{nullptr};
    std::vector<JCheckBox*> m_lines;   // one per digital output

    bool m_syncing{false};
};

} // inline namespace jf
