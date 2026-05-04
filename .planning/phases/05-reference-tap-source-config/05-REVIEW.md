---
status: issues_found
files_reviewed: 6
critical: 2
warning: 4
info: 5
total: 11
---

# Phase 5 Code Review

## Summary
The Phase 5 implementation correctly wires up a global singleton reference tap with per-filter settings persistence. The OBS API usage (source enumeration, property lists, callback registration/release) is idiomatic, and memory management (`bstrdup`/`bfree`) is balanced. However, two critical thread-safety and lifetime issues in `reference_tap.c` will become crashes as soon as the analysis thread (Phase 6+) is introduced: `reference_tap_get_ring` returns a dangling raw pointer without any lifetime contract, and `reference_tap_shutdown` has a racy defensive guard that can trigger undefined behaviour on double-shutdown or init failure. Additionally, the global reference source design allows multiple filters to silently fight over the same singleton state.

## Findings

### CR-01 Critical reference_tap_shutdown racy defensive guard + double-shutdown UB
**File:** `src/reference_tap.c`
**Line:** 79–101
**Issue:** The early-return guard at line 82 reads `ref_ring` and `ref_downmix_scratch` without holding `ref_mutex`. If `reference_tap_shutdown` is called twice concurrently (or after `obs_module_load` returned `false` and OBS still invokes `obs_module_unload`), the second call may attempt to `pthread_mutex_lock` an already-destroyed mutex — undefined behaviour. Furthermore, the two fields are read independently; one could observe a stale non-NULL value while the other is already NULL, causing the function to proceed into an inconsistent teardown.
**Recommendation:** Remove the racy pre-check. Always lock the mutex, then check `if (ref_ring == NULL && ref_downmix_scratch == NULL)` inside the critical section and return early. Alternatively, track a separate `bool ref_initialized` atomic that is set only after `pthread_mutex_init` succeeds.

### CR-02 Critical reference_tap_get_ring returns dangling pointer with no lifetime guarantee
**File:** `src/reference_tap.c`
**Line:** 142–148
**Issue:** `reference_tap_get_ring` returns a raw `av_sync_ring_t *` and immediately releases the mutex. The header documents this as the API for the "analysis thread". Once that thread exists, it will hold the pointer across multiple read operations. If `reference_tap_shutdown` (or a concurrent `reference_tap_set_source` that swaps rings) runs while the analysis thread is reading, the ring is destroyed and the analysis thread dereferences freed memory.
**Recommendation:** Return a reference-counted handle, or at minimum guarantee the ring outlives all consumers. The simplest fix is to never destroy `ref_ring` except in `reference_tap_shutdown`, and make `set_source` only swap the *source* (callback attach/detach) while keeping the same ring. If the ring must be recreated on sample-rate changes, introduce an explicit `reference_tap_release_ring` pairing function so the analysis thread can pin the ring while in use.

### WR-01 Warning reference_tap_init ignores allocation failures
**File:** `src/reference_tap.c`
**Line:** 62–77
**Issue:** `av_sync_ring_create` and `bzalloc` can fail and return NULL. `reference_tap_init` does not check either return value and unconditionally returns `true`. If allocation fails, the audio callback will dereference NULL (the downmix scratch write at line 56 is unguarded).
**Recommendation:** Check `ref_ring != NULL && ref_downmix_scratch != NULL` before returning `true`. Return `false` on failure and let `obs_module_load` abort plugin load.

### WR-02 Warning Multiple filters fight over global reference source
**File:** `src/av_sync_filter.c`
**Line:** 168–201
**Issue:** Every filter instance independently calls `reference_tap_set_source(data->reference_source_name)` in its `update` handler. Because the reference tap is a global singleton, the last filter to have its settings changed wins, silently overriding the reference source for all other filters. A user with three cameras will see the reference flip unpredictably as they edit any filter's properties.
**Recommendation:** Either (a) move reference source selection to a global plugin setting (not per-filter), or (b) document this behaviour prominently and log at `LOG_INFO` whenever the global reference is overridden by a different filter instance.

### WR-03 Warning reference_audio_callback silently drops oversized chunks
**File:** `src/reference_tap.c`
**Line:** 37–39
**Issue:** When an audio chunk exceeds `ref_downmix_capacity`, the callback returns silently. The per-filter callback in `av_sync_filter.c` counts and logs these skips in its rollup telemetry. The reference tap has no equivalent visibility, so reference-side data loss goes completely unnoticed.
**Recommendation:** Add a static `_Atomic uint64_t ref_oversize_skips` and increment it here. Expose a getter so the analysis thread or UI can report reference-side skips alongside filter-side skips.

### WR-04 Warning av_sync_filter_create warns about missing reference but still applies it
**File:** `src/av_sync_filter.c`
**Line:** 103–112
**Issue:** The startup validation gets the source by name, logs a warning if missing, releases the temporary reference, and then falls through. The preceding `av_sync_filter_update` call has already invoked `reference_tap_set_source(data->reference_source_name)`, so the tap logs its own warning too. This produces duplicate warnings for the same condition.
**Recommendation:** Remove the redundant validation block; `reference_tap_set_source` already logs when a source is not found (line 129 of `reference_tap.c`). Alternatively, skip calling `reference_tap_set_source` inside `av_sync_filter_update` when the source is missing, so the filter-level warning is the only one.

### IN-01 Info reference_tap_get_ring holds mutex unnecessarily
**File:** `src/reference_tap.c`
**Line:** 144–146
**Issue:** `ref_ring` is created once in `init` and destroyed once in `shutdown`. It is never modified by `set_source`. Locking a mutex to read a pointer that is effectively immutable during normal operation adds unnecessary synchronization overhead.
**Recommendation:** Use an `_Atomic(av_sync_ring_t *)` for `ref_ring`, or simply return it without locking. If the ring pointer must remain behind the mutex for future expansion, document why.

### IN-02 Info Missing const-correctness on reference_tap_get_ring
**File:** `src/reference_tap.h`
**Line:** 31
**Issue:** The comment states the caller must NOT free the ring, but the return type is a mutable `av_sync_ring_t *`. A `const` return type would make the contract self-enforcing at compile time.
**Recommendation:** Change the signature to `const av_sync_ring_t *reference_tap_get_ring(void);` and adjust the implementation accordingly.

### IN-03 Info Implicit w32-pthreads dependency on Windows
**File:** `src/reference_tap.c`
**Line:** 14
**Issue:** The code relies on `<pthread.h>` being available on Windows. OBS's dependency bundle provides this via `w32-pthreads`, but `CMakeLists.txt` does not declare the dependency explicitly (`find_package(Threads)` or `find_package(w32-pthreads)`). Builds against a minimal OBS SDK that lacks the compatibility headers would fail.
**Recommendation:** Add `find_package(Threads REQUIRED)` to `CMakeLists.txt` and link `Threads::Threads`. On Windows with OBS's build system this resolves to `w32-pthreads` automatically.

### IN-04 Info ENABLE_FRONTEND_API turned ON without Phase 5 requiring it
**File:** `CMakeLists.txt`
**Line:** 7
**Issue:** Phase 5 functionality (`obs_enum_sources`, `obs_properties_add_list`, etc.) lives entirely in `libobs`. The frontend API is not needed until a Qt dock or settings panel is built. Enabling it early pulls in an extra dependency that can break builds on systems where `obs-frontend-api` is not installed.
**Recommendation:** Keep `ENABLE_FRONTEND_API` OFF until a phase actually needs it, or add a code comment explaining why it is enabled preemptively.

### IN-05 Info reference_tap_init return value is always true
**File:** `src/reference_tap.c`
**Line:** 62, 76
**Issue:** The function returns `true` unconditionally. The caller in `plugin-main.c` checks the return value and aborts load on `false`, but that path can never be taken.
**Recommendation:** If WR-01 is fixed (checking allocations), this issue resolves itself. Until then, the dead error-handling path is misleading.

## Files Reviewed
- `src/reference_tap.h`
- `src/reference_tap.c`
- `src/av_sync_filter.c`
- `src/plugin-main.c`
- `CMakeLists.txt`
- `data/locale/en-US.ini`

## Positive Observations
- **Lock-free audio callback:** `reference_audio_callback` performs no heap allocation and acquires no mutexes, satisfying the real-time constraints documented in `CLAUDE.md`.
- **Balanced ref counting:** Every `obs_get_source_by_name` is paired with either `obs_source_release` (error paths) or ownership transfer into `ref_source` (success path), with matching `obs_source_remove_audio_capture_callback` + `obs_source_release` in detach paths.
- **Clean header contract:** `reference_tap.h` clearly documents init/shutdown pairing, thread-safety of `set_source`, and the non-freeing contract of `get_ring`.
