# Architecture

## Problem statement

In multi-camera OBS productions, each camera source arrives with its own end-to-end latency (capture-card buffering, encoder, network transport, NDI jitter, etc.) that differs from the house audio mix. OBS already has a per-source `sync_offset` that can be set programmatically or via filter UI; tuning it by hand per show per camera is tedious. This plugin automates that tuning.

## High-level design

```
┌────────────────────────────────────────────────────────────────┐
│                          OBS Studio                            │
│                                                                │
│   Reference audio source          Camera source (N of them)    │
│         │                                │                     │
│         │  audio_data                    │  audio_data         │
│         ▼                                ▼                     │
│   ┌──────────────┐              ┌────────────────────┐         │
│   │ Ref audio    │              │ obs-av-sync filter │         │
│   │ capture cb   │              │ (per-source)       │         │
│   └──────┬───────┘              └─────────┬──────────┘         │
│          │                                │                    │
│          │ push samples                   │ push samples       │
│          ▼                                ▼                    │
│   ┌───────────────────────────────────────────────────┐        │
│   │             Shared reference ring buffer          │        │
│   │             + per-source ring buffers             │        │
│   └─────────────────────────┬─────────────────────────┘        │
│                             │                                  │
│                             ▼                                  │
│                  ┌──────────────────────┐                      │
│                  │   Analysis thread    │                      │
│                  │   (per filter)       │                      │
│                  │   GCC-PHAT → smooth  │                      │
│                  └──────────┬───────────┘                      │
│                             │ offset_ns                        │
│                             ▼                                  │
│              obs_source_set_sync_offset(src, offset_ns)        │
└────────────────────────────────────────────────────────────────┘
```

## Component responsibilities

### Plugin module (`src/plugin-main.c`)
- Entry point: `obs_module_load`/`obs_module_unload`.
- Registers source types (the AV-sync filter).
- Holds the singleton **reference registry** (which source is the current reference, ref-counted shared access).

### AV-sync filter (`src/av_sync_filter.{cpp,h}` — to be added)
- Type: `OBS_SOURCE_TYPE_FILTER | OBS_SOURCE_AUDIO`.
- Per-instance state: ring buffer, analysis thread handle, last applied offset, settings (enabled, reference source name, min/max clamp, smoothing).
- Hooks:
  - `create` / `destroy` — alloc/free state + spin up/down thread.
  - `filter_audio` — append samples to ring, return samples unchanged.
  - `get_properties` — UI schema for OBS properties dialog.
  - `update` — re-read settings into runtime state (atomic swap).

### Reference tap (`src/reference_tap.{cpp,h}`)
- One instance per scene/collection; lazily created when first filter requests a reference source.
- Uses `obs_source_add_audio_capture_callback` on the designated reference source.
- Pushes samples into a **single shared** ring (the per-filter analysis threads all read from this one buffer, indexed by timestamp).

### Ring buffer (`src/ring_buffer.{cpp,h}`)
- Mono float32, fixed capacity (target: 10 s at 16 kHz → 160 k samples → 640 KB per ring).
- Timestamp-keyed: each chunk tagged with its OBS-side `uint64_t` timestamp (ns) so analysis can align reference and target without relying on wall clock.
- Single-producer-single-consumer per ring; lock-free.

### Analysis engine (`src/gcc_phat.{cpp,h}`)
- Pure function: `estimate_offset(ref_samples, target_samples, samplerate) → (offset_ns, confidence)`.
- Implementation: Hann window → FFT (PFFFT) → cross-spectrum → PHAT weighting → IFFT → peak pick → parabolic interp.
- Confidence = peak-to-sidelobe ratio.
- Unit-testable in isolation (no OBS dependencies).

### Smoother (`src/smoother.{cpp,h}`)
- Takes a stream of (offset, confidence) measurements, outputs a smoothed offset to apply.
- Rejects low-confidence points, exponentially weights recent measurements, slew-rate-limits the output.
- Holds its own last-applied value so startup doesn't spike.

## Threading model

- **OBS audio thread**: pushes samples into rings via `filter_audio` / capture callback. Must be non-blocking — allocations and long work forbidden here.
- **Analysis thread (per filter)**: wakes at ~2 Hz, drains enough samples for a 2–4 s window, runs GCC-PHAT, updates smoother, calls `obs_source_set_sync_offset`.
- **OBS UI thread**: reads filter properties; must not mutate analysis-thread state directly. Use `obs_data_t` + `update` callback → atomic settings swap.

## Data flow invariants

1. Ring buffer timestamps are monotonic per source. Gaps (stream restart) discard pending analysis window.
2. Reference and target windows used for a single GCC-PHAT call must overlap in timestamp space, not wall-clock. Misalignment → discard window, don't guess.
3. Applied `sync_offset` only ever changes on the analysis thread; UI reads it atomically.
4. Settings updates are latched at the start of each analysis iteration — no mid-iteration reconfiguration.

## OBS API touchpoints

| API | Purpose |
|---|---|
| `obs_register_source` | Register filter type |
| `obs_source_add_audio_capture_callback` | Tap reference audio without being in the filter chain |
| `obs_source_set_sync_offset` / `obs_source_get_sync_offset` | Apply/read per-source AV offset (nanoseconds) |
| `obs_source_enum_sources` | Populate reference-source dropdown |
| `obs_properties_add_list` | UI for reference selection |
| `obs_frontend_add_save_callback` | Persist settings across scene saves (if needed beyond `obs_data`) |

## Open questions (to resolve during implementation)

- **Sample rate**: downmix to 16 kHz always, or match source rate? Leaning 16 kHz mono for analysis — good trade-off for FFT cost vs. timing resolution (~63 µs per sample).
- **FFT window size**: 2 s vs 4 s. Longer = more stable but slower reaction to source changes. Likely start at 2 s.
- **Reference ambiguity**: if user picks a reference that is itself downstream of other sources (e.g. a scene audio), we may be chasing circular dependencies. Detect + warn.
- **Multi-channel reference**: stereo reference should be downmixed to mono for correlation; confirm no stereo image artifacts harm the estimate.
- **Scene changes**: when the camera's source goes inactive, we should hold the last offset rather than drift to zero.
