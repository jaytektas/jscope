# Cross-compiling the 1008C application for Windows

    ./packaging/build-windows.sh

Builds `build-win/jscope.exe` for x86-64 Windows from Linux, using
`x86_64-w64-mingw32-g++`. One file, no DLLs to ship beside it.

## What the .exe needs on the target machine

    kernel32 / user32 / gdi32 / msvcrt    always present
    vulkan-1.dll                          installed by the graphics driver

The GCC runtime is linked in statically (`-static-libgcc -static-libstdc++
-static`). Without that, the binary imports `libgcc_s_seh-1.dll`,
`libstdc++-6.dll` and `libwinpthread-1.dll`, which exist only inside the cross
toolchain — the program then refuses to start on Windows with a dialog that
names no missing file. The scope gets carried to a vehicle on a laptop, so what
comes out of the build has to be one file that runs.

## The 1008C itself needs WinUSB

Linux has no driver bound to `0783:5725`, so libusb claims it directly and the
udev rule is enough. **Windows has no class driver for it either**, but it will
not let libusb near the device until one is bound: use Zadig to install
**WinUSB** on `0783:5725`.

That replaces Hantek's own driver, and their software stops seeing the scope
until it is put back through Device Manager. Nothing here can avoid that — it is
how a vendor-specific USB device is reached on Windows.

## Why the build is Release

The application's own translation units are small (the largest is 722 lines),
but the framework's headers are template-heavy enough that an unoptimised object
overflows what the COFF object format can hold, and the assembler stops with
`file too big`. The reflex answer is `-Wa,-mbig-obj`; CLAUDE.md rules it out,
and rightly — here it would be papering over the symptom, since the same
translation units are ordinary at `-O2`. Release is the honest fix and is what
would be shipped anyway.

## third_party/

Two libraries have no pkg-config on Windows and are vendored:

- `libusb-win/` — MinGW build of libusb-1.0. The include path is the **parent**
  of `libusb-1.0/`, because the sources include `<libusb-1.0/libusb.h>`.
- `vulkan-win/` — an import library generated from the Vulkan headers. See its
  README; the short version is that a hand-curated one had already gone stale
  once and broke the link with a single unexplained symbol.

## Status: the headless half is done, the GUI is blocked upstream

Everything that is not the window builds, links and **runs** as native Windows
code. All 15 test executables cross-compile and pass under wine — 403
assertions, covering the 1008C protocol and codec, the capture format, USB
enumeration, the frame queue and pool, measurements and session lifecycle.

`jscope.exe` itself also builds, links and runs — but only against a JFramework
whose Windows platform layer has been patched. Unpatched, seven translation
units fail, all of them ones that include `JAppWindow.h`, and **every error is
inside the framework's own headers**; none are in this repository.

`JWindowsPlatformWindow` is a 430-line partial implementation against
`JLinuxPlatformWindow`'s thousand-odd, and the menu runtime asks it for three
things it does not have — `rawWindowId()`, `focusedWindowRaw()`, `screenSize()`
— plus two places that convert an `HWND` to `uintptr_t` with `static_cast`,
which compiles on X11 (where a window id is an integer) and cannot compile on
Win32 (where it is a pointer).

The fix is about thirty lines and belongs in JFramework, not here: this app is
downstream of a shipped SDK and does not edit it. With those five spots fixed,
the full GUI comes up on Windows — menus, toolbar, docks, graticule, channel
legend, device enumeration through libusb — verified under wine.
