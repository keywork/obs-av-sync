# Phase 6: Continuous Sync Engine - Context

**Gathered:** 2026-05-03
**Status:** Ready for planning

## Phase Boundary

Offset measurements run continuously on a background thread, are smoothed and slew-rate-limited, and are applied automatically to each camera source — no operator action required during a live show.

Requirements: SYNC-02, SYNC-03, DRIFT-01

## Implementation Decisions

### Analysis Thread Architecture
- **D-01:** One dedicated pthread per filter instance. Spawned in `av_sync_filter_create`, joined in `av_sync_filter_destroy`. Simpler lifecycle management than a global thread pool, no shared queue complexity.
- **D-02:** Thread function signature: `static void *av_sync_analysis_thread(void *arg)` where `arg` is `struct av_sync_filter_data *`.
- **D-03:** Thread runs a loop with `sleep`/`nanosleep` (~500 ms between iterations = ~2 Hz effective rate). On each iteration, reads from both the per-filter ring and the shared reference ring, calls `estimate_offset`, applies the smoother, and calls `obs_source_set_sync_offset`.

### Analysis Cadence and Window Size
- **D-04:** 4-second analysis window (192,000 samples at 48 kHz). This gives strong GCC-PHAT correlation at ≥10 dB SNR.
- **D-05:** ~2 Hz update rate (500 ms sleep between iterations). The thread sleeps 500 ms, then reads the most recent 4 seconds from each ring. Overlap is implicit because rings are continuously written.
- **D-06:** If either ring has fewer than 4 seconds of data, skip this iteration and sleep again (warm-up phase).

### Smoother Algorithm
- **D-07:** Exponential Moving Average (EMA) with confidence gating and slew-rate cap.
  - EMA formula: `smoothed = alpha * raw + (1 - alpha) * smoothed`, where `alpha = 0.3` (configurable).
  - Confidence gate: reject measurements where `confidence < 2.0f`. Below this threshold, hold the last valid smoothed value.
  - Slew-rate cap: max change per update = `20.0f` ms. If raw delta exceeds this, clamp to ±20 ms.
- **D-08:** When reference or source is silent (no data in ring), hold the last valid smoothed offset. Do NOT reset to zero.
- **D-09:** Smoother state lives in `struct av_sync_filter_data`: `float smoothed_offset_ms`, `float last_confidence`, `uint32_t valid_measurement_count`, `bool has_valid_offset`.

### Confidence Threshold and Status States
- **D-10:** Three status states exposed via an `_Atomic int` field:
  - `0 = AV_SYNC_STATUS_MEASURING` — fewer than 3 valid measurements accumulated
  - `1 = AV_SYNC_STATUS_SYNCED` — ≥3 valid measurements and residual offset ≤ 20 ms
  - `2 = AV_SYNC_STATUS_OUT_OF_RANGE` — confidence below threshold OR |offset| > 500 ms
- **D-11:** Status transitions:
  - Start → Measuring
  - Measuring + 3 valid measurements + |offset| ≤ 20 ms → Synced
  - Synced + confidence < 2.0 OR |offset| > 500 ms → Out of Range
  - Out of Range + confidence ≥ 2.0 AND |offset| ≤ 500 ms → Synced (not back to Measuring)

### Offset Application
- **D-12:** The analysis thread calls `obs_source_set_sync_offset(source, offset_ns)` directly. OBS API is thread-safe for this function. No main-thread marshaling needed.
- **D-13:** Convert smoothed offset from milliseconds to nanoseconds: `offset_ns = (int64_t)(smoothed_offset_ms * 1.0e6f)`. Positive offset = delay video relative to audio (target lags reference).
- **D-14:** Only apply offset when `sync_enabled == true` AND `has_valid_offset == true`. If disabled or invalid, do not call `obs_source_set_sync_offset`.

### Drift Tracking
- **D-15:** The smoother accumulates corrections over the session lifetime with no periodic reset. The EMA naturally tracks slow drift.
- **D-16:** No explicit drift rate estimation. The EMA's time constant (~3 updates at α=0.3 ≈ 1.5 seconds) is fast enough to track typical IP camera drift.

### Thread Safety
- **D-17:** The analysis thread reads from the per-filter ring and the shared reference ring using `av_sync_ring_read` (SPSC-safe, acquire ordering). No locks needed for ring reads.
- **D-18:** Smoother state is single-writer (analysis thread), single-reader (UI thread in Phase 8). Use plain fields for now; Phase 8 will add atomics for status polling.
- **D-19:** Audio callback (`av_sync_filter_audio`) and analysis thread are the producer/consumer pair for the per-filter ring. Already SPSC-safe from Phase 3.

### Graceful Degradation
- **D-20:** If the reference source is removed, the analysis thread detects `reference_tap_get_ring() == NULL` and pauses applying offsets (holds last value).
- **D-21:** If the per-filter source is muted, the ring stops receiving data. The analysis thread detects insufficient data and skips iterations (holds last value).
- **D-22:** On filter destroy, signal the analysis thread to exit (using a `bool thread_running` flag), then `pthread_join` it before freeing resources.

### Claude's Discretion
- Exact nanosleep duration (500 ms vs 250 ms for 4 Hz) — 500 ms recommended for stability
- EMA alpha value — 0.3 is a reasonable default; can be made configurable in Phase 8
- Slew-rate cap value — 20 ms per update is conservative for live production
- Status transition hysteresis (e.g., require 2 consecutive bad readings before Out of Range) — not required for v1

## Canonical References

### Requirements
- `.planning/REQUIREMENTS.md` §SYNC-02, SYNC-03, DRIFT-01 — Continuous sync and drift requirements
- `.planning/ROADMAP.md` §Phase 6 — Phase goal, plans, and success criteria

### Architecture
- `.planning/PROJECT.md` — Project vision and principles
- `docs/ARCHITECTURE.md` — Data flow and OBS API touchpoints

### Existing Code (MUST read before planning)
- `src/av_sync_filter.c` — Current filter implementation with per-filter state
- `src/reference_tap.h` — Reference tap API (`reference_tap_get_ring()`)
- `src/ring_buffer.h` — Ring buffer read API (`av_sync_ring_cursor_t`, `av_sync_ring_read`)
- `src/gcc_phat.h` — GCC-PHAT estimator (`estimate_offset()`)
- `src/plugin-main.c` — Module entry point

## Existing Code Insights

### Reusable Assets
- `av_sync_ring_read` / `av_sync_ring_cursor_init` — analysis thread reads both per-filter and reference rings
- `estimate_offset()` — pure function, thread-safe, stateless
- `reference_tap_get_ring()` — returns const pointer valid for plugin lifetime

### Established Patterns
- C11 atomics for ring buffer SPSC (`_Atomic size_t total_written`)
- pthread for threading (`pthread_mutex_t` in reference_tap.c)
- `obs_source_set_sync_offset` — OBS API for applying sync correction
- Per-filter `bzalloc`/`bfree` lifecycle

### Integration Points
- `av_sync_filter_create` — spawn analysis thread
- `av_sync_filter_destroy` — signal thread exit, `pthread_join`
- `av_sync_filter_audio` — producer to per-filter ring (consumer is analysis thread)
- `reference_audio_callback` — producer to reference ring (consumer is all analysis threads)

## Deferred Ideas

- Global thread pool for analysis — deferred to v2 if >4 camera performance becomes an issue
- Kalman filter for drift tracking — deferred; EMA is sufficient for v1
- Configurable EMA alpha via filter properties — deferred to Phase 8
- Per-scene reference switching — deferred to v2
