# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`obs-av-sync` — an OBS Studio plugin that automatically measures per-source audio offsets against a designated reference ("house") audio source and applies them via OBS's built-in per-source `sync_offset`. Target: hands-off multi-camera AV sync for live productions.

Core signal-processing approach: **GCC-PHAT** (generalized cross-correlation with phase transform) between each tracked source's audio and the reference, on sliding windows, with smoothing/hysteresis before applying offset changes. See `docs/ARCHITECTURE.md` for the full data flow and OBS API touchpoints, and `docs/ROADMAP.md` for the phased build plan.

## Repository layout

This repo is derived from the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate). Do **not** restructure the template — the GitHub Actions workflows, CMake presets, and helper scripts under `.github/`, `cmake/`, and `build-aux/` depend on the layout.

- `src/` — plugin source (C today; C++ can be added via `target_sources`)
- `data/locale/en-US.ini` — translatable strings
- `cmake/` — template's cross-platform CMake helpers (don't edit unless you know what you're changing)
- `buildspec.json` — plugin identity + pinned OBS/dep versions and hashes
- `CMakeLists.txt` — plugin-level build; toggle `ENABLE_FRONTEND_API` / `ENABLE_QT` when adding UI
- `docs/` — project design docs (roadmap, architecture)

## Build & test

The template's canonical builds go through its GitHub Actions workflows. Locally, use CMake presets:

```sh
cmake --preset windows-x64        # or: macos, ubuntu-x86_64
cmake --build --preset windows-x64
```

Build artifacts land under `build_x86_64/` (or equivalent per preset). There is no test harness yet — add one under `tests/` when the DSP layer lands (unit-test GCC-PHAT with synthetic delayed signals; see roadmap).

Format checks: `build-aux/run-clang-format` and `build-aux/run-gersemi` enforce style (enforced in CI via `check-format` workflow).

## Working on this plugin — things Claude should keep in mind

- **Language**: the template ships C. C++ is allowed and expected for the DSP/state code; add `.cpp` files via `target_sources(${CMAKE_PROJECT_NAME} PRIVATE …)` in `CMakeLists.txt`. Don't convert `plugin-main.c` to C++ — keep the OBS module entry point as C to minimize surprises with `OBS_DECLARE_MODULE`.
- **UI**: any Qt/dock UI requires setting `ENABLE_FRONTEND_API` and `ENABLE_QT` to `ON` in `CMakeLists.txt`. Defer this until the headless sync engine works.
- **Audio tap**: use `obs_source_add_audio_capture_callback()` to receive `struct audio_data` for a source without being in its filter chain. For a "filter-based" approach (per-source audio filter), register via `obs_register_source` with `OBS_SOURCE_TYPE_FILTER | OBS_SOURCE_AUDIO`. Filter approach is preferred for per-source state and user-visible configuration.
- **Sync application**: `obs_source_set_sync_offset(source, offset_ns)` — offset is in nanoseconds, positive = delay video relative to audio. This is the existing OBS knob; we just drive it.
- **Cross-platform**: Windows is primary but keep code portable — no Win32-only APIs in the core. Any FFT dependency must build on all three platforms (KissFFT and PFFFT are good candidates; avoid FFTW due to GPL-licence pollution into dependents).
- **Licensing**: project is GPL-2.0-or-later to match OBS. Do not add dependencies under incompatible licenses (Apache-2.0 with patent clauses, proprietary, etc.).

## Commit & PR conventions

- **No `Co-Authored-By: Claude ...` trailers in commits.**
- **No "Generated with Claude Code" footer in PR bodies.**
- Default branch: `main`.
- Keep commit subjects short and imperative; explain the "why" in the body when non-obvious.

## References

- OBS plugin API: https://docs.obsproject.com/
- Plugin template wiki: https://github.com/obsproject/obs-plugintemplate/wiki
- GCC-PHAT reference: Knapp & Carter 1976, "The Generalized Correlation Method for Estimation of Time Delay"
