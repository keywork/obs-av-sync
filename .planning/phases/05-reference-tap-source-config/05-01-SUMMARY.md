# Plan 05-01 Execution Summary

**Date:** 2026-05-03
**Plan:** 05-01 — Implement reference_tap.c
**Status:** COMPLETE

## Tasks Implemented

### T01: Create src/reference_tap.h
- Created `src/reference_tap.h` with the public API:
  - `reference_tap_init(void)` / `reference_tap_shutdown(void)`
  - `reference_tap_set_source(const char *name)`
  - `reference_tap_get_ring(void)`

### T02: Create src/reference_tap.c
- Created `src/reference_tap.c` with static global state protected by `pthread_mutex_t`:
  - `ref_mutex`, `ref_name[256]`, `ref_source`, `ref_ring`
  - `ref_sample_rate`, `ref_downmix_scratch`, `ref_downmix_capacity`
- Implemented `reference_audio_callback` that:
  - Downmixes multi-plane PCM to mono
  - Writes to the shared `av_sync_ring_t` via `av_sync_ring_write`
  - No heap allocation in the audio callback path
- Implemented `reference_tap_init`:
  - Initializes mutex, queries sample rate, allocates 10-second ring buffer and 1-second downmix scratch
- Implemented `reference_tap_shutdown`:
  - Removes audio capture callback, releases source, destroys ring, frees scratch, destroys mutex
- Implemented `reference_tap_set_source`:
  - Thread-safe attach/detach with source validation (exists, has audio output)
  - No-op if the same source is re-selected
- Implemented `reference_tap_get_ring`:
  - Returns the shared ring pointer under mutex protection

### T03: Wire into plugin-main.c
- Added `#include "reference_tap.h"`
- Called `reference_tap_init()` in `obs_module_load` (returns false on failure)
- Called `reference_tap_shutdown()` in `obs_module_unload`

### T04: Include reference_tap.c in CMake build
- `CMakeLists.txt` already contained `src/reference_tap.c` in `target_sources`
- No additional changes needed

### T05: Update av_sync_filter_create
- Added `#include "reference_tap.h"`
- Added startup validation block in `av_sync_filter_create`:
  - Reads `reference_source_name` from settings
  - Validates the source exists via `obs_get_source_by_name`
  - Calls `reference_tap_set_source(saved_ref)` if valid
  - Logs warning if saved reference source not found at startup
- Added forward declaration for `av_sync_filter_update` to fix C4013/C2371 build errors

## Verification

| Check | Result |
|-------|--------|
| Build (main target `obs-av-sync`) | PASS — zero errors |
| `pthread_mutex_*` in `reference_tap.c` | PASS — init, lock, unlock, destroy all present |
| `obs_source_add_audio_capture_callback` / `remove` | PASS — 4 occurrences |
| `reference_tap_init` / `shutdown` in `plugin-main.c` | PASS — both called |
| `reference_tap_set_source` in `av_sync_filter.c` | PASS — present in `av_sync_filter_create` |

## Deviations / Notes

- `av_sync_filter.c` contained unstaged modifications from later Phase-5 plans (properties, `av_sync_filter_update`, locale strings). These were preserved and a forward declaration was added to resolve a C4013/C2371 compiler error caused by calling `av_sync_filter_update` before its definition.
- `CMakeLists.txt` already had `src/reference_tap.c` added and `ENABLE_FRONTEND_API=ON` from prior unstaged work; no edits were required for T04.
- Test target `test_gcc_phat` has a pre-existing LNK1218 (PDB warning treated as error) unrelated to this plan. The main plugin DLL builds cleanly.

## Files Changed

- `src/reference_tap.h` (new)
- `src/reference_tap.c` (new)
- `src/plugin-main.c`
- `src/av_sync_filter.c`
- `CMakeLists.txt` (already contained change from prior work)
- `data/locale/en-US.ini` (unstaged from prior work, preserved)
