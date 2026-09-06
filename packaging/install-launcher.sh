#!/usr/bin/env bash
# Install the desktop entry and icon for the current user. No root: everything
# goes under ~/.local/share, which is what XDG says a user-built application
# should do.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
bin="$root/build/jscope"
apps="$HOME/.local/share/applications"
icons="$HOME/.local/share/icons/hicolor"

[ -x "$bin" ] || { echo "not built yet: $bin" >&2; exit 1; }

install -Dm644 "$here/jscope.svg" "$icons/scalable/apps/jscope.svg"

# Rasterised sizes as well as the SVG. The scalable icon is enough for anything
# using librsvg, but panels and docks that read only PNG themes would otherwise
# fall back to a generic icon.
#
# Rendered through GdkPixbuf, NOT ImageMagick: `convert` has an rsvg delegate
# configured but the binary is not installed on this machine, so it silently
# falls back to its own SVG parser, drops fill="none" and turns the trace into a
# black blob.
if python3 -c "import gi" 2>/dev/null; then
    for s in 16 24 32 48 64 128 256; do
        mkdir -p "$icons/${s}x${s}/apps"
        python3 -c "
import gi, sys
gi.require_version('GdkPixbuf', '2.0')
from gi.repository import GdkPixbuf
GdkPixbuf.Pixbuf.new_from_file_at_size(sys.argv[1], int(sys.argv[3]), int(sys.argv[3])).savev(sys.argv[2], 'png', [], [])
" "$here/jscope.svg" "$icons/${s}x${s}/apps/jscope.png" "$s"
    done
else
    echo "python3-gi not found - installing the SVG only" >&2
fi

# Exec must be absolute, and this repo can live anywhere, so it is written at
# install time rather than baked into the committed file.
mkdir -p "$apps"
# Exec points at the crash-capturing wrapper, not the bare binary: launched from
# a desktop entry there is no terminal, so a segfault would otherwise leave
# nothing but a window that vanished. See packaging/jscope-launch.sh.
sed "s|^Exec=.*|Exec=$here/jscope-launch.sh|" "$here/jscope.desktop" > "$apps/jscope.desktop"
chmod 644 "$apps/jscope.desktop"



command -v update-desktop-database >/dev/null && update-desktop-database "$apps" || true

# NO gtk-update-icon-cache here, deliberately.
#
# ~/.local/share/icons/hicolor has no index.theme of its own - it is merged with
# /usr/share/icons/hicolor, which supplies one. Writing a cache into a directory
# that has no index.theme still reports "Cache file created successfully", and
# GTK then trusts that empty cache INSTEAD of reading the files, so every icon
# under it disappears. That is exactly what happened here: all seven PNGs and the
# SVG were present on disk and the launcher showed nothing.
#
# Deleting any cache left by an earlier run, and touching the directory so its
# mtime tells GTK to rescan, is what actually makes the icon appear.
rm -f "$icons/icon-theme.cache"
touch "$icons"

echo "installed:"
echo "  $apps/jscope.desktop  ->  $here/jscope-launch.sh  ->  $bin"
echo "  $icons/scalable/apps/jscope.svg"

# Prove the theme can find it rather than assuming: the failure above was silent
# in both directions, which is why it went unnoticed until the launcher was used.
if python3 -c "import gi" 2>/dev/null; then
    python3 -c "
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk
t = Gtk.IconTheme.get_default(); t.rescan_if_needed()
i = t.lookup_icon('jscope', 48, 0)
print('  icon lookup:', i.get_filename() if i else 'NOT FOUND - the launcher will show a placeholder')
" 2>/dev/null || true
fi
