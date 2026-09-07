// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JScopeShortcuts.h"

#include "JScopeActions.h"
#include "JScopeApp.h"
#include "scope/JReferenceSignal.h"
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
        // A key rather than only a menu, because the moment you want a reference
        // is the moment both hands are on probes. Cycles rather than toggling:
        // there are eight, and reaching for a menu to step between them defeats
        // the point of having a key at all.
        { JKeyEvent::JKey::R,     "R",     "Cycle the reference trace",
          [](JScopeApp& a) {
              constexpr size_t n = sizeof(kReferenceSignals) / sizeof(kReferenceSignals[0]);
              const JReferenceSignal current = a.traceView().reference();
              size_t i = 0;
              while (i < n && kReferenceSignals[i] != current) ++i;
              a.traceView().setReference(kReferenceSignals[(i + 1) % n]);
          } },
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
