// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JAppWindow.h>

#include <functional>
#include <vector>

inline namespace jf {

class JScopeApp;

// The keyboard shortcuts, as DATA rather than as a switch statement.
//
// They were a switch, plus a log line listing them, plus -- once there was a help
// system -- a third copy for the user to read. Three statements of the same fact
// drift apart, and the two that are prose drift first because nothing checks
// them. So there is one table: the dispatcher runs it and the help is generated
// from it, and a shortcut that is added or renamed cannot appear in one and not
// the other.
struct JScopeShortcut {
    JKeyEvent::JKey            key;
    const char*                name;          // as the user should press it
    const char*                description;   // as the help should say it
    std::function<void(JScopeApp&)> action;
};

class JScopeShortcuts {
public:
    static void install(JAppWindow& window, JScopeApp& app);

    // The one list. Help reads it; so does the dispatcher.
    static const std::vector<JScopeShortcut>& table();
};

} // inline namespace jf
