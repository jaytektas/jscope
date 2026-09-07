// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeTheme.h"
#include "measure/JMeasurementEngine.h"
#include "scope/JScopeCapabilities.h"

#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>

#include <array>
#include <vector>

inline namespace jf {

class JScopeFrame;

// Automatic measurement readouts for one channel.
//
// The channel is chosen here rather than showing all of them: eight channels of
// thirteen measurements is a hundred numbers, which is a table nobody reads. A
// scope shows a handful for the channel you are working on.
class JMeasurementPanel : public JContainer {
public:
    JMeasurementPanel(JSceneGraph& graph);

    void rebuild(const JScopeCapabilities& caps);

    // Recompute from the frame on display. `window` is the cursor window when
    // the X cursors are on, or the whole record when they are not — the same
    // code path either way.
    void update(const JScopeFrame& frame, JMeasurementWindow window = {});

    uint8_t channel() const { return m_channel; }
    JSignal<uint8_t> onChannelChanged;

private:
    JSceneGraph& m_graph;
    uint8_t      m_channel{0};

    JComboBox*  m_channelSelect{nullptr};
    std::array<JLabel*, static_cast<size_t>(JMeasurementKind::Count_)> m_values{};

    bool m_syncing{false};
};

} // inline namespace jf
