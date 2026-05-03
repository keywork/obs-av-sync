# Architecture

## Overall Pattern

`obs-av-sync` is an OBS Studio plugin that registers a per-source **audio filter** (`OBS_SOURCE_TYPE_FILTER | OBS_SOURCE_AUDIO`). The filter intercepts every audio callback for its parent source, buffers PCM samples in a mono float32 ring, and (in later phases) cross-correlates those samples against a reference source to derive a nanosecond offset that is applied back to OBS via `obs_source_set_sync_offset()`.

The module entry point lives in C (`plugin-main.c`) to remain compatible with `OBS_DECLARE_MODULE`. All per-source logic is in a separate translation unit (`av_sync_filter.c`) that can be converted to C++ without touching the entry point.

---

## Core Components

### `plugin-main.c` — Module entry point
- Declares the OBS module with `OBS_DECLARE_MODULE()` and `OBS_MODULE_USE_DEFAULT_LOCALE`.
- `obs_module_load()` calls `av_sync_register_filter()` then logs a success banner.
- `obs_module_unload()` logs shutdown; no teardown needed yet (filter instances clean up themselves).
- Kept as plain C; must not be converted to C++ (OBS macro compatibility).

### `av_sync_filter.c` / `av_sync_filter.h` — Per-source audio filter
- Registers `obs_source_info` struct with ID `"obs_av_sync_filter"`, type `OBS_SOURCE_TYPE_FILTER`, output flag `OBS_SOURCE_AUDIO`.
- Public surface: single function `av_sync_register_filter(void)` (declared in the header with C linkage guards for future C++ consumers).
- **Per-instance state** (`struct av_sync_filter_data`):
  - `obs_source_t *source` — back-pointer to the filter's own source handle.
  - `uint64_t callback_count` — monotonically incrementing call counter.
  - `uint64_t total_frames` — cumulative PCM frame count across all callbacks.
  - `uint64_t first_timestamp_ns` — OBS timestamp of the very first audio chunk.
  - `uint64_t prev_timestamp_ns` — timestamp of the previous chunk (gap detection).
  - `uint64_t window_start_ns` — start of the current diagnostic rollup window.
  - `uint64_t window_max_gap_ns` — maximum inter-callback gap within the rollup window.
  - `uint32_t sample_rate` — captured from `obs_get_audio_info()` on first callback.
  - `av_sync_ring_t *ring` — per-filter mono ring buffer, lazily allocated on first callback.
  - `uint64_t oversize_skips` — count of chunks that exceeded the 2048-frame downmix scratch limit.

### `ring_buffer.c` / `ring_buffer.h` — Mono float32 circular buffer
- Opaque type `av_sync_ring_t` (struct defined only in `.c`).
- **Internal struct fields** (`struct av_sync_ring`):
  - `float *samples` — heap-allocated sample array of `capacity` floats.
  - `size_t capacity` — total sample slots (currently `sample_rate * 10` seconds).
  - `uint32_t sample_rate` — stored for timestamp arithmetic.
  - `uint64_t total_written` — monotonic write counter; `total_written % capacity` gives current write head.
  - `uint64_t newest_timestamp_ns` — OBS-side timestamp of the last sample written.
- **API**:
  - `av_sync_ring_create(capacity_samples, sample_rate) → av_sync_ring_t *`
  - `av_sync_ring_destroy(ring)` — frees both sample buffer and struct.
  - `av_sync_ring_write(ring, samples, n, timestamp_ns)` — single-producer append; overwrites oldest on wrap; handles n ≥ capacity by taking only the last `capacity` samples.
  - `av_sync_ring_get_stats(ring, out)` — fills `av_sync_ring_stats_t` with capacity, fill level, total written, oldest/newest timestamps.
- **Stats struct** (`av_sync_ring_stats_t`): `capacity`, `filled`, `total_written`, `oldest_timestamp_ns`, `newest_timestamp_ns`, `sample_rate`.

### `plugin-support.h` — Logging shim
- Declares `PLUGIN_NAME`, `PLUGIN_VERSION` (extern strings populated by the template's CMake machinery).
- Wraps OBS's `blogva()` into a `obs_log(int level, const char *fmt, ...)` helper.

---

## Data Flow

```
OBS audio thread
      │
      │  struct obs_audio_data  (multi-channel float32, ~480 frames @ 48 kHz)
      ▼
av_sync_filter_audio()
      │
      ├── guard: skip empty callbacks
      ├── first callback: query sample_rate, alloc ring (sample_rate × 10 s)
      ├── gap tracking: prev_timestamp_ns → window_max_gap_ns
      │
      ├── downmix: sum all planes → mono scratch[2048] × (1/planes)
      │   (skip write if frames > 2048, increment oversize_skips)
      │
      ├── av_sync_ring_write(ring, scratch, frames, timestamp_ns)
      │
      ├── diagnostic logging (first 5 callbacks verbosely; every 5 s as rollup)
      │
      └── return audio unchanged  (pure pass-through to OBS mixer)

[Phase 3+]
Analysis thread (per filter, ~2 Hz)
      │
      ├── read window from filter's ring + reference ring
      ├── gcc_phat(ref_samples, target_samples, sample_rate) → (offset_ns, confidence)
      ├── smoother: reject low-confidence, exponential decay, slew-rate limit
      └── obs_source_set_sync_offset(parent_source, offset_ns)
```

All audio callbacks return `audio` unchanged — the filter is a pure pass-through at the OBS mixer level. The sync offset is applied as OBS's built-in per-source delay knob, not by manipulating the PCM data itself.

---

## State Management

State is entirely **per-instance** — no global singletons exist yet. Each filter instance carries its own `struct av_sync_filter_data` allocated by `bzalloc` in `av_sync_filter_create` and freed by `av_sync_filter_destroy`.

The ring buffer is allocated lazily in the **first audio callback** (not in `create`) because `obs_get_audio_info()` requires the OBS audio subsystem to be fully running; calling it at create time can return zero.

Planned additions per `docs/ARCHITECTURE.md`:
- A **shared reference ring** owned by a `reference_tap` singleton, read by all filter instances.
- Per-filter analysis thread handle.
- Atomic settings swap so the OBS UI thread can update configuration without data races.

---

## Signal Processing Design

### Currently implemented (Phases 1–2)
- Multi-channel → mono downmix: `sum(planes) / planes` into a stack-allocated 2048-float scratch.
- Ring buffer write with wrap-around; `total_written % capacity` as write head.
- Timestamp tagging: `newest_timestamp_ns = chunk_start_ns + (n-1) / sample_rate * 1e9`.
- No analysis, no offset application.

### Planned (Phases 3–4, per `docs/ARCHITECTURE.md` and `docs/ROADMAP.md`)
- **GCC-PHAT** (`src/gcc_phat.{cpp,h}`): pure function `estimate_offset(ref, target, rate) → (offset_ns, confidence)`.
  - Pipeline: Hann window → FFT (PFFFT, BSD-licensed) → cross-spectrum → PHAT weighting → IFFT → peak pick → parabolic sub-sample interpolation.
  - Confidence = peak-to-sidelobe ratio.
- **Smoother** (`src/smoother.{cpp,h}`): exponential weighting, low-confidence rejection, slew-rate limiting.
- **Analysis thread**: fires ~2 Hz, uses 2–4 s overlapping windows (50% overlap).
- Target sample rate for analysis: **16 kHz mono** (~63 µs per-sample resolution, cheap FFTs).
- Target accuracy: < 1 ms error at SNR ≥ 10 dB (Phase 3 exit criterion); < 20 ms residual in live sessions (Phase 4).

---

## Threading Model

### Current (Phases 1–2)
Single-threaded from the plugin's perspective. The `filter_audio` callback runs on OBS's audio thread. The ring buffer has **no locks** — comments in `ring_buffer.c` explicitly note it is single-producer only and that cross-thread atomics are deferred to Phase 3.

Diagnostic reads of ring stats (`av_sync_ring_get_stats`) happen only from the same audio thread during rollup logging — no concurrent readers yet.

### Planned (Phase 3+)
- **OBS audio thread**: sole writer to each per-source ring. Must be non-blocking; no allocations inside callbacks (ring already allocated; downmix uses stack scratch).
- **Analysis thread (per filter)**: sole reader of its filter's ring + shared reference ring. Calls `obs_source_set_sync_offset` — OBS API is thread-safe for this call.
- **OBS UI thread**: reads/writes `obs_data_t` settings; communicates to analysis thread via atomic settings swap in the `update` callback (to be implemented).
- Ring buffer will need atomic read/write head variables (likely `_Atomic` or `std::atomic<size_t>`) before the analysis thread is introduced.
