#include "JScopeWindowIdentity.h"

#include "scope/JScopeLog.h"

#include <j/app/JAppWindow.h>

#if defined(__linux__)
#include <xcb/xcb.h>
#include <cstring>
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

#else

void applyWindowIdentity(JAppWindow&) {}

#endif

} // inline namespace jf
