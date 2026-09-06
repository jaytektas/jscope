#pragma once

#include "JScopeTheme.h"
#include "scope/JScopeCapabilities.h"
#include "scope/JScopeTriggerConfig.h"

#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDoubleSpinBox.h>
#include <j/core/JLabel.h>

#include <vector>

inline namespace jf {

class JScopeActions;
class JScopeDriver;

// Trigger source, slope, level and sweep mode.
//
// Only the sweep modes the device reports are offered. A device whose hardware
// has no sweep mode at all can still advertise all three, because Auto/Normal/
// Single are host-side policy where the instrument declines to make the choice —
// but that is the driver's decision to declare, not this panel's to assume.
class JTriggerPanel : public JContainer {
public:
    JTriggerPanel(JSceneGraph& graph, JScopeActions& actions);

    void rebuild(const JScopeCapabilities& caps);
    void syncFrom(const JScopeDriver& driver);

private:
    JSceneGraph&   m_graph;
    JScopeActions& m_actions;

    JScopeCapabilities             m_caps;
    std::vector<JScopeTriggerMode> m_modes;    // only those the device supports

    JComboBox*      m_mode{nullptr};
    JComboBox*      m_source{nullptr};
    JComboBox*      m_slope{nullptr};
    JDoubleSpinBox* m_level{nullptr};
    JLabel*         m_state{nullptr};

    bool m_syncing{false};
};

} // inline namespace jf
