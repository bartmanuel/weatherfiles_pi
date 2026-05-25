#!/usr/bin/env bash
#
# Repack the CPack TGZ in <build-dir> into an OpenCPN-importable tarball.
#
# OpenCPN's "Import plugin" needs metadata.xml at the payload root (next to
# OpenCPN.app), but the CPack build emits the metadata as a separate sibling
# *.xml (the catalog combines them later; local import does not) — so a raw
# CPack tarball fails with "Error extracting metadata from tarball". This injects
# the metadata and re-tars.
#
# Used by both the CircleCI macOS job and ci/build-macos-local.sh so the format
# stays identical. Prints the path to the importable tarball.
#
# Usage: ci/make-importable-tarball.sh [build-dir]   (default: build)

set -euo pipefail

BUILD="${1:-build}"
cd "$BUILD"

# The CPack tarball, ignoring any importable tarball from a previous run.
TARBALL="$(ls -1 *.tar.gz 2>/dev/null | grep -v -- '-import\.tar\.gz' | head -1 || true)"
META="$(ls -1 *.xml 2>/dev/null | head -1 || true)"
[ -n "$TARBALL" ] || { echo "make-importable: no CPack tarball in $BUILD" >&2; exit 1; }
[ -n "$META" ]    || { echo "make-importable: no metadata .xml in $BUILD" >&2; exit 1; }

rm -rf _import && mkdir _import
tar -C _import -xzf "$TARBALL"

# Place metadata.xml at the payload root — the dir that holds OpenCPN.app on
# macOS (falls back to the extraction root for other layouts).
APP="$(find _import -maxdepth 3 -name OpenCPN.app -type d | head -1 || true)"
if [ -z "$APP" ]; then ROOT="_import"; else ROOT="$(dirname "$APP")"; fi
cp "$META" "$ROOT/metadata.xml"

IMPORT="${TARBALL%.tar.gz}-import.tar.gz"
tar -C "$ROOT" -czf "$IMPORT" .

echo "make-importable: wrote $BUILD/$IMPORT"
echo "make-importable: contents (head):"
tar -tzf "$IMPORT" | head -20
