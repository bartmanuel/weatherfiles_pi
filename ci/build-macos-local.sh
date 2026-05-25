#!/usr/bin/env bash
#
# Local macOS plugin build, mirroring the CircleCI build-macos-universal job,
# for a fast dev loop without waiting on CI.
#
#   ci/build-macos-local.sh --setup   # one-time: brew deps + prebuilt universal
#                                      # wx into /usr/local (sudo) + cmake configure
#   ci/build-macos-local.sh           # fast loop: incremental build -> package
#                                      # -> OpenCPN-importable tarball
#
# After --setup, just rerun the no-arg form on each code change (incremental
# make = seconds) and import build/*-import.tar.gz via OpenCPN -> Options ->
# Plugins -> Import plugin. Local builds aren't quarantined, so no xattr step.
#
# Tip: universal (arm64+x86_64) matches CI but doubles compile time. For pure
# local testing you can halve it by reconfiguring native-only once:
#   cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES=$(uname -m) ...

set -euo pipefail
here=$(cd "$(dirname "$0")"; pwd)
root=$(cd "$here/.."; pwd)
cd "$root"

if [ "${1:-}" = "--setup" ]; then
  command -v cmake >/dev/null || brew install cmake
  command -v wget  >/dev/null || brew install wget
  # Installs deps, downloads prebuilt universal wx into /usr/local (sudo), and
  # runs the universal cmake configure; stops after configure ($CI is unset).
  bash ci/circleci-build-macos-universal.sh
  # The CI script doesn't check cmake's exit code, so a failed configure looks
  # like success. Verify the Makefile was actually generated.
  if [ ! -f build/Makefile ]; then
    echo >&2
    echo "ERROR: cmake configure did not produce build/Makefile." >&2
    echo "Last CMake errors from build/build.log:" >&2
    grep -i -A5 "CMake Error" build/build.log >&2 || true
    exit 1
  fi
  echo
  echo "Setup complete. Build with:  ci/build-macos-local.sh"
  exit 0
fi

if [ ! -f build/Makefile ]; then
  echo "build/ is not configured (no Makefile) - run '$0 --setup' first." >&2
  exit 1
fi

jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
# `make package` first invocation can miss tarball-conf-stamp; retry once (same
# quirk the CI script works around).
( cd build && make -j"$jobs" && { make package || make package; } )
bash ci/make-importable-tarball.sh build

echo
echo "Done. Import this into OpenCPN (Options -> Plugins -> Import plugin):"
ls -1 "$root"/build/*-import.tar.gz
