#!/usr/bin/env bash
# Render packaging/jscope.ico from packaging/jscope.svg.
#
# The SVG is the single source for the application's icon on every platform;
# this is the Windows half of it, since Windows wants a .ico resource compiled
# into the executable rather than a file the desktop looks up by name.
#
# Rendered at each size RATHER THAN SCALED FROM ONE. A 256px render squashed to
# 16px turns the graticule into grey mush, because the lines fall between
# pixels; rendering at 16 lets the rasteriser put each line on a pixel. Windows
# picks whichever size it needs -- 16 in the title bar, 32 in alt-tab, 256 in
# Explorer's large-icon view -- so all of them are in the file and each one is
# drawn for the size it will actually be seen at.
#
# rsvg-convert, not ImageMagick's own SVG support: the internal MSVG renderer
# ignores fill="none" and fills the CH1 trace path solid black, which looks
# exactly like a broken icon because it is one.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SVG="$ROOT/packaging/jscope.svg"
ICO="$ROOT/packaging/jscope.ico"

command -v rsvg-convert >/dev/null || { echo "make-icon: no rsvg-convert (apt install librsvg2-bin)" >&2; exit 1; }
command -v magick       >/dev/null || { echo "make-icon: no ImageMagick" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

SIZES="16 24 32 48 64 128 256"
for s in $SIZES; do
    rsvg-convert -w "$s" -h "$s" -o "$TMP/$s.png" "$SVG"
done

magick $(for s in $SIZES; do echo "$TMP/$s.png"; done) "$ICO"

echo "wrote $ICO"
magick identify "$ICO" | sed 's/^/  /'
