// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JChannelStrip.h"
#include "scope/JScopeCapabilities.h"

#include <j/core/JContainer.h>
#include <j/core/JScrollArea.h>
#include <memory>
#include <vector>

inline namespace jf {

class JScopeActions;
class JScopeDriver;

// One strip per channel the device reports. Rebuilt whenever a device is
// opened, so switching from the 2-channel DSO2D15 to the 8-channel 1008C
// changes the panel rather than leaving six dead controls behind.
// Eight channels of six labelled controls is taller than any dock, so the strips
// live in a scroll area rather than being silently cut off at whichever channel
// runs past the bottom.
class JChannelPanel : public JScrollArea {
public:
    JChannelPanel(JSceneGraph& graph, JScopeActions& actions);

    void rebuild(const JScopeCapabilities& caps);
    void syncFrom(const JScopeDriver& driver);
    void clearStrips();

private:
    JSceneGraph&                 m_graph;
    JScopeActions&               m_actions;
    std::vector<JChannelStrip*>  m_strips;   // owned by the container
};

} // inline namespace jf
