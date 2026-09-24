#!/usr/bin/env bash
# Build the Windows installer from the cross-compiled jscope.exe: dist/jscope-<version>-setup.exe.
#
# The build script produces one self-contained executable; this produces the
# thing you can actually hand to someone. The difference is not the binary --
# it is everything a stranger needs in order to run it and everything the
# licence requires be shipped alongside it:
#
#   jscope.exe            the application, GCC runtime linked in statically
#   README.txt            the WinUSB step, which is the one thing that WILL
#                         stop a first-time user, and how to undo it
#   LICENSE.txt           GPLv3, the licence of the work as a whole
#   SOURCE.txt            where the corresponding source is, pinned to the exact
#                         commit this binary was built from -- GPLv3 s6 is not
#                         satisfied by "it's on GitHub somewhere"
#   licences/             libusb (LGPL-2.1, linked in statically, so s6(a)
#                         applies whether or not anyone ever asks) and libwdi
#                         (LGPL-3.0, the driver step)
#   driver/wdi-simple.exe binds WinUSB to the scope (third_party/libwdi-win)
#
# AN INSTALLER, because jscope updates itself: the update downloads the next
# jscope-<version>-setup.exe and runs it silently over this copy (packaging/
# jscope.iss says how that stays smooth). And because the one hard step for a
# first-time user -- binding WinUSB -- is a checkbox in it rather than a page of
# Zadig instructions.
#
# Needs Inno Setup's compiler: ISCC (wine is fine -- set ISCC to the command, e.g.
# ISCC="wine C:/InnoSetup/ISCC.exe"; the default looks for exactly that).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-win"
DIST="$ROOT/dist"

fail() { echo "package-windows: $*" >&2; exit 1; }

ISCC="${ISCC:-wine C:/InnoSetup/ISCC.exe}"
export WINEDEBUG="${WINEDEBUG:--all}"   # wine narrates its own start-up otherwise
command -v ${ISCC%% *} >/dev/null || fail "no Inno Setup compiler: set ISCC (see the top of this file)"
[ -f "$ROOT/third_party/libwdi-win/wdi-simple.exe" ] || fail "third_party/libwdi-win is missing"

# Build first. Cheap when it is already current, and it removes the failure
# mode where a package is cut from a stale exe that nobody thought to rebuild.
"$ROOT/packaging/build-windows.sh"

[ -f "$BUILD/jscope.exe" ] || fail "no jscope.exe after the build"

VERSION="$(sed -n 's/.*project(jscope VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || fail "could not read the version out of CMakeLists.txt"

# The commit is what makes SOURCE.txt a real offer rather than a gesture, so a
# dirty tree is marked as such -- a package built from uncommitted work cannot
# honestly point at a commit.
COMMIT="$(git -C "$ROOT" rev-parse HEAD)"
git -C "$ROOT" diff-index --quiet HEAD -- || COMMIT="$COMMIT (plus uncommitted changes)"

NAME="jscope-$VERSION-win64"
STAGE="$DIST/$NAME"
rm -rf "$STAGE" "$DIST/jscope-$VERSION-setup.exe"
mkdir -p "$STAGE/licences" "$STAGE/driver"

cp "$BUILD/jscope.exe" "$STAGE/"
x86_64-w64-mingw32-strip "$STAGE/jscope.exe"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"

# libusb ships its own COPYING inside the source tarball that is already
# vendored for the build, so the licence text comes from the same place the
# code does rather than from a copy that could drift away from it.
tar xjf "$ROOT/third_party/libusb-win/libusb-1.0.29.tar.bz2" \
    -O libusb-1.0.29/COPYING > "$STAGE/licences/libusb-1.0.29-COPYING.txt"
cp "$ROOT/third_party/libwdi-win/COPYING-LGPL.txt" "$STAGE/licences/libwdi-1.5.1-COPYING.txt"
cp "$ROOT/third_party/libwdi-win/wdi-simple.exe" "$STAGE/driver/"

cat > "$STAGE/SOURCE.txt" <<EOF
jscope $VERSION -- corresponding source
=======================================

This program is free software under the GNU General Public License version 3.
You are entitled to the complete source code for it, and it is here:

    https://github.com/jaytektas/jscope

This particular binary was built from commit

    $COMMIT

which you can check out directly:

    git clone https://github.com/jaytektas/jscope
    cd jscope
    git checkout $COMMIT

It depends on the JFramework toolkit, which is also GPLv3 and also published:

    https://github.com/jaytektas/JFramework

Everything needed to reproduce this exact package is in the repository --
packaging/build-windows.sh cross-compiles it from Linux with mingw-w64, and
packaging/package-windows.sh builds the installer this came from.

libusb 1.0.29 is linked into jscope.exe statically and is licensed LGPL-2.1
(see licences/). Its unmodified source tarball is in the repository under
third_party/libusb-win/, and the build links against it through a documented,
scripted step, so the program can be relinked against a different build of
libusb by anyone who wants to.

driver/wdi-simple.exe is libwdi 1.5.1 (LGPL-3.0, see licences/), built from the
source tarball in third_party/libwdi-win/ by the steps in the README there.
EOF

cat > "$STAGE/README.txt" <<'EOF'
jscope for Windows (x86-64)
===========================

The executable is self-contained: the GCC runtime and libusb are linked in. It
needs only what Windows and your graphics driver already provide -- kernel32,
user32, gdi32, msvcrt, and vulkan-1.dll, which the Vulkan loader installs with
the driver.

jscope keeps itself up to date: it checks for a new release when it starts, and
Help -> Check for Updates asks at any time. An update downloads, is checked
against its published checksum, and installs over this copy.


The USB driver
--------------

The Hantek 1008C is a vendor-specific USB device. Windows has no class driver
for it, and jscope talks to it through libusb, which on Windows can only open a
device that has WinUSB bound to it. Out of the box the scope is bound to
Hantek's own driver instead, and jscope will not see it at all.

The installer's "Install the USB driver" task binds WinUSB to the scope (USB ID
0783:5725) -- Windows asks for an administrator's permission for that one step.
Unplug the scope and plug it back in afterwards. To run the step again later,
use "Install the jscope USB driver" in the Start menu.

READ THIS BEFORE YOU DO IT: binding WinUSB means HANTEK'S OWN SOFTWARE WILL NO
LONGER SEE THE SCOPE. Only one driver can be bound at a time. It is completely
reversible -- Device Manager -> the device -> Update Driver -> pick the Hantek
driver again, or just uninstall the device and replug it -- but if you rely on
the OEM application, know that you are choosing between them rather than adding
to what you have.

If the automatic step does not work, Zadig (https://zadig.akeo.ie) does the same
thing by hand: run it as Administrator, Options -> List All Devices, select the
device with USB ID 0783 5725 (it is probably called "YDJ-2088" -- CHECK THE USB
ID, NOT THE NAME: Zadig replaces the driver on whatever is selected, keyboard
included), choose WinUSB, and click Replace Driver.


Graphics
--------

jscope renders with Vulkan. Any GPU driver from roughly 2017 onwards has it.
If the window fails to open, updating the graphics driver is the first thing to
try; integrated Intel and AMD graphics are fine, and a discrete card is not
needed for a scope display.


Where your settings go
----------------------

Window layout, channel setup and the instrument you used last are remembered
per-user under your AppData folder. Uninstalling jscope leaves them behind; they
are harmless, and they are what makes the application come back the way you left
it.


Help
----

Help -> Contents inside the application covers the instrument itself, sweeps and
triggering, probes and clamps, the pulse generator, and what to check when a
reading looks wrong. Keyboard shortcuts are listed there too.

Source, licence and issues: https://github.com/jaytektas/jscope
EOF

# ISCC is a Windows program: hand it Windows paths. winepath does that when it
# runs under wine; a native ISCC (on Windows itself) takes them as they are.
winpath() { if command -v winepath >/dev/null; then winepath -w "$1" 2>/dev/null; else echo "$1"; fi; }
$ISCC /Q "/DAppVersion=$VERSION" "/DStageDir=$(winpath "$STAGE")" "/DOutputDir=$(winpath "$DIST")" \
      "$(winpath "$ROOT/packaging/jscope.iss")" || fail "the installer did not build"
[ -f "$DIST/jscope-$VERSION-setup.exe" ] || fail "no jscope-$VERSION-setup.exe after the build"
rm -rf "$STAGE"

echo
echo "packaged: $DIST/jscope-$VERSION-setup.exe"
