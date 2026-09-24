#!/usr/bin/env bash
# Build every file a jscope release carries, for the version in CMakeLists.txt:
#
#   dist/release-<version>/
#       jscope-<version>-x86_64.AppImage   Linux, updates itself
#       jscope_<version>_amd64.deb         Linux, for a package manager to own
#       jscope-<version>-setup.exe         Windows installer, updates itself
#       SHA256SUMS                         `sha256sum` of the three
#
#   packaging/build-release.sh
#
# SHA256SUMS is not optional: jscope's updater refuses to install a release without it, because the
# checksum is what proves the file that arrived is the file that was published. The update picks its
# file by name -- the one ending -x86_64.AppImage on Linux, -setup.exe on Windows -- so these names are
# part of the contract. Attach all four files to a GitHub release tagged v<version>.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$ROOT/dist"

fail() { echo "build-release: $*" >&2; exit 1; }

VERSION="$(sed -n 's/.*project(jscope VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || fail "could not read the version out of CMakeLists.txt"
OUT="$DIST/release-$VERSION"

# A release is cut from committed work: SOURCE.txt in the Windows package points at a commit, and a
# binary built from anything else cannot honestly point at one.
git -C "$ROOT" diff-index --quiet HEAD -- || fail "uncommitted changes -- commit first"

cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$ROOT/build" --target jscope --parallel
"$ROOT/packaging/build-deb.sh"
"$ROOT/packaging/build-appimage.sh"
"$ROOT/packaging/package-windows.sh"

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$DIST/jscope-$VERSION-x86_64.AppImage" "$DIST/jscope_${VERSION}_amd64.deb" \
   "$DIST/jscope-$VERSION-setup.exe" "$OUT/"
( cd "$OUT" && sha256sum jscope-* > SHA256SUMS )

echo
echo "release $VERSION: $OUT"
( cd "$OUT" && ls -l | sed 's/^/  /' && sed 's/^/  /' SHA256SUMS )
