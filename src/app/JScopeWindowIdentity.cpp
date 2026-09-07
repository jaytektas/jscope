// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JScopeWindowIdentity.h"

#include "scope/JScopeLog.h"

#include <j/app/JAppWindow.h>

#if defined(__linux__)
#include <xcb/xcb.h>
#include <cstring>
#elif defined(_WIN32)
#include <windows.h>
#endif

inline namespace jf {

#if defined(__linux__)

void applyWindowIdentity(JAppWindow& window) {
    const JNativeWindowHandle h = window.window().nativeHandle();
    if (!h.connectionPointer || !h.windowPointer) {
        JLOGC(JScopeLog::kUi, JLogLevel::Debug)
            << "no native X window; leaving WM_CLASS alone";
        return;
    }

    auto* connection = static_cast<xcb_connection_t*>(h.connectionPointer);
    const auto id = static_cast<xcb_window_t>(reinterpret_cast<uintptr_t>(h.windowPointer));

    // WM_CLASS is two NUL-terminated strings back to back: instance then class.
    // The trailing NUL is part of the value, so the length counts it.
    static constexpr char kClass[] = "jscope\0JScope";
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, id,
                        XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
                        sizeof kClass, kClass);
    xcb_flush(connection);

    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "WM_CLASS set to jscope/JScope";
}

#elif defined(_WIN32)

void applyWindowIdentity(JAppWindow& window) {
    const JNativeWindowHandle h = window.window().nativeHandle();
    if (!h.windowPointer) {
        JLOGC(JScopeLog::kUi, JLogLevel::Debug)
            << "no native window; leaving the icon alone";
        return;
    }

    auto* hwnd = static_cast<HWND>(h.windowPointer);
    HINSTANCE instance = GetModuleHandleW(nullptr);

    // Resource id 1, matching packaging/jscope.rc.in. Loaded at the two sizes
    // Windows actually asks for rather than at one size and left to be scaled:
    // the icon carries a 16px drawing made for 16px, and letting the window
    // manager shrink the 32px one instead throws that away.
    auto load = [&](int cx, int cy) {
        return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                             cx, cy, LR_DEFAULTCOLOR));
    };
    HICON large = load(GetSystemMetrics(SM_CXICON),   GetSystemMetrics(SM_CYICON));
    HICON small = load(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));

    if (!large && !small) {
        JLOGC(JScopeLog::kUi, JLogLevel::Warn)
            << "no icon resource in the executable; the window keeps the default";
        return;
    }

    // The CLASS icon, not just this window's, because JFramework registers one
    // window class for every window it makes -- so this reaches the floating
    // dock windows too, including ones torn out later, without having to catch
    // each one as it appears. The class is registered against this process's
    // own instance handle, so nothing outside this application is touched.
    if (large) SetClassLongPtrW(hwnd, GCLP_HICON,   reinterpret_cast<LONG_PTR>(large));
    if (small) SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(small));

    // The main window and its taskbar button already exist by now, and neither
    // re-reads the class icon on its own, so they are told directly.
    if (large) SendMessageW(hwnd, WM_SETICON, ICON_BIG,   reinterpret_cast<LPARAM>(large));
    if (small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));

    JLOGC(JScopeLog::kUi, JLogLevel::Info) << "window icon set from the executable resource";
}

#else

void applyWindowIdentity(JAppWindow&) {}

#endif

} // inline namespace jf
