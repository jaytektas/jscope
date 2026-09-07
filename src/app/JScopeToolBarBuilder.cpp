// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JScopeToolBarBuilder.h"
#include "JScopeApp.h"
#include "JScopeActions.h"
#include "scope/JScopeLog.h"

inline namespace jf {

void JScopeToolBarBuilder::build(JAppWindow& window, JScopeApp& app) {
    JToolBar& bar = window.toolBar();

    // Same single path as the menu and the shortcuts.
    JScopeActions& a = app.actions();
    bar.addButton("Run",     [&a] { a.run(); });
    bar.addButton("Stop",    [&a] { a.stop(); });
    bar.addButton("Single",  [&a] { a.single(); });
    bar.addSeparator();
    bar.addButton("Force",   [&a] { a.forceTrigger(); });
    bar.addButton("Autoset", [&a] { a.autoset(); });

    JLOGC(JScopeLog::kUi, JLogLevel::Debug) << "toolbar built";
}

} // inline namespace jf
