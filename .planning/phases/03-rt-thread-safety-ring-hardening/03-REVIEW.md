---
status: issues_found
files_reviewed: 9
critical: 1
warning: 3
info: 3
total: 7
---

# Phase 3 Code Review

## Summary
The SPSC ring buffer core is architecturally sound—release/acquire ordering on `total_written` is correct, and the filter pre-allocates its downmix scratch so the audio callback stays allocation-free. However, the `test_ring_spsc.c` stress test has a structural flaw that makes it likely to hang or fail on any machine where the writer thread outpaces the main thread. Additionally, the ring's timestamp fields are non-atomic, creating a latent data-race if `av_sync_ring_get_stats` is ever called from a non-producer thread, and the filter's oversize-skip telemetry spams the log every rollup interval once the first skip occurs.

## Findings

### CR-01 Critical Test hangs due to cursor-init race and lap accounting
**File:** `tests/test_ring_spsc.c`
**Line:** 66–102
**Issue:** The reader cursor is initialized *after* the writer thread is created (line 71 before line 74). If the writer writes more than `CAPACITY` (4800) samples before `av_sync_ring_cursor_init` runs, `cursor->pos` is initialized to a non-zero value (`tw - capacity`). Because `total_read` only counts samples actually copied—not samples lost to lap jumps—it can never reach `expected` (480 000). Once the writer finishes, `av_sync_ring_read` returns 0, the loop spins forever on `Sleep(0)`/`sched_yield()`, and the test hangs. The same hang occurs if the reader is lapped at any point during the run.
**Recommendation:** Initialize the cursor **before** starting the writer, or use a thread barrier so both start together. Change the pass condition from `total_read == expected` to `cursor.pos == expected` (which proves the consumer observed every sample position). To also verify data integrity, encode the absolute sample index into each float value and assert that every sample returned by `av_sync_ring_read` matches `cursor.pos_before_read + i`.

### WR-01 Warning Non-atomic timestamp fields create data-race UB
**File:** `src/ring_buffer.c`
**Line:** 33–34
**Issue:** `newest_timestamp_ns` and `oldest_timestamp_ns` are plain `uint64_t`, not atomic. `av_sync_ring_get_stats` reads them (line 119–120) and is callable from any thread. If a non-producer thread calls it concurrently with the audio callback, the C11 memory model defines this as a data race (undefined behavior). On 32-bit platforms the read can also tear.
**Recommendation:** Change both fields to `_Atomic uint64_t` and use `atomic_load_explicit(..., memory_order_relaxed)` / `atomic_store_explicit(..., memory_order_relaxed)` in `get_stats` and `write`.

### WR-02 Warning Oversize skip counter never resets, causing log spam
**File:** `src/av_sync_filter.c`
**Line:** 154–159
**Issue:** `data->oversize_skips` is incremented when a chunk exceeds `downmix_capacity` but is never cleared. The rollup gate (every 5 s) logs a `LOG_WARNING` whenever `oversize_skips > 0`, so a single oversize event produces a warning every 5 seconds for the lifetime of the filter.
**Recommendation:** Reset `data->oversize_skips = 0` immediately after logging it inside the rollup block (around line 173).

### WR-03 Warning SPSC test does not verify sample values
**File:** `tests/test_ring_spsc.c`
**Line:** 76–90
**Issue:** The test only checks that the expected number of samples were processed (`stats.total_written == expected`). It never inspects the contents of `out[]`. A corrupted ring (e.g., returning zeros, stale data, or mis-aligned wraps) would pass.
**Recommendation:** Write monotonically increasing float values in the producer and assert in the consumer that `out[i] == expected_absolute_index`. This catches wrap-around arithmetic errors and data races that TSan alone might miss if the race happens to produce a "correct" value by chance.

### IN-01 Info Timestamp underflow on non-monotonic OBS timestamps
**File:** `src/av_sync_filter.c`
**Line:** 116
**Issue:** `const uint64_t gap = ts - data->prev_timestamp_ns;` assumes OBS timestamps are strictly monotonic. If a source resets or OBS re-initializes its timestamp base, `ts` can be smaller than `prev_timestamp_ns`, causing unsigned wrap-around and a bogus `window_max_gap_ns`.
**Recommendation:** Guard the subtraction: `if (ts > data->prev_timestamp_ns) { gap = ts - data->prev_timestamp_ns; ... }`.

### IN-02 Info Stats path reads timestamps without synchronization
**File:** `src/ring_buffer.c`
**Line:** 113, 119–120
**Issue:** `av_sync_ring_get_stats` uses `memory_order_relaxed` for `total_written` and then immediately reads `newest_timestamp_ns` / `oldest_timestamp_ns`. On weakly-ordered architectures (ARM64) the timestamp loads may be reordered and observe stale values, producing an inconsistent snapshot.
**Recommendation:** If timestamps are made atomic per WR-01, pair the `total_written` load with an `atomic_thread_fence(memory_order_acquire)` or simply use `memory_order_acquire` for the `total_written` load in `get_stats`.

### IN-03 Info Test gives writer an uncontrolled head start
**File:** `tests/test_ring_spsc.c`
**Line:** 71–74
**Issue:** Because `thread_create` precedes `av_sync_ring_cursor_init`, the amount of writer progress at cursor initialization is non-deterministic. This makes the test flaky even if the hang is fixed.
**Recommendation:** Initialize the cursor before creating the thread, or use a `pthread_barrier_t` / Windows event to synchronize the start of both threads.

## Files Reviewed
- `src/av_sync_filter.c`
- `src/ring_buffer.h`
- `src/ring_buffer.c`
- `CMakeLists.txt`
- `tests/obs_shim.h`
- `tests/test_ring_spsc.c`
- `tests/CMakeLists.txt`
- `tests/util/bmem.h`
- `.gitignore`
