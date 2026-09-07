// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JAppWindow.h>

inline namespace jf {

class JScopeApp;

// Builds the acquisition toolbar: the run/stop/single controls that a scope
// user reaches for constantly and should not have to open a menu to find.
class JScopeToolBarBuilder {
public:
    static void build(JAppWindow& window, JScopeApp& app);
};

} // inline namespace jf
