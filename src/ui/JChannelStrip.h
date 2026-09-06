#pragma once

#include "JScopeTheme.h"
#include "scope/JScopeCapabilities.h"
#include "scope/JScopeChannelConfig.h"

#include <j/core/JCheckBox.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDoubleSpinBox.h>
#include <j/core/JLabel.h>

#include <cstdint>
#include <functional>
#include <memory>

inline namespace jf {

class JScopeActions;

// One channel's controls: enable, volts/div, offset, coupling, probe, invert.
//
// Built from that channel's JScopeChannelCaps, so a control the device does not
// have is not created at all rather than created and ignored. The 1008C has no
// selectable coupling and no offset control; its strip simply will not show
// them, and nothing here needs to know which device it is talking to.
class JChannelStrip : public JContainer {
public:
    JChannelStrip(JSceneGraph& graph, uint8_t channelId,
                  const JScopeChannelCaps& caps, JScopeActions& actions);

    uint8_t channelId() const { return m_channelId; }

    // Refresh every control from what the driver actually applied. Called after
    // any change, so a control can never display a value the hardware refused.
    // Caps come in with every sync because the LADDER CAN MOVE: on a device
    // where the app owns the probe, changing it rescales every step. The strip
    // does no arithmetic on it — whatever the driver publishes is what is shown.
    void syncFrom(const JScopeChannelConfig& cfg, const JScopeChannelCaps& caps);

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    void _buildRows(JSceneGraph& graph, const JScopeChannelCaps& caps);

    uint8_t             m_channelId;
    JScopeActions&      m_actions;
    JScopeChannelCaps   m_caps;

    // Owned by the container; these are wiring handles, not lifetimes.
    JCheckBox*      m_enable{nullptr};
    JComboBox*      m_voltsPerDiv{nullptr};
    JDoubleSpinBox* m_offset{nullptr};
    JComboBox*      m_coupling{nullptr};
    JComboBox*      m_probe{nullptr};
    JCheckBox*      m_invert{nullptr};

    // Guards the read-back: syncFrom() writes the controls, and a control that
    // signals on programmatic change would then write straight back to the
    // driver, and so on. One flag, set only while syncing.
    bool m_syncing{false};

    // The probe ratio the V/div list is currently built for. The list is at the
    // probe tip, so it has to be rebuilt when the ratio changes.
};

} // inline namespace jf
