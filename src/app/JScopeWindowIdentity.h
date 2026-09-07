// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

namespace jf { class JAppWindow; }

inline namespace jf {

// Give the window a WM_CLASS of its own.
//
// JFramework sets WM_CLASS to "genesis-ui"/"GenesisUi" for every application it
// builds, and offers no way to change it. That is what a desktop uses to decide
// which application a window belongs to, so a running JScope showed as
// "genesisui" in the dock and in alt-tab, and could not be told apart from any
// other JFramework application on the machine — jayecu's studio-jf is one, and
// is pinned to the same dock.
//
// The framework does expose the native handle, so the application can set the
// property itself afterwards. No framework change, no shared class, and the
// desktop entry can name JScope specifically without capturing another app's
// windows.
//
// X11 only, and silently does nothing anywhere else: this is a property of the
// X protocol, and under a native Wayland surface there is nothing to set.
void applyWindowIdentity(JAppWindow& window);

} // inline namespace jf
