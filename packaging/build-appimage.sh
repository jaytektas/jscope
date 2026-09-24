#!/usr/bin/env bash
# Package the Linux build as the release file: dist/jscope-<version>-x86_64.AppImage.
#
#   packaging/build-appimage.sh        (after: cmake --build build)
#
# The AppImage is the Linux release that UPDATES ITSELF: jscope's updater (JFramework's JAppUpdater)
# swaps the file named by $APPIMAGE, and picks the release asset ending in -x86_64.AppImage. So the name
# here is the name a release must carry. The .deb (build-deb.sh) still ships beside it for people who
# want a package manager to own the install; it does not update itself.
#
# What the .deb installs and an AppImage cannot — the udev rule — jscope carries and installs itself the
# first time the scope is refused (JUdevRule, through the system's own password prompt).
#
# libusb is bundled: it is the one library jscope needs that a desktop is not certain to have. Everything
# else it links (libc, libstdc++, xcb, Vulkan's loader) is part of any desktop that can show it at all.
#
# Needs appimagetool (github.com/AppImage/appimagetool) on PATH, and rsvg-convert for the icon.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/jscope"
DIST="$ROOT/dist"

fail() { echo "build-appimage: $*" >&2; exit 1; }

command -v appimagetool >/dev/null || fail "no appimagetool on PATH"
command -v rsvg-convert >/dev/null || fail "no rsvg-convert (apt install librsvg2-bin)"
[ -x "$BIN" ] || fail "not built: $BIN -- run cmake --build build first"

VERSION="$(sed -n 's/.*project(jscope VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || fail "could not read the version out of CMakeLists.txt"
OUT="$DIST/jscope-$VERSION-x86_64.AppImage"

LIBUSB="$(ldd "$BIN" | awk '/libusb-1\.0\.so/ {print $3}')"
[ -f "$LIBUSB" ] || fail "jscope does not link libusb-1.0 where ldd can see it"

APPDIR="$DIST/jscope.AppDir"
rm -rf "$APPDIR" "$OUT"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib"

install -m755 "$BIN" "$APPDIR/usr/bin/jscope"
strip "$APPDIR/usr/bin/jscope"
cp -L "$LIBUSB" "$APPDIR/usr/lib/libusb-1.0.so.0"

rsvg-convert -w 256 -h 256 -o "$APPDIR/jscope.png" "$ROOT/packaging/jscope.svg"
sed 's|^Exec=.*|Exec=jscope|' "$ROOT/packaging/jscope.desktop" > "$APPDIR/jscope.desktop"

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/jscope" "$@"
EOF
chmod 755 "$APPDIR/AppRun"

mkdir -p "$DIST"
ARCH=x86_64 appimagetool --no-appstream "$APPDIR" "$OUT" >/dev/null 2>&1 \
    || { ARCH=x86_64 appimagetool --no-appstream "$APPDIR" "$OUT"; fail "appimagetool failed"; }
rm -rf "$APPDIR"
echo "built: $OUT"
