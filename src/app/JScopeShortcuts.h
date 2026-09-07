// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JAppWindow.h>

inline namespace jf {

class JScopeApp;

// Keyboard shortcuts for the acquisition controls. A scope is driven one-handed
// while the other hand holds a probe, so these matter more here than in most
// applications.
class JScopeShortcuts {
public:
    static void install(JAppWindow& window, JScopeApp& app);
};

} // inline namespace jf
