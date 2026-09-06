#!/usr/bin/env bash
# Cross-compile the 1008C application for Windows (x86-64) from Linux.
#
# The result is ONE self-contained jscope.exe. It imports only DLLs that are
# already on a Windows machine -- kernel32, user32, gdi32, msvcrt, and
# vulkan-1.dll from the graphics driver -- because the GCC runtime is linked in
# statically. Nothing has to be copied beside it.
#
# The one thing Windows still needs that Linux does not: the 1008C is a
# vendor-specific device with no Windows class driver, so WinUSB must be bound
# to 0783:5725 with Zadig before libusb can open it. That replaces Hantek's own
# driver, so their software will not see the scope until it is put back.
#
#   JFRAMEWORK_WIN_SDK=<path>  — the Windows JFramework SDK (built with the same toolchain)
#   JFRAMEWORK_SRC=<path>      — the JFramework source tree, for the toolchain file and headers
#
# Prerequisites, verified before anything is configured, because each one fails
# much later and much less clearly than it does here.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-win"
SDK="${JFRAMEWORK_WIN_SDK:-$HOME/jframework-sdk-win-new}"
JF_SRC="${JFRAMEWORK_SRC:-$HOME/workspace/JFramework}"
TOOLCHAIN="$JF_SRC/cmake/mingw-w64.cmake"

fail() { echo "build-windows: $*" >&2; exit 1; }

command -v x86_64-w64-mingw32-g++ >/dev/null || fail "no mingw-w64 toolchain (apt install g++-mingw-w64-x86-64)"
[ -f "$TOOLCHAIN" ]                          || fail "no toolchain file at $TOOLCHAIN (set JFRAMEWORK_SRC)"
[ -d "$SDK/lib/cmake/JFramework" ]           || fail "no Windows JFramework SDK at $SDK (set JFRAMEWORK_WIN_SDK)"
[ -f "$ROOT/third_party/libusb-win/lib/libusb-1.0.a" ] || fail "third_party/libusb-win is missing"
[ -f "$ROOT/third_party/vulkan-win/lib/libvulkan-1.a" ] || fail "third_party/vulkan-win is missing"

# Release, not because speed matters here, but because the framework's headers
# are template-heavy enough that an unoptimised object exceeds what the COFF
# object format can hold -- an -O0 cross build dies with "file too big" in the
# assembler. The application's own translation units are all small.
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DJFramework_DIR="$SDK/lib/cmake/JFramework" \
    -DVulkan_LIBRARY="$ROOT/third_party/vulkan-win/lib/libvulkan-1.a" \
    -DVulkan_INCLUDE_DIR="$JF_SRC/include" \
    -DLIBUSB_WIN_INCLUDE_DIR="$ROOT/third_party/libusb-win/include" \
    -DLIBUSB_WIN_LIBRARY="$ROOT/third_party/libusb-win/lib/libusb-1.0.a"

cmake --build "$BUILD"

echo
echo "built: $BUILD/jscope.exe"
x86_64-w64-mingw32-objdump -p "$BUILD/jscope.exe" | sed -n 's/^\tDLL Name: /  needs /p'
