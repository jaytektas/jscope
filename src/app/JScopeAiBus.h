// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeBusCommands.h"

#include <memory>

// THE AI BUS, wired to this application.
//
// JFramework's bus is opt-in: JAppWindow switches it on when the environment
// says JF_AI_BUS=1 (jscope --ai-bus sets that for itself). When it is on, a
// client reads the widget tree and can click and focus anything a person can —
// that much is generic. This adds the application's own verbs (`jf-busctl call
// "<verb>"`, JScopeBusCommands), so the instrument can be set up, run, measured
// and recorded without driving the controls by their screen positions.
//
// The commands are headless and answer for the instrument; this is where the
// window's share of them — recording, cursors, the view, docks, devices,
// screenshot, quit — is bound to JScopeApp.

inline namespace jf {

class JScopeApp;

class JScopeAiBus {
public:
    explicit JScopeAiBus(JScopeApp& app);

    JScopeAiBus(const JScopeAiBus&)            = delete;
    JScopeAiBus& operator=(const JScopeAiBus&) = delete;

    // Answer `jf-busctl call` from now on. A no-op when the bus is off.
    void install();

private:
    JScopeBusHooks _hooks();

    JScopeApp&                         m_app;
    std::unique_ptr<JScopeBusCommands> m_commands;
};

} // inline namespace jf
