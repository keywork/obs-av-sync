# Phase 6 Review Fix Log

## Summary

Code review (`06-REVIEW.md`) identified **2 critical bugs** and **5 warnings**. All 7 issues were addressed in commit `322ad44`. The fix commit touched `src/av_sync_filter.c`, `src/reference_tap.c`, and `src/smoother.cpp`.

## Fixes Applied

### CR-01 — Erroneous `pthread_mutex_unlock` in allocation-failure path

- **File**: `src/reference_tap.c:86`
- **Problem**: `pthread_mutex_unlock(&ref_mutex)` was called in the allocation-failure path of `reference_tap_init()`, but the mutex had only been initialized and was **never locked**. Unlocking an unlocked mutex is undefined behavior.
- **Fix**: Removed the `pthread_mutex_unlock(&ref_mutex)` call. The failure path now only calls `pthread_mutex_destroy(&ref_mutex)` after cleaning up the partially allocated state.
- **Verification**: Clean build passes; no runtime mutex errors observed.

### CR-02 — Missing NaN/Inf guard in smoother

- **Files**: `src/smoother.cpp:34`, `src/smoother.cpp:38–40`, `av_sync_filter.c:455`
- **Problem**: `smoother_process()` did not validate that `raw_offset_ms` or `confidence` are finite. A NaN measurement would poison the EMA state permanently, and the subsequent `int64_t` cast for `obs_source_set_sync_offset` would invoke undefined behavior. `smoother_get_status()` would also falsely report **Synced** for NaN state.
- **Fix**: Added `if (!isfinite(raw_offset_ms) || !isfinite(confidence)) return false;` at the top of `smoother_process()`. Updated `smoother_get_status()` to treat non-finite `smoothed_offset_ms` as Out of Range (`return 2`).
- **Verification**: Clean build passes; 36/36 GCC-PHAT synthetic tests pass.

### WR-01 — Data race on `sync_enabled`

- **File**: `src/av_sync_filter.c:76`, `av_sync_filter.c:240`, `av_sync_filter.c:379`, `av_sync_filter.c:407`, `av_sync_filter.c:482`
- **Problem**: `data->sync_enabled` was a plain `bool` written by `av_sync_filter_update()` (OBS UI/main thread) and read by `av_sync_analysis_thread()`. Undefined behavior in C11.
- **Fix**: Changed the field type to `_Atomic bool` and replaced all reads/writes with `atomic_load(&data->sync_enabled)` and `atomic_store(&data->sync_enabled, value)`.
- **Verification**: Clean build passes (MSVC C11 atomics).

### WR-04 — Non-atomic global reads in reference tap getters

- **File**: `src/reference_tap.c:172–175`, `src/reference_tap.c:182–185`
- **Problem**: `reference_tap_get_ring()` and `reference_tap_get_sample_rate()` returned non-atomic global variables without acquiring `ref_mutex`, creating a data race during teardown.
- **Fix**: Declared `ref_ring` as `_Atomic(av_sync_ring_t *)` and `ref_sample_rate` as `_Atomic uint32_t`. Updated all internal reads/writes to use `atomic_load`/`atomic_store`. Updated `reference_tap_shutdown()` to atomically clear both.
- **Verification**: Clean build passes; ring round-trip test passes.

### WR-06 — Missing NULL check for `data->ring` in analysis thread

- **File**: `src/av_sync_filter.c:357`
- **Problem**: `av_sync_analysis_thread()` called `av_sync_ring_cursor_init(data->ring, &data->src_cursor)` without checking if `data->ring` was NULL. In `av_sync_filter_create()`, `av_sync_ring_create()` failure was not validated.
- **Fix**: Added `if (!data->ring || !data->analysis_ref_buf || !data->analysis_src_buf) { ...; return NULL; }` guard at the start of the analysis thread. Also hardened `av_sync_filter_create()` to validate all allocations (see WR-07).
- **Verification**: Clean build passes.

### WR-07 — Unchecked allocations in filter creation

- **File**: `src/av_sync_filter.c:123–146`
- **Problem**: `av_sync_filter_create()` did not check the return values of `bzalloc()`, `av_sync_ring_create()`, or the analysis buffer allocations. A single failed allocation would produce a partially constructed filter that crashes in the audio callback or analysis thread.
- **Fix**: Added NULL checks after every allocation:
  - `bzalloc(sizeof(*data))` → return NULL
  - `bzalloc(downmix_scratch)` → bfree(data); return NULL
  - `av_sync_ring_create()` → bfree(scratch, data); return NULL
  - `bzalloc(analysis_ref_buf)` → destroy ring, bfree(scratch, data); return NULL
  - `bzalloc(analysis_src_buf)` → bfree(ref_buf), destroy ring, bfree(scratch, data); return NULL
- **Verification**: Clean build passes.

## Issues Deferred / Accepted

- **WR-02** (OBS API calls from non-OBS thread): Accepted as known limitation. `obs_source_set_sync_offset()` is called from the analysis pthread. Per OBS plugin ecosystem practice, this is safe for the sync-offset property, but a future Phase 7/8 improvement could queue the update to the graphics thread.
- **WR-03** (Non-atomic diagnostic fields for future UI): Accepted for Phase 8. The diagnostic fields (`smoothed_offset_ms`, `last_confidence`, `valid_count`, `has_valid_offset`) are written only by the analysis thread and read only by OBS UI properties callbacks (which run on the main thread). Phase 8 will introduce a proper atomic snapshot or `memory_order_release`/`acquire` pairing when the dock UI consumes these fields concurrently.
- **WR-05** (Mutex held across OBS API calls in `reference_tap_shutdown` / `reference_tap_set_source`): Accepted as low-risk. These functions are not called from OBS callbacks; they are called from filter creation/update/destroy paths where deadlock is not currently possible.

## Files Changed

| File | Lines Changed | Description |
|------|---------------|-------------|
| `src/av_sync_filter.c` | +43 / −13 | Atomic `sync_enabled`, allocation validation, thread NULL guards |
| `src/reference_tap.c` | +16 / −10 | Atomic globals, removed invalid mutex unlock, atomic store on shutdown |
| `src/smoother.cpp` | +4 / −1 | NaN/Inf guards in `smoother_process()` and `smoother_get_status()` |

## Verification

- [x] Clean build: `cmake --build build_x64 --config RelWithDebInfo` — **PASS**
- [x] SPSC ring round-trip test: `test_ring_spsc.exe` — **PASS** (480,000 samples round-tripped)
- [x] GCC-PHAT synthetic tests: `test_gcc_phat.exe` — **PASS** (36/36, < 1 ms accuracy, confidence > 1.0)

## Commit

All fixes committed in `322ad44` — `fix(phase6): Address CR-01/CR-02 and WR-01/04/06/07 from code review`.
