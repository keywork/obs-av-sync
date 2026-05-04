# Phase 6 Code Review Report

## Summary

Phase 6 introduces a per-filter pthread analysis thread, a global reference audio tap, and an EMA+slew-rate smoother. The overall design is sound—SPSC ring cursors avoid locks in the hot audio path, thread teardown uses an atomic flag with `pthread_join`, and the smoother correctly gates on confidence before applying offsets. However, the review identified **two critical bugs**: an erroneous `pthread_mutex_unlock` in a failure path that triggers undefined behavior, and a missing NaN/Inf guard in the smoother that can permanently poison state and cause undefined behavior when casting to `int64_t` for `obs_source_set_sync_offset`. Several thread-safety warnings were also found, including a data race on `sync_enabled`, OBS API calls from a non-OBS thread, and unsynchronized reads of shared globals.

## Findings

### Critical

- **CR-01** (`reference_tap.c:86`): `pthread_mutex_unlock(&ref_mutex)` is called in the allocation-failure path of `reference_tap_init()`, but the mutex was only initialized (via `pthread_mutex_init`) and was **never locked**. Unlocking an unlocked mutex is undefined behavior and will likely crash or corrupt mutex state on some platforms.  
  → **Fix**: Remove the `pthread_mutex_unlock(&ref_mutex)` call on line 86.

- **CR-02** (`smoother.cpp:34`, `smoother.cpp:38–40`, `av_sync_filter.c:455`): `smoother_process()` does not validate that `raw_offset_ms` or `confidence` are finite. If `estimate_offset()` returns `NaN` (e.g., from an all-zero input or internal DSP division-by-zero), the comparison `confidence < s->confidence_threshold` evaluates to **false** (accepting the measurement), and the EMA update `desired = alpha * NaN + ...` poisons `smoothed_offset_ms` with `NaN` permanently. The subsequent `int64_t` cast in `av_sync_filter.c:455`—`(int64_t)(data->smoothed_offset_ms * 1.0e6f)`—then invokes undefined behavior. Additionally, `smoother_get_status()` line 67 does `fabsf(NaN) > 500.0f`, which is false, so the module would report **Synced** while applying garbage offsets.  
  → **Fix**: Reject non-finite values in `smoother_process()`. Add `if (!isfinite(raw_offset_ms) || !isfinite(confidence)) return false;` at the top of the function. Also treat non-finite `smoothed_offset_ms` as Out of Range in `smoother_get_status()`.

### Warning

- **WR-01** (`av_sync_filter.c:76`, `av_sync_filter.c:240`, `av_sync_filter.c:379`): `data->sync_enabled` is a plain `bool` written by `av_sync_filter_update()` (called from the OBS UI/main thread) and read by `av_sync_analysis_thread()`. This is a data race. While a torn read of a `bool` is unlikely to crash on common platforms, it is undefined behavior in C11 and could be optimized unpredictably.  
  → **Fix**: Change the field to `_Atomic bool` (or `atomic_bool`) and use `atomic_load`/`atomic_store`.

- **WR-02** (`av_sync_filter.c:395–400`, `av_sync_filter.c:438–439`, `av_sync_filter.c:456–458`, `av_sync_filter.c:464–465`): The analysis thread calls OBS API functions `obs_filter_get_parent()`, `obs_source_get_name()`, and `obs_source_set_sync_offset()` from a non-OBS pthread. OBS sources are not generally thread-safe; the parent pointer can change if the filter is reordered, and `obs_source_set_sync_offset()` may race with OBS’s own source teardown or UI updates.  
  → **Fix**: Queue sync-offset application to the OBS graphics/main thread (e.g., via `obs_queue_task` if available, or by using an `obs_source_t` cached with `obs_source_get_ref`/`obs_source_release`). At minimum, assert `obs_filter_get_parent` is non-NULL and wrap the setter in a critical section or thread-safe queue.

- **WR-03** (`av_sync_filter.c:92–95`): `smoothed_offset_ms`, `last_confidence`, `valid_count`, and `has_valid_offset` are non-atomic fields that the analysis thread writes and a future Phase 8 UI thread will read. Even if not consumed today, leaving them as plain scalars invites a data race in the next milestone.  
  → **Fix**: Make these fields `_Atomic` or move them into the `_Atomic int status` encoding so the UI can read a consistent snapshot.

- **WR-04** (`reference_tap.c:172–175`, `reference_tap.c:182–185`): `reference_tap_get_ring()` and `reference_tap_get_sample_rate()` return non-atomic global variables (`ref_ring`, `ref_sample_rate`) without acquiring `ref_mutex`. During module teardown, `reference_tap_shutdown()` writes these while analysis threads may still be reading them.  
  → **Fix**: Declare `ref_ring` as `_Atomic(av_sync_ring_t *)` and `ref_sample_rate` as `_Atomic uint32_t`, or require callers to hold a read lock.

- **WR-05** (`reference_tap.c:99–126`, `reference_tap.c:128–170`): `reference_tap_shutdown()` and `reference_tap_set_source()` hold `ref_mutex` across OBS API calls (`obs_source_remove_audio_capture_callback`, `obs_source_release`, `obs_source_add_audio_capture_callback`). If OBS ever invokes a plugin callback while holding an internal lock that the plugin then tries to acquire, this is a potential deadlock vector. The risk is low because these functions are not called from OBS callbacks, but it violates the guideline of not holding application locks across external API boundaries.  
  → **Fix**: Release `ref_mutex` before calling OBS APIs, or reduce the mutex scope to only protect the assignment of `ref_source` and `ref_name`.

- **WR-06** (`av_sync_filter.c:357`): `av_sync_analysis_thread()` calls `av_sync_ring_cursor_init(data->ring, &data->src_cursor)` without checking whether `data->ring` is `NULL`. In `av_sync_filter_create()`, the return value of `av_sync_ring_create()` is not validated; if allocation fails, the analysis thread will dereference a null pointer on its first iteration. (The audio callback defensively checks `data->ring`, but the thread does not.)  
  → **Fix**: Add `if (!data->ring) { ...; return NULL; }` at the start of the thread, or validate `av_sync_ring_create()` in `av_sync_filter_create()`.

- **WR-07** (`av_sync_filter.c:123–146`): `av_sync_filter_create()` does not check the return values of `bzalloc()`, `av_sync_ring_create()`, or the analysis buffer allocations. While OBS’s `bzalloc()` typically aborts on OOM, `av_sync_ring_create()` is project code and may return `NULL`. A single failed allocation leads to a partially constructed filter that will crash in either the audio callback or the analysis thread.  
  → **Fix**: Check all allocation results and fail filter creation gracefully (`bfree(data); return NULL;`) if any required buffer cannot be allocated.

### Info

- **IN-01** (`av_sync_filter.c:98`, `av_sync_filter.c:436–437`): `data->status` is updated with `memory_order_relaxed`. This is sufficient to prevent torn reads/writes of the `int` value, but it provides no happens-before relationship with the non-atomic fields (`smoothed_offset_ms`, etc.) that Phase 8 may read alongside it. A UI thread could observe a new status but stale offset values.  
  → **Note**: When Phase 8 reads these fields, either upgrade to `memory_order_release`/`acquire` or pack all diagnostic state into a single atomic snapshot.

- **IN-02** (`smoother.cpp:64–70`): `valid_count` is a monotonically increasing lifetime counter. Once it reaches 3, `smoother_get_status()` never returns `0` (Measuring) again; it only toggles between `1` (Synced) and `2` (Out of Range). This means a source that recovers from an out-of-range state immediately shows Synced rather than re-converging through Measuring.  
  → **Note**: Confirm whether this behavior is intentional. If a re-convergence indication is desired, track consecutive valid measurements separately.

- **IN-03** (`av_sync_filter.c:145–146`): Each filter allocates two 4-second analysis scratch buffers (`analysis_ref_buf` and `analysis_src_buf`). At 48 kHz this is ~1.5 MiB per filter instance. In a 10-source production this scales to ~15 MiB, which is acceptable but should be documented in memory-constrained environments.  
  → **Note**: Consider making the window duration configurable if users need to trade memory for convergence speed.

- **IN-04** (`reference_tap.c:106`): `reference_tap_shutdown()` returns early if `!ref_ring && !ref_downmix_scratch`. In normal operation both are non-NULL until shutdown, so this is harmless. If partial state corruption ever leaves one NULL and the other non-NULL, the function would leak the surviving allocation.  
  → **Note**: Consider checking each pointer independently rather than the combined condition.

## Positive Observations

- **Clean thread lifecycle**: `av_sync_filter_destroy()` sets `_Atomic bool thread_running` to false and immediately `pthread_join`s the analysis thread before freeing any data. This eliminates use-after-free and torn-read risks during teardown.
- **SPSC ring design**: The ring buffer uses caller-owned cursors (`av_sync_ring_cursor_t`) that the audio producer never touches. This keeps the hot audio path lock-free and cache-friendly.
- **Restart detection**: The analysis thread checks `src_stats.total_written < data->last_src_total_written` to detect ring resets or stream restarts and reinitializes the consumer cursor automatically.
- **Reference tap mutex scope**: `reference_tap_set_source()` serializes global reference source changes, preventing multiple filters from racing to attach/detach the shared reference.
- **EMA + slew-rate logic**: The smoother correctly combines EMA smoothing (`alpha = 0.3`) with a per-update delta cap (`±20 ms`), satisfying the requirement to keep residual error below 20 ms once converged.
- **Audio passthrough integrity**: The filter never blocks, drops, or modifies audio frames. Ring writes and analysis are fully decoupled from the real-time audio callback.
