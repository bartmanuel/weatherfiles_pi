# weatherfiles_pi

An [OpenCPN](https://opencpn.org/) plugin for [WeatherFiles](https://weatherfiles.com) —
browse 27+ European weather models, pick an area on the chart, and download sliced
GRIB2 files that open directly in OpenCPN's built-in GRIB display. No email
round-trips, no SSB workflow: reliable HTTPS download straight into your chartplotter.

> **Status: early scaffold (v0.1.0).** The repository is seeded from the official
> OpenCPN plugin template ([`jongough/testplugin_pi`](https://github.com/jongough/testplugin_pi))
> and renamed; the WeatherFiles-specific UI (token prefs, model list, area-select,
> download → GRIB display) is being built on top. Not yet in the OpenCPN catalog.

## How it works

The plugin is a client of the public **WeatherFiles API** (`https://api.weatherfiles.com/v1`):

- **Authenticate** with a personal access token (`wf_pat_…`) you create at
  [weatherfiles.com/account/tokens](https://weatherfiles.com/account/tokens) and paste
  into the plugin's preferences.
- **Browse models** via `GET /v1/models` (bounds, parameters, resolution, run cadence).
- **Download** a one-shot GRIB via `GET /v1/grib?model=…&params=…&bbox=…`, or create a
  reusable slice (`POST /v1/slices` → `GET /v1/dl/{token}`).

Full API docs + an interactive reference: **[developers.weatherfiles.com](https://developers.weatherfiles.com)**.

## Building

Standard OpenCPN FrontEnd-2 (FE2) CMake build. Quickest path to a Plugin-Manager-importable package:

```sh
git clone --recurse-submodules https://github.com/bartmanuel/weatherfiles_pi.git
cd weatherfiles_pi
rm -rf build; mkdir build; cd build
bash ../build-local-package-example.sh
```

Targets OpenCPN plugin API **1.18**, wxWidgets **3.2**. CircleCI builds the
cross-platform (macOS / Linux / Windows / Android) tarballs on push.

## License

GPL-3.0 (inherited from the OpenCPN plugin template). See [`LICENSE`](LICENSE).
