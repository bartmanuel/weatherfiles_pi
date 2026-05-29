#!/usr/bin/env bash
#
# Local Linux build via Docker, mirroring the CI build-debian-x86_64-12-bookworm
# job but runnable on a Mac (the CI script assumes a CircleCI Linux host). Builds
# inside a debian:bookworm container; on Apple Silicon that's an ARM64 build,
# which is what an arm64 UTM VM needs (the x86_64 CI artifact won't load there).
#
# Requires a running Docker daemon (colima start). Output:
#   build-linux/weatherfiles_pi-*-import.tar.gz   -> import into OpenCPN (Linux)
#
# Override the base image for a different distro/arch, e.g.:
#   WF_LINUX_IMAGE=arm64v8/debian:bookworm ci/build-linux-local.sh
#   WF_LINUX_IMAGE=amd64/debian:bookworm   ci/build-linux-local.sh   # (emulated)

set -euo pipefail
here=$(cd "$(dirname "$0")"; pwd)
root=$(cd "$here/.."; pwd)

IMAGE="${WF_LINUX_IMAGE:-debian:bookworm}"
echo "Building the Linux plugin in $IMAGE (host arch: $(uname -m))..."

# A Linux plugin must declare the SAME distro/version as the OpenCPN running it,
# or OpenCPN rejects it as "incompatible". BUILD_ENV + OCPN_TARGET drive that
# (PluginSetup.cmake): debian -> <target>debian-<arch></target> + version from
# lsb_release; ubuntu+jammy -> <target>ubuntu-<arch></target>. On Ubuntu, wx3.2
# isn't in 22.04's repos, so add the OpenCPN PPA (same source as the user's
# OpenCPN).
docker run --rm -v "$root":/src -w /src "$IMAGE" bash -ec '
  set -e
  export DEBIAN_FRONTEND=noninteractive
  apt-get -qq update
  apt-get -y --no-install-recommends install ca-certificates gnupg \
    software-properties-common lsb-release >/dev/null
  if grep -qi ubuntu /etc/os-release; then
    add-apt-repository -y ppa:opencpn/opencpn
    apt-get -qq update
    export BUILD_ENV=ubuntu OCPN_TARGET=jammy
  else
    export BUILD_ENV=debian OCPN_TARGET=bookworm
  fi
  apt-get -y --no-install-recommends install \
    build-essential cmake gettext git pkg-config wx-common \
    libgtk2.0-dev libwxgtk3.2-dev \
    libcurl4-openssl-dev libbz2-dev libexpat1-dev libcairo2-dev \
    libarchive-dev liblzma-dev libexif-dev libssl-dev >/dev/null
  export WX_VER=32 BUILD_GTK3=true
  echo "build-env=$BUILD_ENV ocpn-target=$OCPN_TARGET version=$(lsb_release -rs)"
  rm -rf build-linux && mkdir build-linux && cd build-linux
  cmake -DCMAKE_BUILD_TYPE=Release ..
  make -j"$(nproc)"
  make package || make package
  chmod -R a+rw .
'

# Repack on the host (build-linux/ is bind-mounted and now populated).
bash "$here/make-importable-tarball.sh" build-linux
echo
echo "Done. Import this into OpenCPN in your Linux VM (Options -> Plugins ->"
echo "Import plugin):"
ls -1 "$root"/build-linux/*-import.tar.gz
