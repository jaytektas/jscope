// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <vector>

inline namespace jf {

// The help text.
//
// WHAT IT DOES NOT SAY: that Run starts the sweep, or that Stop stops it. Help
// that restates the labels costs a reader their attention and gives nothing back,
// and it is the first thing to go stale because nobody re-reads it.
//
// What it says instead is the things this instrument does that surprise people,
// and that cost an afternoon when they are not written down: that the scope keeps
// no settings of its own, that it drops off the USB bus if the host stops talking
// to it, that a generator pattern is not sent until it is downloaded. Every one of
// those was a bug report before it was a paragraph.
//
// The shortcut list is GENERATED from JScopeShortcuts::table() rather than typed,
// because a list of keys is exactly the kind of prose that drifts silently.
class JScopeHelp {
public:
    struct JTopic {
        const char* title;
        std::string body;
    };

    static std::vector<JTopic> topics();

    // Built from the shortcut table, so it cannot disagree with what the keys do.
    static std::string shortcuts();
};

} // inline namespace jf
