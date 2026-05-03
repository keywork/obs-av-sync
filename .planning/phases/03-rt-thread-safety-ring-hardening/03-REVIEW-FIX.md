---
status: all_fixed
fixes: 7
commits: 3
---

# Phase 3 Review Fix Log

All findings from `03-REVIEW.md` have been addressed.

## Fixes Applied

### CR-01 — Test hangs due to cursor-init race and lap accounting
**File:** `tests/test_ring_spsc.c`  
**Commit:** `7e90739`  
**Fix:** Moved `av_sync_ring_cursor_init` to *before* `thread_create`, eliminating the race where the writer could lap the ring before the cursor was initialized. Changed primary pass condition from `total_read == expected` to `cursor.pos == expected`.

### WR-01 — Non-atomic timestamp fields create data-race UB
**File:** `src/ring_buffer.c`  
**Commit:** `4549114`  
**Fix:** Changed `newest_timestamp_ns` and `oldest_timestamp_ns` from plain `uint64_t` to `_Atomic uint64_t`. Updated `av_sync_ring_write` to use `atomic_store_explicit(..., memory_order_relaxed)` and `av_sync_ring_get_stats` to use `atomic_load_explicit(..., memory_order_relaxed)`.

### WR-02 — Oversize skip counter never resets, causing log spam
**File:** `src/av_sync_filter.c`  
**Commit:** `9080ef9`  
**Fix:** Added `data->oversize_skips = 0` after the info rollup log (inside the 5s gate block), so the warning fires only once per interval.

### WR-03 — SPSC test does not verify sample values
**File:** `tests/test_ring_spsc.c`  
**Commit:** `7e90739`  
**Fix:** Added sample value verification loop: tracks `verify_pos = cursor.pos`, then after each `av_sync_ring_read` asserts `out[i] == (float)(verify_pos + i)` for every returned sample. Catches wrap-around arithmetic errors and data races.

### IN-01 — Timestamp underflow on non-monotonic OBS timestamps
**File:** `src/av_sync_filter.c`  
**Commit:** `9080ef9`  
**Fix:** Guarded `ts - data->prev_timestamp_ns` with `if (ts > data->prev_timestamp_ns)` to prevent unsigned wrap-around.

### IN-02 — Stats path reads timestamps without synchronization
**File:** `src/ring_buffer.c`  
**Commit:** `4549114`  
**Fix:** Changed `total_written` load in `get_stats` from `memory_order_relaxed` to `memory_order_acquire`, pairing with the producer's release store for a consistent snapshot.

### IN-03 — Test gives writer an uncontrolled head start
**File:** `tests/test_ring_spsc.c`  
**Commit:** `7e90739`  
**Fix:** Same as CR-01 — cursor initialization now precedes thread creation, giving both threads a deterministic starting point.

## Verification

- **Build:** PASS (zero errors, `RelWithDebInfo`)
- **Test run:** PASS (`spsc_round_trip` — 480,000 samples read, 480,000 written, all values verified)
- **Commits:** 3 fix commits on `main`
