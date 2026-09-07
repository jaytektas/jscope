#!/usr/bin/env bash
# Assemble a distributable Windows package from the cross-compiled jscope.exe.
#
# The build script produces one self-contained executable; this produces the
# thing you can actually hand to someone. The difference is not the binary --
# it is everything a stranger needs in order to run it and everything the
# licence requires be shipped alongside it:
#
#   jscope.exe            the application, GCC runtime linked in statically
#   INSTALL.txt           the WinUSB step, which is the one thing that WILL
#                         stop a first-time user, and how to undo it
#   LICENSE.txt           GPLv3, the licence of the work as a whole
#   SOURCE.txt            where the corresponding source is, pinned to the exact
#                         commit this binary was built from -- GPLv3 s6 is not
#                         satisfied by "it's on GitHub somewhere"
#   licences/libusb.txt   LGPL-2.1, because libusb is linked in statically and
#                         s6(a) applies whether or not anyone ever asks
#
# Deliberately NOT an installer. There is nothing to install: no registry keys,
# no DLLs to place, no uninstaller to get wrong. Unzip it and run it.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-win"
DIST="$ROOT/dist"

fail() { echo "package-windows: $*" >&2; exit 1; }

command -v zip >/dev/null || fail "no zip (apt install zip)"

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

rm -rf "$STAGE" "$DIST/$NAME.zip"
mkdir -p "$STAGE/licences"

cp "$BUILD/jscope.exe" "$STAGE/"
x86_64-w64-mingw32-strip "$STAGE/jscope.exe"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"

# libusb ships its own COPYING inside the source tarball that is already
# vendored for the build, so the licence text comes from the same place the
# code does rather than from a copy that could drift away from it.
tar xjf "$ROOT/third_party/libusb-win/libusb-1.0.29.tar.bz2" \
    -O libusb-1.0.29/COPYING > "$STAGE/licences/libusb-1.0.29-COPYING.txt"

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
packaging/package-windows.sh assembles the zip you are reading this from.

libusb 1.0.29 is linked into jscope.exe statically and is licensed LGPL-2.1
(see licences/). Its unmodified source tarball is in the repository under
third_party/libusb-win/, and the build links against it through a documented,
scripted step, so the program can be relinked against a different build of
libusb by anyone who wants to.
EOF

cat > "$STAGE/INSTALL.txt" <<'EOF'
jscope for Windows (x86-64)
===========================

There is nothing to install. Unzip it somewhere and run jscope.exe.

The executable is self-contained: the GCC runtime and libusb are linked in, so
no DLLs need to be copied beside it. It needs only what Windows and your
graphics driver already provide -- kernel32, user32, gdi32, msvcrt, and
vulkan-1.dll, which the Vulkan loader installs with the driver.


The one thing you MUST do first
-------------------------------

The Hantek 1008C is a vendor-specific USB device. Windows has no class driver
for it, and jscope talks to it through libusb, which on Windows can only open a
device that has WinUSB bound to it. Out of the box the scope is bound to
Hantek's own driver instead, and jscope will not see it at all.

To fix that, once, with the scope plugged in:

  1. Download Zadig from https://zadig.akeo.ie -- it is a single executable and
     needs no installation either.
  2. Run it as Administrator.
  3. Options -> List All Devices.
  4. In the dropdown, select the device with USB ID  0783 5725.
     It will probably be called "YDJ-2088" rather than anything with Hantek in
     the name. That is normal -- the device identifies itself as a generic OEM
     part, which is also why jscope matches it by USB ID and never by name.

     CHECK THE USB ID, NOT THE NAME, AND CHECK IT TWICE. "List All Devices"
     shows every USB device on the machine, including your keyboard and mouse,
     and Zadig will replace the driver on whichever one is selected. Doing that
     to an input device leaves you with no way to undo it except another
     keyboard. The ID field must read 0783 5725 before you click anything.
  5. Choose WinUSB as the driver on the right, and click Replace Driver.
  6. Unplug the scope and plug it back in.

jscope will find it from then on.

READ THIS BEFORE YOU DO IT: replacing the driver means HANTEK'S OWN SOFTWARE
WILL NO LONGER SEE THE SCOPE. Only one driver can be bound at a time. It is
completely reversible -- Device Manager -> the device -> Update Driver -> pick
the Hantek driver again, or just uninstall the device and replug it -- but if
you rely on the OEM application, know that you are choosing between them rather
than adding to what you have.


Graphics
--------

jscope renders with Vulkan. Any GPU driver from roughly 2017 onwards has it.
If the window fails to open, updating the graphics driver is the first thing to
try; integrated Intel and AMD graphics are fine, and a discrete card is not
needed for a scope display.


Where your settings go
----------------------

Window layout, channel setup and the instrument you used last are remembered
per-user under your AppData folder. Deleting jscope.exe leaves them behind; they
are harmless, and they are what makes the application come back the way you left
it.


Help
----

Help -> Contents inside the application covers the instrument itself, sweeps and
triggering, probes and clamps, the pulse generator, and what to check when a
reading looks wrong. Keyboard shortcuts are listed there too.

Source, licence and issues: https://github.com/jaytektas/jscope
EOF

( cd "$DIST" && zip -qr "$NAME.zip" "$NAME" )

echo
echo "packaged: $DIST/$NAME.zip"
( cd "$DIST" && unzip -l "$NAME.zip" | sed 's/^/  /' )
