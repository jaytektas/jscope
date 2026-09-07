// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JTriggerPanel.h"

#include "app/JScopeActions.h"
#include "scope/JScopeDriver.h"
#include "scope/JScopeLog.h"

#include <algorithm>
#include <memory>

inline namespace jf {

JTriggerPanel::JTriggerPanel(JSceneGraph& graph, JScopeActions& actions)
    : JContainer(graph), m_graph(graph), m_actions(actions) {
    const JScopeTheme& t = JScopeTheme::current();
    setLayoutMode(JLayoutMode::Form);
    setGap(t.panelGap);
    setPadding(JEdges(t.panelPadding));
}

void JTriggerPanel::rebuild(const JScopeCapabilities& caps) {
    m_caps = caps;
    m_modes.clear();
    m_mode = nullptr; m_source = nullptr; m_slope = nullptr;
    m_level = nullptr; m_state = nullptr;
    clear();

    const JScopeTheme& t = JScopeTheme::current();

    // Offer only the sweep modes this device declares.
    for (JScopeTriggerMode m : { JScopeTriggerMode::Auto, JScopeTriggerMode::Normal,
                                 JScopeTriggerMode::Single })
        if (jScopeTriggerModeSupported(caps.triggerModes, m)) m_modes.push_back(m);

    if (!m_modes.empty()) {
        std::vector<std::string> items;
        for (JScopeTriggerMode m : m_modes) items.push_back(jScopeTriggerModeName(m));
        add(std::make_unique<JLabel>(m_graph, "Sweep", t.panelLabelWidth, t.panelRowHeight));
    m_mode = add(std::make_unique<JComboBox>(m_graph, std::move(items),
                                                 t.panelFieldWidth, t.panelRowHeight));
        m_mode->onIndexChanged.connect([this](int i) {
            if (m_syncing || i < 0 || i >= static_cast<int>(m_modes.size())) return;
            m_actions.setTriggerMode(m_modes[i]);
        });
    }

    std::vector<std::string> sources;
    for (const JScopeChannelCaps& c : caps.channels) sources.push_back(c.label);
    if (!sources.empty()) {
        add(std::make_unique<JLabel>(m_graph, "Source", t.panelLabelWidth, t.panelRowHeight));
    m_source = add(std::make_unique<JComboBox>(m_graph, std::move(sources),
                                                   t.panelFieldWidth, t.panelRowHeight));
        m_source->onIndexChanged.connect([this](int i) {
            if (m_syncing || i < 0) return;
            m_actions.setTriggerSource(static_cast<uint8_t>(i));
        });
    }

    add(std::make_unique<JLabel>(m_graph, "Slope", t.panelLabelWidth, t.panelRowHeight));
    m_slope = add(std::make_unique<JComboBox>(m_graph,
        std::vector<std::string>{ jScopeTriggerSlopeName(JScopeTriggerSlope::Rising),
                                  jScopeTriggerSlopeName(JScopeTriggerSlope::Falling),
                                  jScopeTriggerSlopeName(JScopeTriggerSlope::Either) },
        t.panelFieldWidth, t.panelRowHeight));
    m_slope->onIndexChanged.connect([this](int i) {
        if (m_syncing) return;
        m_actions.setTriggerSlope(static_cast<JScopeTriggerSlope>(std::clamp(i, 0, 2)));
    });

    // The level's range is the full vertical span of the source channel, which
    // is the only range at which a trigger can actually fire.
    double span = 1.0;
    if (!caps.channels.empty() && !caps.channels[0].voltsPerDiv.empty())
        span = caps.channels[0].voltsPerDiv.back() * caps.verticalDivisions;
    add(std::make_unique<JLabel>(m_graph, "Level", t.panelLabelWidth, t.panelRowHeight));
    m_level = add(std::make_unique<JDoubleSpinBox>(m_graph, -span, span, 0.0, 3,
                                                   t.panelFieldWidth, t.panelRowHeight));
    m_level->setSuffix(" V");
    m_level->onValueChanged.connect([this](double v) {
        if (m_syncing) return;
        m_actions.setTriggerLevel(v);
    });

    add(std::make_unique<JLabel>(m_graph, "State", t.panelLabelWidth, t.panelRowHeight));
    m_state = add(std::make_unique<JLabel>(m_graph, "", t.panelFieldWidth, t.panelRowHeight));

    JLOGC(JScopeLog::kUi, JLogLevel::Info)
        << "trigger panel rebuilt: " << m_modes.size() << " sweep mode(s), "
        << int(caps.channelCount()) << " source(s)"
        << (caps.hasHardwareTrigger ? ", hardware trigger" : ", host-side trigger");
    invalidate();
}

void JTriggerPanel::syncFrom(const JScopeDriver& driver) {
    const JScopeTriggerConfig& cfg = driver.triggerConfig();
    m_syncing = true;

    if (m_mode) {
        const auto it = std::find(m_modes.begin(), m_modes.end(), cfg.mode);
        if (it != m_modes.end())
            m_mode->setCurrentIndex(static_cast<int>(it - m_modes.begin()));
    }
    if (m_source) m_source->setCurrentIndex(cfg.sourceChannel);
    if (m_slope)  m_slope->setCurrentIndex(static_cast<int>(cfg.slope));
    if (m_level)  m_level->setValue(cfg.levelVolts);

    // Armed and Triggered are the states a user watches while probing, so the
    // panel says which one it is in rather than leaving it to the status bar.
    if (m_state) m_state->setText(jScopeStateName(driver.state()));

    m_syncing = false;
}

} // inline namespace jf
