# WeatherFiles plugin — UI build plan

How the WeatherFiles-specific UI lands on top of the renamed OpenCPN template.
Tracks what's scaffolded, what to build next, and how to compile/test each step.

> **Build reality:** the C++ in this branch was written without a local OpenCPN
> SDK / wx 3.2 toolchain, so it has **not been compiled here**. Treat each
> increment as "scaffold → you build in OpenCPN → we fix what the compiler
> finds." Nothing below is verified-running until you've built it.

## What the plugin does

A thin client of the public WeatherFiles API (`https://api.weatherfiles.com/v1`,
docs at developers.weatherfiles.com). The user flow:

1. Paste a personal access token (`wf_pat_…`, created at
   weatherfiles.com/account/tokens) into the plugin preferences.
2. Browse the model catalogue (bounds, params, resolution, cadence).
3. Draw an area on the chart, pick params + horizon, download a sliced GRIB2.
4. The file opens in OpenCPN's built-in GRIB display.

## HTTP: why wxWebRequest, not OCPN_downloadFile

`OCPN_downloadFile()` (opencpn-libs/api-21/ocpn_plugin.h) has **no custom-header
parameter**, so it can't send `Authorization: Bearer wf_pat_…`. The JSON
endpoints (`/v1/auth/me`, `/v1/models`) need that header, so they go through
**wxWebRequest** (wxWidgets 3.2, supports `SetHeader`). JSON is parsed with
**wxJSON** (already vendored in `opencpn-libs/`; no wxcurl, no extra deps).

The eventual **GRIB download** (`GET /v1/dl/{token}`) carries auth *in the URL*
(the slice token), so that one CAN use `OCPN_downloadFile` and drop straight
into the GRIB-display handoff — no header needed there.

## Increments

### 0. API client foundation — *scaffolded, this branch*
- `include/wf_api.h` / `src/wf_api.cpp` — `WfApi : public wxEvtHandler`.
  - `ValidateToken(cb)` → `GET /v1/auth/me` → `WfAccount{email, tier,
    daily_download_limit, daily_downloads_used_today, allowed_models}`.
  - `FetchModels(cb)` → `GET /v1/models` → `vector<WfModel>{id, name, source,
    bounds, params, format, forecast_horizon_h, step_h, run_interval_h}`.
  - Single request in flight at a time (calls are sequential); async callbacks
    fire on the GUI thread.
- **CMake:** done — `src/wf_api.cpp`/`include/wf_api.h` are in `SRCS`/`HDRS`
  (CMakeLists.txt), and wxWebRequest ships in the wx `net` library, already in
  `wxWidgets_USE_LIBS` (cmake/PluginConfigure.cmake). No further wiring needed.
- **Build check:** compiles + links; no runtime UI yet.

### 1. Preferences: token entry + validate — *scaffolded*
- In `weatherfiles_pi::ShowPreferencesDialog(wxWindow* parent)`, build a small
  dialog: a `wxTextCtrl` for the token, a **Validate** button, a status label.
- Persist the token via OpenCPN's config (`GetOCPNConfigObject()` →
  `[PlugIns/WeatherFiles] Token=`). Load it in `Init()`.
- Validate calls `WfApi::ValidateToken`; on success show
  `tier · used/limit today`, on failure show the error.
- **Build check:** paste a real PAT, click Validate, see the account line;
  bad/empty token shows the 401 message; token survives an OpenCPN restart.

### 2. Model list panel
- A panel (toolbar button → `wxFrame`/`AUI` pane) listing models from
  `WfApi::FetchModels`: name, source, bounds, params, horizon.
- Grey out / annotate models not in `allowed_models` for the current tier.
- **Build check:** panel populates from live `/v1/models`; tier gating matches
  the account.

### 3. Area select + download (later milestone)
- Rubber-band an area on the chart → bbox; pick params + horizon.
- `GET /v1/grib?model=&params=&bbox=&time_window_h=&time_step_h=` (one-shot,
  no persisted slice) → save to the GRIB dir → hand to OpenCPN's GRIB display.
- Show quota use; handle 403 (tier), 413 (size cap), 503 (no current run).

## Build wiring (CMake) — already in place

1. `src/wf_api.cpp` + `include/wf_api.h` are in `SRCS`/`HDRS` (CMakeLists.txt
   ~line 183).
2. wxWebRequest is part of the wx **`net`** library (since 3.1.5), and `net` is
   already in `wxWidgets_USE_LIBS` (cmake/PluginConfigure.cmake ~line 589) — so
   it's linked. No separate `webrequest` component to add.
3. On Linux, wxWebRequest needs a backend (libcurl) at runtime; macOS/Windows
   use the OS HTTP stack. No new vendored deps.

## How to build + test locally (macOS, fast loop)

Mirrors the CircleCI `build-macos-universal` job, but lets you rebuild
incrementally in seconds instead of waiting on CI.

```sh
# One-time setup: brew deps + prebuilt universal wx into /usr/local (sudo) +
# cmake configure. (Installs cmake + wget via brew if missing.)
ci/build-macos-local.sh --setup

# Fast loop: edit code, then rebuild -> package -> importable tarball.
ci/build-macos-local.sh
```

The fast loop prints the path to `build/*-import.tar.gz`. Import it via OpenCPN
**Options → Plugins → Import plugin**, enable WeatherFiles, open its
Preferences. Local builds aren't quarantined, so no `xattr` step is needed (a
CI-downloaded tarball does need `xattr -dr com.apple.quarantine <file>` first).

Both the local helper and CI call `ci/make-importable-tarball.sh`, which injects
the build's metadata `*.xml` into the CPack tarball as `metadata.xml` at the
payload root — without it OpenCPN's import fails with "Error extracting metadata
from tarball".

Universal (arm64+x86_64) matches CI but doubles compile time; for pure local
testing, reconfigure native-only once (`-DCMAKE_OSX_ARCHITECTURES=$(uname -m)`)
to roughly halve it.

Quick API sanity outside the plugin (confirms a token before testing in C++):

```sh
curl -H "Authorization: Bearer wf_pat_…" https://api.weatherfiles.com/v1/auth/me
curl -H "Authorization: Bearer wf_pat_…" https://api.weatherfiles.com/v1/models | head
```
