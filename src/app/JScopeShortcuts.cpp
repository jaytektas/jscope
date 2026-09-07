// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JScopeShortcuts.h"

#include "JScopeActions.h"
#include "JScopeApp.h"
#include "scope/JScopeLog.h"
#include "ui/JTraceView.h"

inline namespace jf {

const std::vector<JScopeShortcut>& JScopeShortcuts::table() {
    static const std::vector<JScopeShortcut> t = {
        { JKeyEvent::JKey::Space, "Space", "Run or stop the sweep",
          [](JScopeApp& a) { a.actions().toggleRunStop(); } },
        { JKeyEvent::JKey::S,     "S",     "Single shot: capture one frame and stop",
          [](JScopeApp& a) { a.actions().single(); } },
        { JKeyEvent::JKey::F,     "F",     "Force a trigger without waiting for an edge",
          [](JScopeApp& a) { a.actions().forceTrigger(); } },
        { JKeyEvent::JKey::A,     "A",     "Autoset: let the instrument choose a scale",
          [](JScopeApp& a) { a.actions().autoset(); } },
        { JKeyEvent::JKey::Z,     "Z",     "Reset the zoom to the whole record",
          [](JScopeApp& a) { a.traceView().resetViewWindow(); } },
    };
    return t;
}

void JScopeShortcuts::install(JAppWindow& window, JScopeApp& app) {
    window.onKey = [&window, &app](const JKeyEvent& ke) {
        if (!ke.pressed) return;
        for (const JScopeShortcut& s : table())
            if (s.key == ke.key) { s.action(app); break; }
        window.requestRedraw();
    };

    JLOGC(JScopeLog::kUi, JLogLevel::Debug)
        << "shortcuts installed: " << table().size() << " keys";
}

} // inline namespace jf
