// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JScopeBusHooks.h"

#include <j/config/Json.h>

#include <functional>
#include <string>
#include <vector>

// THE AI BUS'S VERBS: everything a person can do in the window, as a line of text.
//
//     jf-busctl call "status"
//     jf-busctl call "channel 5 on vdiv=2 coupling=DC"
//     jf-busctl call "edges 5 6 level=2.5"
//
// Every answer is JSON beginning {"ok":true or {"ok":false,"error":...}, the
// same shape jayecu's studio answers in, so one client reads both. The verb list
// is `help`, and docs/ai-bus.md describes each one.
//
// Settings go through JScopeActions — the capability checks, the read-back and
// the refusal reasons a button gets are the ones the bus gets — and the reply
// carries the READ-BACK, so a request the instrument quantised or refused says
// what it actually did. Anything only the window can do goes through the hooks
// (see JScopeBusHooks).
//
// Main thread only. The bus services one action per frame inside the render
// loop, and nothing here touches the acquisition thread: frames are read from
// the session's latest, which the main thread owns.

inline namespace jf {

class JScopeActions;
class JScopeSession;
class JScopeFrame;

class JScopeBusCommands {
public:
    JScopeBusCommands(JScopeSession& session, JScopeActions& actions, JScopeBusHooks hooks = {});
    ~JScopeBusCommands();

    JScopeBusCommands(const JScopeBusCommands&)            = delete;
    JScopeBusCommands& operator=(const JScopeBusCommands&) = delete;

    // Answer one command line. False when the verb is not one of these (the
    // caller may have others); true with `reply` set otherwise, error or not.
    bool handle(const std::string& line, std::string& reply);

    // The verbs, for `help` and the docs.
    static const std::vector<std::pair<std::string, std::string>>& verbs();

private:
    using Args = std::vector<std::string>;

    JJson _status() const;
    JJson _capabilities() const;
    JJson _channel(uint8_t ch) const;
    JJson _timebase() const;
    JJson _trigger() const;
    JJson _generator() const;
    JJson _replay() const;

    bool _doChannel  (const Args& a, JJson& out, std::string& why);
    bool _doTimebase (const Args& a, JJson& out, std::string& why);
    bool _doTrigger  (const Args& a, JJson& out, std::string& why);
    bool _doAcquire  (const std::string& verb, JJson& out, std::string& why);
    bool _doOption   (const Args& a, JJson& out, std::string& why);
    bool _doGenerator(const Args& a, JJson& out, std::string& why);
    bool _doMeasure  (const Args& a, JJson& out, std::string& why);
    bool _doEdges    (const Args& a, JJson& out, std::string& why);
    bool _doFrame    (const Args& a, JJson& out, std::string& why);
    bool _doRecord   (const Args& a, JJson& out, std::string& why);
    bool _doReplay   (const Args& a, JJson& out, std::string& why);

    // The plane carrying device channel `ch` in this frame, or -1.
    static int _planeOf(const JScopeFrame& f, uint8_t ch);
    // "5", "ch5", "CH5" -> 4 (device channel id). False on anything else.
    bool _parseChannel(const std::string& s, uint8_t& ch, std::string& why) const;
    bool _ok(bool done, std::string& why, const char* what);

    JScopeSession& m_session;
    JScopeActions& m_actions;
    JScopeBusHooks m_hooks;

    // The reason the last action was refused, as JScopeActions said it. Cleared
    // before every command, so a reply never carries an older refusal.
    std::string           m_refusal;
    std::function<void()> m_disconnectRefusal;
};

} // inline namespace jf
