// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JChannelPanel.h"

#include "app/JScopeActions.h"
#include "scope/JScopeDriver.h"
#include "scope/JScopeLog.h"

#include <memory>

inline namespace jf {

JChannelPanel::JChannelPanel(JSceneGraph& graph, JScopeActions& actions)
    : JScrollArea(graph), m_graph(graph), m_actions(actions) {
    const JScopeTheme& t = JScopeTheme::current();
    setContentPadding(t.panelPadding, t.panelPadding, t.panelGap);
}

void JChannelPanel::clearStrips() {
    m_strips.clear();
    clearChildren();          // destroys the owned strips
}

void JChannelPanel::rebuild(const JScopeCapabilities& caps) {
    clearStrips();
    for (uint8_t i = 0; i < caps.channelCount(); ++i) {
        m_strips.push_back(addChildWidget(std::make_unique<JChannelStrip>(
            m_graph, i, caps.channels[i], m_actions)));
    }

    JLOGC(JScopeLog::kUi, JLogLevel::Info)
        << "channel panel rebuilt for " << int(caps.channelCount())
        << " channel(s) of " << caps.model;
    invalidate();
}

void JChannelPanel::syncFrom(const JScopeDriver& driver) {
    for (JChannelStrip* s : m_strips)
        s->syncFrom(driver.channelConfig(s->channelId()),
                    driver.capabilities().channels[s->channelId()]);
}

} // inline namespace jf
