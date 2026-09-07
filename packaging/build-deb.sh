#!/usr/bin/env bash
# Build jscope_<version>_amd64.deb from an existing Linux build.
#
# A .deb rather than a tarball because of the ONE thing Linux can do that the
# Windows package cannot: install the udev rule. The 1008C binds to no kernel
# driver on Linux, so unlike Windows there is nothing to replace -- the only
# obstacle is permission on the device node, and a package can simply grant it.
# Where a Windows user has to fetch Zadig and hand-pick their scope out of a
# list of every USB device they own, a Linux user installs this and plugs in.
#
# The framework is linked in statically, so the dependencies below are the whole
# of it: stock libraries every desktop already has.
#
# NOT relocatable and not built here from source -- it packages whatever is in
# build/, which is the binary that was actually tested.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/jscope"
DIST="$ROOT/dist"

fail() { echo "build-deb: $*" >&2; exit 1; }

command -v dpkg-deb >/dev/null || fail "no dpkg-deb"
command -v rsvg-convert >/dev/null || fail "no rsvg-convert (apt install librsvg2-bin)"
[ -x "$BIN" ] || fail "not built: $BIN -- run cmake --build build first"

VERSION="$(sed -n 's/.*project(jscope VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || fail "could not read the version out of CMakeLists.txt"

PKG="$DIST/jscope_${VERSION}_amd64"
rm -rf "$PKG" "$PKG.deb"
mkdir -p "$PKG/DEBIAN" \
         "$PKG/usr/bin" \
         "$PKG/usr/share/applications" \
         "$PKG/usr/share/icons/hicolor/scalable/apps" \
         "$PKG/usr/lib/udev/rules.d" \
         "$PKG/usr/share/doc/jscope"

install -Dm755 "$BIN" "$PKG/usr/bin/jscope"
strip "$PKG/usr/bin/jscope"

# The debug launcher goes in under its own name rather than as the thing the
# desktop entry runs. It is genuinely useful -- a crash from a desktop icon
# otherwise leaves nothing at all to work from -- but running every session
# under gdb is not what someone installing a package expects, and it would make
# gdb a dependency of starting the program at all.
sed 's|^BIN=.*|BIN="${JSCOPE_BIN:-/usr/bin/jscope}"|' \
    "$ROOT/packaging/jscope-launch.sh" > "$PKG/usr/bin/jscope-debug"
chmod 755 "$PKG/usr/bin/jscope-debug"

install -Dm644 "$ROOT/packaging/jscope.svg" \
               "$PKG/usr/share/icons/hicolor/scalable/apps/jscope.svg"

# Rasterised sizes beside the SVG: a panel or dock that reads only PNG themes
# falls back to a generic icon otherwise. Rendered at each size for the same
# reason the .ico is -- a 256px drawing scaled to 16px loses the graticule.
for s in 16 24 32 48 64 128 256; do
    mkdir -p "$PKG/usr/share/icons/hicolor/${s}x${s}/apps"
    rsvg-convert -w "$s" -h "$s" \
        -o "$PKG/usr/share/icons/hicolor/${s}x${s}/apps/jscope.png" \
        "$ROOT/packaging/jscope.svg"
done

# The committed desktop entry runs the launcher out of a source tree. An
# installed package runs the installed binary, so Exec is rewritten and the
# repo-relative form does not escape into /usr.
sed 's|^Exec=.*|Exec=jscope|' "$ROOT/packaging/jscope.desktop" \
    > "$PKG/usr/share/applications/jscope.desktop"

cat > "$PKG/usr/lib/udev/rules.d/60-hantek-1008c.rules" <<'EOF'
# Hantek 1008C -- 8-channel automotive oscilloscope.
#
# The device binds to no kernel driver, so an application can claim it through
# libusb directly. All it needs is permission on the node, which defaults to
# root-only.
#
# uaccess hands it to whoever is logged in at the seat, which is the modern
# answer and is why this is not MODE="0666" -- there is no reason for every
# account on the machine to have raw USB access to it. The plugdev group is
# kept as a fallback for setups without systemd-logind.
#
# The device reports itself as "YDJ-2088" by "C3PO" with no mention of Hantek,
# so matching is by ID and never by name.
SUBSYSTEM=="usb", ATTR{idVendor}=="0783", ATTR{idProduct}=="5725", \
    MODE="0660", GROUP="plugdev", TAG+="uaccess"
EOF

INSTALLED_KB=$(du -sk "$PKG" | cut -f1)

cat > "$PKG/DEBIAN/control" <<EOF
Package: jscope
Version: $VERSION
Section: electronics
Priority: optional
Architecture: amd64
Maintainer: Jason Roughley <pis.controller@gmail.com>
Installed-Size: $INSTALLED_KB
Depends: libc6 (>= 2.38), libstdc++6 (>= 13.1), libgcc-s1 (>= 3.0), libxcb1, libxcb-keysyms1, libxcb-sync1, libusb-1.0-0, libvulkan1
Recommends: mesa-vulkan-drivers | vulkan-driver
Suggests: gdb
Homepage: https://github.com/jaytektas/jscope
Description: Oscilloscope front end for the Hantek 1008C
 A GUI oscilloscope for the Hantek 1008C, the eight-channel automotive scope
 that ships as a vendor-specific USB device with no documentation and no
 kernel driver.
 .
 Eight channels with per-channel probe attenuation and coupling, roll and burst
 acquisition, Auto/Normal/Single sweeps, measurements and cursors, and capture
 and replay to a file so a session can be reviewed with no hardware attached.
 .
 It also drives the instrument's pattern generator -- eight digital outputs
 running a pattern of up to 1440 pulses at a speed given in RPM, which is a
 crank and cam simulator -- as an editor, with the vendor's own .squ files
 readable and writable.
 .
 Renders with Vulkan. Installing this package also installs a udev rule
 granting the logged-in user access to the device.
EOF

cat > "$PKG/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    # The rule is only consulted when a device appears, so without a reload and
    # trigger a scope that is ALREADY PLUGGED IN keeps its old permissions and
    # the application still cannot open it -- which looks like the package
    # having done nothing.
    if command -v udevadm >/dev/null 2>&1; then
        udevadm control --reload-rules || true
        udevadm trigger --subsystem-match=usb --attr-match=idVendor=0783 || true
    fi
    command -v update-desktop-database >/dev/null 2>&1 && \
        update-desktop-database -q /usr/share/applications || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && \
        gtk-update-icon-cache -q -f /usr/share/icons/hicolor || true
fi
exit 0
EOF
chmod 755 "$PKG/DEBIAN/postinst"

cat > "$PKG/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    command -v udevadm >/dev/null 2>&1 && udevadm control --reload-rules || true
    command -v update-desktop-database >/dev/null 2>&1 && \
        update-desktop-database -q /usr/share/applications || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && \
        gtk-update-icon-cache -q -f /usr/share/icons/hicolor || true
fi
exit 0
EOF
chmod 755 "$PKG/DEBIAN/postrm"

COMMIT="$(git -C "$ROOT" rev-parse HEAD)"
git -C "$ROOT" diff-index --quiet HEAD -- || COMMIT="$COMMIT (plus uncommitted changes)"

cat > "$PKG/usr/share/doc/jscope/copyright" <<EOF
Upstream-Name: jscope
Source: https://github.com/jaytektas/jscope

Files: *
Copyright: 2026 Jason Roughley <pis.controller@gmail.com>
License: GPL-3.0-or-later
 This program is free software: you can redistribute it and/or modify it under
 the terms of the GNU General Public License as published by the Free Software
 Foundation, either version 3 of the License, or (at your option) any later
 version.
 .
 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 .
 On Debian systems the full text of the GNU General Public License version 3
 can be found in /usr/share/common-licenses/GPL-3.

This binary was built from commit
 $COMMIT

Unlike the Windows build, libusb is NOT linked in here -- it is depended on as
the system's own shared library (libusb-1.0-0), so no LGPL relinking provision
is engaged and the copy you have is the one your distribution maintains.
EOF

gzip -9n < "$ROOT/README.md" > "$PKG/usr/share/doc/jscope/README.md.gz"

# Mandatory in a .deb, and read by "apt changelog". Generated from the git tags
# rather than kept by hand, so it cannot drift from what was actually released.
{
    git -C "$ROOT" for-each-ref --sort=-creatordate --format \
        '%(refname:short)|%(creatordate:rfc2822)|%(contents:subject)' refs/tags \
    | while IFS='|' read -r tag date subject; do
        printf 'jscope (%s) unstable; urgency=medium\n\n  * %s\n\n -- Jason Roughley <pis.controller@gmail.com>  %s\n\n' \
               "${tag#v}" "${subject:-Release $tag}" "$date"
      done
} | gzip -9n > "$PKG/usr/share/doc/jscope/changelog.gz"

# A man page, because an installed /usr/bin/jscope with no "man jscope" is a
# gap a package is expected to fill. Written here rather than in the repo so
# the version and the option list come from the same place the program does.
mkdir -p "$PKG/usr/share/man/man1"
cat > "$PKG/usr/share/man/man1/jscope.1" <<MAN
.TH JSCOPE 1 "$(date -u +%Y-%m-%d)" "jscope $VERSION" "User Commands"
.SH NAME
jscope \- oscilloscope front end for the Hantek 1008C
.SH SYNOPSIS
.B jscope
.RI [ options ]
.SH DESCRIPTION
.B jscope
drives the Hantek 1008C, an eight-channel automotive oscilloscope that connects
as a vendor-specific USB device. It provides the channel, timebase and trigger
controls the instrument itself has no front panel for, along with measurements,
cursors, capture and replay, and an editor for the instrument's digital pattern
generator.
.PP
The device binds to no kernel driver. Access is granted by the udev rule this
package installs, which hands the device to the user logged in at the seat; no
further setup is needed.
.SH OPTIONS
.TP
.B \-v, \-\-verbose
Log at debug level.
.TP
.B \-q, \-\-quiet
Log warnings and errors only.
.TP
.BI \-\-trace " CATEGORY"
Log one category at trace level. Categories include
.BR usb ", " scope ", " scope.hantek1008 " and " ui .
.SH FILES
.TP
.I ~/.local/share/jscope/logs
Session logs, written when started through
.BR jscope-debug (1).
.TP
.I /usr/lib/udev/rules.d/60-hantek-1008c.rules
Grants the logged-in user access to USB device 0783:5725.
.SH NOTES
The instrument reports itself as "YDJ-2088" by "C3PO" and does not mention
Hantek anywhere. This is the manufacturer's own generic identity, which is why
the device is matched by USB vendor and product ID and never by name.
.PP
Rendering requires Vulkan.
.SH SEE ALSO
Full documentation, including the reverse-engineered wire protocol, at
.UR https://github.com/jaytektas/jscope
.UE
.SH AUTHOR
Jason Roughley <pis.controller@gmail.com>
MAN
cat > "$PKG/usr/share/man/man1/jscope-debug.1" <<MAN
.TH JSCOPE\-DEBUG 1 "$(date -u +%Y-%m-%d)" "jscope $VERSION" "User Commands"
.SH NAME
jscope\-debug \- run jscope under a debugger and keep a log
.SH SYNOPSIS
.B jscope-debug
.RI [ options ]
.SH DESCRIPTION
Runs
.BR jscope (1)
under
.BR gdb (1),
which sits above the process doing nothing until something goes wrong. On an
abnormal exit it writes every thread's backtrace to the session log and renames
it so it stands out. Started from a desktop icon there is no terminal for a
crash to print to, so without this a crash leaves nothing to work from.
.PP
The application's own log output goes into the same file ahead of the trace,
line buffered so the last lines before a fault actually reach disk. Being asked
to quit is not treated as a crash.
.PP
If gdb is not installed the program is simply launched directly.
.SH ENVIRONMENT
.TP
.B JSCOPE_BIN
Binary to run. Default
.IR /usr/bin/jscope .
.TP
.B JSCOPE_LOG_DIR
Where logs are kept. Default
.IR ~/.local/share/jscope/logs .
Ten sessions are kept; crash logs are never pruned.
.TP
.B JSCOPE_NO_GDB
Set to any value to launch without the debugger.
.SH SEE ALSO
.BR jscope (1)
.SH AUTHOR
Jason Roughley <pis.controller@gmail.com>
MAN
gzip -9n "$PKG/usr/share/man/man1/jscope.1" "$PKG/usr/share/man/man1/jscope-debug.1"

# AppStream metadata, so the program appears in a software centre with its own
# description and icon instead of as a bare package name. The modalias provide
# is what lets one offer to install it when the scope is plugged in.
mkdir -p "$PKG/usr/share/metainfo"
cat > "$PKG/usr/share/metainfo/io.github.jaytektas.jscope.metainfo.xml" <<META
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>io.github.jaytektas.jscope</id>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>GPL-3.0-or-later</project_license>
  <name>JScope</name>
  <summary>Oscilloscope front end for the Hantek 1008C</summary>
  <description>
    <p>
      A GUI oscilloscope for the Hantek 1008C, the eight-channel automotive
      scope that connects as a vendor-specific USB device with no documentation
      and no kernel driver.
    </p>
    <p>
      Eight channels with per-channel probe attenuation and coupling, roll and
      burst acquisition, Auto, Normal and Single sweeps, measurements and
      cursors, and capture and replay to a file so a session can be reviewed
      with no hardware attached. It also drives the instrument's pattern
      generator, eight digital outputs running a crank and cam pattern at a
      speed given in RPM, as an editor.
    </p>
  </description>
  <launchable type="desktop-id">jscope.desktop</launchable>
  <url type="homepage">https://github.com/jaytektas/jscope</url>
  <url type="bugtracker">https://github.com/jaytektas/jscope/issues</url>
  <developer id="io.github.jaytektas">
    <name>Jason Roughley</name>
  </developer>
  <provides>
    <modalias>usb:v0783p5725d*</modalias>
  </provides>
  <categories>
    <category>Development</category>
    <category>Electronics</category>
  </categories>
  <content_rating type="oars-1.1"/>
  <releases>
    <release version="$VERSION" date="$(date -u +%Y-%m-%d)"/>
  </releases>
</component>
META

# Modes are inherited from whatever umask built the tree -- 0775 and 0664 here
# -- and a package is expected to ship 0755 and 0644. Normalised wholesale and
# then the executables put back, which is shorter than getting every redirect
# and heredoc along the way to agree.
find "$PKG" -type d -exec chmod 755 {} +
find "$PKG" -type f -exec chmod 644 {} +
chmod 755 "$PKG/usr/bin/jscope" "$PKG/usr/bin/jscope-debug" \
          "$PKG/DEBIAN/postinst" "$PKG/DEBIAN/postrm"

# Ownership matters in a .deb and the build directory is owned by whoever built
# it, so root is asserted at pack time rather than left to chance.
fakeroot dpkg-deb --build --root-owner-group "$PKG" >/dev/null 2>&1 \
    || dpkg-deb --build --root-owner-group "$PKG" >/dev/null

rm -rf "$PKG"

echo "packaged: $PKG.deb"
dpkg-deb --info "$PKG.deb" | sed -n '1,12p' | sed 's/^/  /'
