# Phase 3: RT-Thread Safety & Ring Hardening - Context

**Gathered:** 2026-05-03
**Status:** Ready for planning

<domain>
## Phase Boundary

Eliminate the heap-allocation bug on the OBS real-time audio thread and upgrade the ring buffer to a correct SPSC (single-producer, single-consumer) design with a window-read API. All downstream DSP work (Phase 4+) depends on this foundation being correct.

**In scope:**
- Move `av_sync_ring_create` call from `av_sync_filter_audio` to `av_sync_filter_create`
- Move downmix scratch buffer from stack (2048-frame compile-time constant) to heap, allocated at filter create time
- Add C11 `_Atomic size_t` write cursor to ring buffer struct; apply acquire/release ordering
- Add `oldest_timestamp_ns` field to ring buffer struct (maintained on write, O(1))
- Add `av_sync_ring_read` window-copy API with caller-owned consumer cursor
- Add rate-limited oversize-chunk warning log

**Out of scope:**
- Any DSP or offset-measurement logic (Phase 4)
- Reference tap / multiple-ring coordination (Phase 5)
- Filter property UI (Phase 5)
</domain>

<decisions>
## Implementation Decisions

### Atomics strategy
- **D-01:** Ring buffer stays in **C** (`ring_buffer.c`). Use C11 `_Atomic size_t` (`<stdatomic.h>`) for the write cursor — no file rename to `.cpp`, no `extern "C"` boundary. C11 atomics are supported on all three target platforms (MSVC `/std:c11`, GCC, Clang).
- **D-02:** Write cursor uses `atomic_store_explicit(..., memory_order_release)` on the producer (audio thread) and `atomic_load_explicit(..., memory_order_acquire)` on the consumer (analysis thread). This is the minimum correct ordering for SPSC.

### Read API design
- **D-03:** `av_sync_ring_read` takes a **caller-owned consumer cursor** (an opaque struct passed by pointer). The ring does not own any consumer state — this keeps the ring struct single-producer-safe and allows the caller (analysis thread) to own its read position without touching the ring struct from the write side.
- **D-04:** Proposed signature:
  ```c
  typedef struct { size_t pos; } av_sync_ring_cursor_t;

  /* Initialize cursor to "start reading from oldest available sample". */
  void av_sync_ring_cursor_init(const av_sync_ring_t *ring, av_sync_ring_cursor_t *cursor);

  /* Copy up to `n` samples into `out`, advancing cursor. Returns samples actually copied.
     Skips samples that were overwritten since the cursor was last updated. */
  size_t av_sync_ring_read(const av_sync_ring_t *ring, av_sync_ring_cursor_t *cursor,
                           float *out, size_t n);
  ```
- **D-05:** The read function is safe to call from any thread while the audio thread writes. No lock needed — correctness comes from the atomic write cursor and acquire/release ordering.

### Downmix buffer
- **D-06:** Replace the 2048-frame stack scratch with a **heap-allocated per-filter downmix buffer**, sized at filter create time. Size = `sample_rate * AV_SYNC_MAX_CHUNK_S` (a compile-time constant, e.g. 1 second = ~48000 samples at 48 kHz). This eliminates the silent data-loss path. The `oversize_skips` counter and warning log are kept as a defense-in-depth diagnostic.
- **D-07:** The downmix buffer is owned by `av_sync_filter_data` and freed in `av_sync_filter_destroy`. It is only written from the audio callback (single writer). No atomics needed on the downmix buffer itself.

### Timestamp tracking
- **D-08:** Add `oldest_timestamp_ns` directly to the `av_sync_ring` struct. Maintained on every `av_sync_ring_write` call: set to the incoming `timestamp_ns` when the ring transitions from empty to non-empty; updated to `newest_timestamp_ns - span_of_capacity_ns` once the ring is full. This makes Phase 4's window-time queries O(1) with no division on the read path.
- **D-09:** `get_stats` continues to work but reads `oldest_timestamp_ns` directly from the struct rather than recomputing it.

### Allocation timing
- **D-10:** Both ring allocation and downmix buffer allocation move to `av_sync_filter_create`. Use `obs_get_audio_info()` at create time for sample rate; fall back to 48000 Hz if it returns false (matches existing pattern). The `callback_count == 1` branch in `av_sync_filter_audio` that currently allocates the ring is removed.

### Claude's Discretion
- Exact size constant for `AV_SYNC_MAX_CHUNK_S` (1 second is safe; could be 0.5 s)
- Whether to rename `sample_rate` field from `uint32_t` to be initialized at create time vs. first callback (preference: at create time, from `obs_get_audio_info`)
- Test strategy for SPSC correctness (ThreadSanitizer preferred per roadmap success criteria; TSan is available on Linux/macOS CI; on Windows use code inspection + Helgrind-equivalent)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### OBS API
- `docs/ARCHITECTURE.md` — plugin data flow and OBS API touchpoints
- OBS plugin API: https://docs.obsproject.com/ — `obs_get_audio_info`, `obs_source_add_audio_capture_callback`, `obs_source_set_sync_offset` (nanoseconds)

### Existing source files (read before planning)
- `src/ring_buffer.h` — current public API (to be extended)
- `src/ring_buffer.c` — current implementation (write cursor is plain `uint64_t total_written`, no atomics)
- `src/av_sync_filter.c` — filter lifecycle; line 105 is the bug: `av_sync_ring_create` called inside `av_sync_filter_audio`

### Roadmap
- `.planning/ROADMAP.md` §Phase 3 — four specific plans and four success criteria (canonical scope)

### Language / platform constraints
- `CLAUDE.md` — language rules (C entry point, C++ allowed for DSP), no Win32-only APIs, GPL-2.0-or-later
- C11 `<stdatomic.h>` — MSVC support requires `/std:c11` or `/std:c17` (available since VS 2019 16.8)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `av_sync_ring_t` struct: `float *samples`, `size_t capacity`, `uint32_t sample_rate`, `uint64_t total_written`, `uint64_t newest_timestamp_ns` — extend, don't replace
- `bzalloc` / `bfree` from `<util/bmem.h>` — OBS heap allocator, already in use for ring and filter data
- `obs_get_audio_info()` call pattern already in `av_sync_filter_audio` callback_count==1 branch — move to `av_sync_filter_create`

### Established Patterns
- OBS `obs_log(LOG_INFO, ...)` / `obs_log(LOG_WARNING, ...)` for all logging
- Rate-limited logging pattern already established via `AV_SYNC_DIAG_LOG_INTERVAL_NS` (5 s window)
- Guard clauses (`if (!r || ...) return;`) on all public ring functions
- `UNUSED_PARAMETER` macro for OBS callback params that aren't used

### Integration Points
- `av_sync_filter_create` — add ring + downmix buffer allocation here
- `av_sync_filter_destroy` — add downmix buffer free (ring free already present)
- `av_sync_filter_audio` — remove lazy-init branch; downmix writes to heap buffer instead of stack
- `ring_buffer.h` — add `av_sync_ring_cursor_t` typedef and `av_sync_ring_read` / `av_sync_ring_cursor_init` declarations

</code_context>

<specifics>
## Specific Ideas

- The SPSC design is writer = OBS audio thread, reader = Phase 4 analysis thread (one reader per filter instance). The caller-owned cursor model is chosen explicitly to avoid constraining this to a single fixed consumer in the ring struct.
- Rate-limited oversize warning: reuse the existing `AV_SYNC_DIAG_LOG_INTERVAL_NS` throttle pattern — log at most once per 5 s window when `oversize_skips` increments.
- Phase 4 will read windows of 2–4 s; the ring capacity of 10 s (established in Phase 2) provides adequate headroom. No capacity change needed in Phase 3.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 03-rt-thread-safety-ring-hardening*
*Context gathered: 2026-05-03*
