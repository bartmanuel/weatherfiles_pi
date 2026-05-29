# weatherfiles_pi

An [OpenCPN](https://opencpn.org/) plugin for [WeatherFiles](https://weatherfiles.com) —
browse 27+ European weather models, pick an area on the chart, and download sliced
GRIB2 files that open directly in OpenCPN's built-in GRIB display. No email
round-trips, no SailDocs ritual: reliable HTTPS download straight into your
chartplotter.

> **Status (2026-05-29):** functionally complete on **macOS**, **Linux** and
> **Windows** desktop; first **Alpha submission** to the OpenCPN catalog is in
> flight. Until then, install by downloading an importable tarball from
> [Cloudsmith](https://cloudsmith.io/~bartmanuel-fgsm/repos/weatherfiles-alpha/)
> and using *Options → Plugins → Import plugin*.

## How it works

The plugin adds one toolbar button to OpenCPN. Pressing it opens a small
wizard that walks you from "what do I want" to a GRIB on the chart in a few
clicks.

### 0. Start screen — your saved sets

Opens to your existing **WeatherFiles sets** (collections of slices grouped
by a tag), each with a one-click *Download* button that pulls the latest run
for every slice in the set and hands them to OpenCPN's GRIB viewer. If you
have no saved sets yet (or want a fresh selection), one click on **Download
something else (new area) ▸** drops you into the 3-step wizard.

### Step 1 — Area

Type, click *Use chart view*, or *Draw on chart* (the dialog slides aside so
you can rubber-band a box). N/S/W/E inputs sit on the four edges of a
rectangle that shows the live dimensions in degrees and nautical miles. The
direction letters auto-flip on sign so e.g. typing `-42` in the North field
re-labels it `S`.

### Step 2 — Models

Pick **up to 5 atmospheric, 2 wave and 2 current** models from the catalogue
your tier allows. Each row shows a coloured coverage indicator:

- 🟢 **full** — the area is entirely within the model's domain
- 🟠 **partial** — the area extends past the model's domain
- ⚪️ **outside** — no overlap (the row is disabled)

Model parameters are shown using the canonical short codes from the
WeatherFiles
[models overview page](https://weatherfiles.com/models/overview) (e.g.
`W.G.P.R.T` = wind, gusts, pressure, precipitation, temperature).

### Step 3 — Filters

Bundles **horizon** (forecast hours, capped at the smallest selected
model's native horizon), **step** (`0.25h`/`1h`/`3h`/`6h` filtered by the
coarsest selected step) and **spatial** factor (`1x`/`2x`/`3x`/`4x`
downsample). A `(?)` icon next to each opens a rich tooltip explaining what
it does. Parameters are a 2-column canonical-order checkbox list defaulted
to "all on"; uncheck to skip.

### Downloading

The wizard hides and a download dialog appears with:

- A live progress section per slice, becoming **Done · N of N downloaded
  (kB)** with thousand-separator kB sizes, the on-disk path, and an *Open
  folder* button.
- **Save as Set on WeatherFiles.com** (optional) — type a tag name to
  persist the slices server-side via `POST /v1/slices`. Validates against
  the server's tag rules (`^[a-z0-9-]+$`, max 20 chars) and existing tags
  in real time. Save locks the input and confirms `Saved as '<tag>'.`
- **Open in GRIB viewer** (optional) — pick one model per category
  (checkbox if there is only one in that category, dropdown with
  *--- none ---* if more). Concatenates the chosen GRIB2 files into a
  single `combined.grib2` and sends one `GRIB_APPLY_JSON_CONFIG` message
  to `grib_pi`, which renders wind/waves/currents together on the chart.

GRIBs land under `~/Documents/WeatherFiles/<tag-or-timestamp>/` so you can
always find them later. Saved sets are re-downloadable from Step 0 of the
next session.

### Behind the scenes

The plugin is a thin client of the public **WeatherFiles API**
(`https://api.weatherfiles.com/v1`) with full reference docs and an
interactive console at **[developers.weatherfiles.com](https://developers.weatherfiles.com)**.

- HTTPS-only (`https://` scheme enforced at every HTTP call).
- Bearer token validated against `^[A-Za-z0-9._-]+$` before being
  written to the `Authorization` header (no CRLF injection surface).
- Redirects disabled on both transports (libcurl `CURLOPT_FOLLOWLOCATION=0`
  / WinHTTP `WINHTTP_DISABLE_REDIRECTS`) so the token can't leak to an
  attacker-influenced 3xx hop.
- Native HTTP client per platform: **libcurl** on macOS/Linux,
  **WinHTTP** on Windows (the Debian-packaged wx3.2 ships without
  `wxWebRequest`, hence the split).

## Installing

### From the OpenCPN catalog (preferred — once Alpha approval lands)

In OpenCPN: **Options → Plugins → Update plugin catalog** (set the channel
to *Alpha*), then **Install** next to *WeatherFiles*.

### Sideload (today, Alpha pre-publish)

1. Download the importable tarball for your platform from the
   [Cloudsmith Alpha repo](https://cloudsmith.io/~bartmanuel-fgsm/repos/weatherfiles-alpha/):
   - macOS: `*-darwin-wx32-arm64-x86_64-*-import.tar.gz`
   - Linux: `*-ubuntu-arm64-22.04-jammy-import.tar.gz` (and other distros as they land)
   - Windows: `*-msvc-wx32-*-import.tar.gz`
2. OpenCPN → **Options → Plugins → Import plugin** → pick the file.
3. Open the plugin's preferences, paste a personal access token from
   [weatherfiles.com/account/tokens](https://weatherfiles.com/account/tokens),
   click *Validate*.

### Linux note

OpenCPN Linux plugins are **distro + version + arch specific** —
"Incompatible Import plugin" means the build doesn't match your distro. The
official catalog publishes flatpaks (Phase 2 of the rollout) that work on
every distro; for now match the tarball to your OS.

## Building from source

Standard OpenCPN FrontEnd-2 (FE2) CMake build. Quickest path to a
Plugin-Manager-importable package:

```sh
git clone --recurse-submodules https://github.com/bartmanuel/weatherfiles_pi.git
cd weatherfiles_pi

# Native build on macOS (Apple Silicon or Intel)
bash ci/build-macos-local.sh         # one-shot; needs --setup the first time

# Linux (any distro) via Docker — produces the matching distro/arch tarball
WF_LINUX_IMAGE=ubuntu:22.04 bash ci/build-linux-local.sh
WF_LINUX_IMAGE=debian:bookworm bash ci/build-linux-local.sh

# Windows (MSVC, in CI only for now)
# See .circleci/config.yml `build-msvc-wx32-2022` job.
```

Targets OpenCPN plugin API **1.18** with wxWidgets **3.2**. CircleCI runs
all three desktop matrices on every push and publishes signed tarballs to
the [Cloudsmith Alpha repo](https://cloudsmith.io/~bartmanuel-fgsm/repos/weatherfiles-alpha/).

## Project layout

```
include/              public headers
  weatherfiles_pi.h     plugin entry-point + toolbar wiring
  wf_api.h              WeatherFiles HTTP client (libcurl + WinHTTP)
  wf_multi_slice_dialog.h   3-step wizard
  wf_download_progress.h    download + save + loader dialog
  wf_prefs_dialog.h     token-entry + validate
src/                  implementation (same filenames as include/)
data/                 SVG/PNG toolbar icons + license
ci/                   build + packaging helpers
  build-macos-local.sh
  build-linux-local.sh
  make-importable-tarball.sh
cmake/                FE2 CMake helpers (mostly upstream from testplugin_pi)
```

## License

GPL-3.0 (inherited from the OpenCPN plugin template; required by the
OpenCPN catalog). See [`LICENSE`](LICENSE).
