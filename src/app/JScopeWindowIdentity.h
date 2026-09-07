// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

namespace jf { class JAppWindow; }

inline namespace jf {

// Give the window an identity of its own: a WM_CLASS on X11, an icon on
// Windows. Both are things the desktop reads to decide what this window is, and
// in both cases JFramework leaves the field at a default shared by every
// application built on it.
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
// On Windows the framework registers its window class with a null hIcon, so a
// running window shows the generic default even once the executable carries an
// icon resource. The icon is set on the CLASS, which reaches the floating dock
// windows as well, since they share it.
//
// Silently does nothing anywhere else -- under a native Wayland surface there
// is no WM_CLASS to set and no icon to attach.
void applyWindowIdentity(JAppWindow& window);

} // inline namespace jf
