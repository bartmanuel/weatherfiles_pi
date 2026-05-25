#!/usr/bin/env bash
#
# Repack the CPack TGZ in <build-dir> into an OpenCPN-importable tarball.
#
# OpenCPN's "Import plugin" needs metadata.xml at the payload root, but the
# CPack build emits the metadata as a separate sibling *.xml (the catalog
# combines them later; local import does not) — so a raw CPack tarball fails
# with "Error extracting metadata from tarball". This injects the metadata and
# re-tars, for both macOS (OpenCPN.app payload) and Linux (bin/lib/share).
#
# Used by the CircleCI macOS + Linux jobs and ci/build-macos-local.sh so the
# format stays identical. Prints the path to the importable tarball.
#
# Usage: ci/make-importable-tarball.sh [build-dir]   (default: build)

set -euo pipefail

BUILD="${1:-build}"
cd "$BUILD"

# Newest CPack tarball + metadata, ignoring any importable tarball from a
# previous run (-t = sort by mtime so a stale artifact is never picked).
TARBALL="$(ls -1t *.tar.gz 2>/dev/null | grep -v -- '-import\.tar\.gz' | head -1 || true)"
META="$(ls -1t *.xml 2>/dev/null | head -1 || true)"
[ -n "$TARBALL" ] || { echo "make-importable: no CPack tarball in $BUILD" >&2; exit 1; }
[ -n "$META" ]    || { echo "make-importable: no metadata .xml in $BUILD" >&2; exit 1; }

rm -rf _import && mkdir _import
tar -C _import -xzf "$TARBALL"
echo "make-importable: extracted payload (depth 2):"
( cd _import && find . -maxdepth 2 | sed 's|^\./||' | grep -v '^$' | head -30 )

# Payload root: CPack archives wrap everything in a single top-level dir (the
# package-name prefix). metadata.xml must sit inside it, alongside the platform
# tree (OpenCPN.app on macOS; bin/lib/share or usr/ on Linux/Windows). If
# there's no single prefix dir, use the extraction root.
COUNT="$(ls -A _import | wc -l | tr -d ' ')"
FIRST="$(ls -A _import | head -1)"
if [ "$COUNT" = "1" ] && [ -d "_import/$FIRST" ]; then
  ROOT="_import/$FIRST"
else
  ROOT="_import"
fi
echo "make-importable: payload root = $ROOT"
cp "$META" "$ROOT/metadata.xml"

IMPORT="${TARBALL%.tar.gz}-import.tar.gz"
tar -C "$ROOT" -czf "$IMPORT" .

echo "make-importable: wrote $BUILD/$IMPORT"
echo "make-importable: contents (head):"
tar -tzf "$IMPORT" | sed 's|^|  |' | head -20
