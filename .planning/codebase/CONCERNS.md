# Concerns & Risks

_Generated: 2026-05-03. Based on src/, docs/, CMakeLists.txt, and buildspec.json._

---

## Technical Debt

### Ring buffer is not lock-free (despite the comment saying it will be)
`ring_buffer.c` line 24–25 explicitly defers thread-safety: _"Cross-thread atomics land in Phase 3 when the analysis thread reads."_ The struct has no atomics, no mutex, and no `_Atomic` annotations anywhere. The `total_written` and `newest_timestamp_ns` fields are plain `uint64_t`. Any read from a second thread (analysis thread in Phase 3+) will produce data races under C11/C++11 memory model.

### Ring write position computed from `total_written` modulo capacity
`av_sync_ring_write` uses `r->total_written % r->capacity` to find the write position. This works correctly, but once `total_written` exceeds `UINT64_MAX` (effectively never at audio rates, but still) it wraps silently. Not a practical risk, but there is no explicit write-head cursor field — making future SPSC atomic upgrade harder than it needs to be.

### Lazy ring allocation inside the audio callback
`av_sync_filter_create` does not allocate the ring. Instead, the first call to `av_sync_filter_audio` calls `av_sync_ring_create`, which calls `bzalloc` (a heap allocation). Heap allocation from the OBS real-time audio thread violates the non-blocking constraint stated in `ARCHITECTURE.md` §Threading model. If the allocator blocks (e.g. under system memory pressure), it stalls the OBS audio thread, causing a drop.

### Diagnostic rollup window reset uses the *current* audio timestamp, not wall clock
`data->window_start_ns` is reset to the current `audio->timestamp` in the rollup branch. If `audio->timestamp` is non-monotonic (e.g. after a stream restart) the log interval could fire too often or not at all.

### No `av_sync_ring_read` function exists
`ring_buffer.h` only exposes `write` and `get_stats`. The architecture describes an analysis thread that *reads* windows of samples — there is no read API yet, no cursor management for the consumer side.

---

## Architecture Risks

### Threading: ring buffer has no SPSC protection for Phase 3+
The Phase 3 analysis thread will read from the ring while the audio thread writes to it. The current implementation has zero synchronization primitives. Introducing atomics later is not a drop-in change — the entire ring struct and write/read API will need to be redesigned with at least `_Atomic size_t` head/tail cursors, or a mutex. Retrofitting this while keeping the API stable is a medium-complexity risk.

### Reference ring buffer architecture unimplemented
`ARCHITECTURE.md` describes a **shared reference ring buffer** that all per-filter analysis threads read from. No `reference_tap.{c,cpp,h}` file exists, and `plugin-main.c` does not hold any reference registry. The current filter only taps the source it is applied to — there is no way to compare against a reference source yet.

### No reference source selection mechanism
There is no code for resolving or storing a reference source name, no `obs_source_add_audio_capture_callback` call, and no `obs_source_enum_sources` usage. The filter is purely pass-through/diagnostic.

### `obs_source_set_sync_offset` not called anywhere
The core purpose of the plugin — setting sync offsets — is entirely absent. This is expected at Phase 2, but the gap between the architectural intent and current code is very large.

### Analysis thread not spawned
No `pthread_create`, `CreateThread`, or equivalent exists in the codebase. The analysis thread (Phase 4) design in ARCHITECTURE.md is unrealized.

---

## Implementation Gaps

The following components described in `ARCHITECTURE.md` and `ROADMAP.md` are **not yet present** in `src/`:

| Component | Source file(s) planned | Status |
|---|---|---|
| Reference tap | `src/reference_tap.{cpp,h}` | Missing entirely |
| GCC-PHAT engine | `src/gcc_phat.{cpp,h}` | Missing entirely |
| Smoother / hysteresis | `src/smoother.{cpp,h}` | Missing entirely |
| Analysis thread | Inside `av_sync_filter.cpp` | Missing |
| Filter properties/UI | `get_properties`, `update` hooks | Missing |
| Reference registry (singleton in plugin-main) | `plugin-main.c` | Missing |
| `obs_source_set_sync_offset` call | `av_sync_filter.c` | Missing |
| Ring buffer read API | `ring_buffer.{c,h}` | Missing |
| FFT vendor dependency (PFFFT) | CMakeLists.txt / submodule | Missing |
| Unit tests | `tests/` directory | Missing |

Coverage: the codebase is at roughly Phase 2b completion. Phases 3–7 are entirely unimplemented.

---

## API / Compatibility Risks

### OBS pinned to 31.1.1 — API stability unclear beyond this
`buildspec.json` pins `obs-studio` at `31.1.1`. OBS does not maintain a stable plugin ABI across major versions. If OBS 32.x makes breaking changes to `struct audio_data`, `obs_source_info`, or the filter callback signature, the plugin will silently misbehave or crash. There is no documented compatibility policy in the plugin.

### `obs_source_get_sync_offset` / `obs_source_set_sync_offset` behaviour not validated
These are the core OBS APIs for applying sync. Their exact semantics (clamping, units interpretation, interaction with other filters in the chain, thread-safety requirements) have not been verified against the version pinned. ARCHITECTURE.md documents the intent but no smoke test calls these APIs yet.

### `obs_filter_get_parent` called at destroy time without null-check on `data->source`
`av_sync_filter_destroy` calls `obs_filter_get_parent(data->source)` — but if OBS has already nulled the source reference by the time destroy is called, this dereferences a stale pointer. The null-check guards `data->source` but not what `obs_filter_get_parent` itself does with it.

### Linux is untested
`buildspec.json` has no Linux hash entries. ROADMAP Phase 7 mentions `.deb/.rpm` as optional. CI likely runs Linux (template default), but no local build or runtime verification for Linux has been documented.

---

## Performance Concerns

### Heap allocation on audio thread (already noted above — critical)
`av_sync_ring_create` is called from `av_sync_filter_audio` on the first callback. On a heavily loaded system this can block the audio thread.

### Stack-allocated 2048-float scratch in the hot path
`av_sync_filter_audio` declares `float scratch[AV_SYNC_DOWNMIX_SCRATCH]` (8 KB) on the stack inside the audio callback on every invocation (when the ring is live and frames ≤ 2048). Stack allocation is fast, but 8 KB per callback on a tight audio thread stack could be a concern on platforms with limited thread stack size. OBS audio threads typically have 512 KB–1 MB stacks, so this is low risk but worth noting.

### `oversize_skips` silently discards frames > 2048
If OBS ever delivers chunks larger than 2048 frames (non-standard but possible in certain source configurations), samples are silently dropped. There is no log warning at the drop site, only a counter in the rollup log (every 5 s). Drop events that occur between rollups are invisible in real time.

### Ring capacity hardcoded to 10 s at the source's full sample rate
`AV_SYNC_RING_SECONDS = 10`. At 48 kHz that is 480,000 floats = 1.92 MB per filter instance. With six camera sources that is ~11.5 MB of ring memory. This is fine for a few sources, but ARCHITECTURE.md planned to downsample to 16 kHz for analysis (160,000 samples = 640 KB per ring). The current implementation stores at full source rate, wasting ~3× memory and making future FFT window extraction more complex.

### No downsampling to 16 kHz
ARCHITECTURE.md and ROADMAP Phase 2 both mention downsampling to 16 kHz mono for the analysis path. The current ring stores full-rate mono. Without a resampler, the Phase 3 GCC-PHAT FFT will operate on 48 kHz data, requiring 3× larger FFT windows for equivalent time coverage and proportionally more CPU.

---

## Licensing & Dependency Risks

### PFFFT license must be verified before vendoring
ROADMAP Phase 3 specifies **PFFFT** as the FFT library ("BSD-like, small, fast"). PFFFT is licensed under a custom BSD-style license that is GPL-compatible; however it must be verified against the exact version used. KissFFT (BSD-3-Clause) is also mentioned in CLAUDE.md as an alternative. FFTW is explicitly forbidden due to GPL-3.0 incompatibility with GPL-2.0-only code.

### GPL-2.0-or-later header mismatch risk
All current source files use the `version 2 of the License, or (at your option) any later version` boilerplate (GPL-2.0-or-later), which is correct. Adding any GPL-2.0-only dependency in the future would create an incompatibility. This is currently only a future risk, but must be enforced in dependency review.

### No SPDX identifiers in source files
Source files use prose license headers rather than machine-readable `SPDX-License-Identifier: GPL-2.0-or-later` tags. This is a minor tooling/compliance debt.

---

## Build & Maintenance Risks

### Template divergence
The repo is derived from `obs-plugintemplate`. As the upstream template receives OBS version bumps, CI workflow updates, and CMake helper changes, the fork will drift. There is no automated upstream-sync mechanism. Manually tracking template changes for `cmake/`, `.github/`, and `build-aux/` is a sustained maintenance burden.

### `ENABLE_FRONTEND_API` and `ENABLE_QT` are `OFF` by default and must be toggled manually
When Phase 5 (filter properties UI) and Phase 6 (dock UI) land, developers must remember to enable these flags. They are not auto-detected. A missed flag during a local build will silently omit UI code without a compile error.

### No test infrastructure
`CLAUDE.md` and `ROADMAP.md` both call out adding tests under `tests/` for the GCC-PHAT engine. No `tests/` directory exists, no CMake test target is wired, and no CI step runs tests. When the DSP layer lands, test infrastructure will need to be added from scratch.

### `buildspec.json` has no Linux `prebuilt`/`qt6` hashes
The `hashes` objects under `prebuilt` and `qt6` only list `macos` and `windows-x64` keys. The template CI likely expects a Linux entry. This may cause Linux CI to fail or use an un-pinned version.

### CMakeLists.txt does not vendor or find any FFT library
No `find_package(PFFFT)`, no `FetchContent`, no submodule reference. When Phase 3 adds GCC-PHAT, the FFT dependency integration must be added to CMakeLists.txt. This is a build system gap that could cause integration friction.

---

## Recommended Mitigations

Prioritized by risk of causing silent corruption or crashes in future phases:

1. **[Critical] Move ring buffer allocation to `av_sync_filter_create`** — allocate eagerly (sample rate is available via `obs_get_audio_info` at create time) to eliminate heap allocation on the audio thread. If sample rate is not yet known at create time, use a fixed 48 kHz default and resize on first callback only if the actual rate differs.

2. **[Critical] Design the SPSC ring for thread-safety before Phase 3** — add `_Atomic` write/read head cursors and appropriate memory ordering (acquire/release) before the analysis thread is introduced. Retrofitting this later is more disruptive than designing it correctly now.

3. **[High] Add a `av_sync_ring_read` / window-copy API** — the analysis thread needs to extract a contiguous time-aligned window; design this API alongside the atomic head/tail counters.

4. **[High] Implement downsampling (16 kHz) in the ring write path** — reduces per-ring memory 3×, simplifies FFT window sizing, and aligns with the documented architecture. Consider an integer polyphase decimator (3:1 for 48 kHz → 16 kHz).

5. **[High] Log a warning (rate-limited) when `oversize_skips` increments** — silent sample loss is operationally dangerous during a live production.

6. **[Medium] Add `tests/` scaffold with a CMake CTest target** — even an empty scaffold ensures CI is wired before DSP code lands.

7. **[Medium] Pin and vendor PFFFT before Phase 3** — add as a git submodule or `FetchContent` in CMakeLists.txt; verify its license text against GPL-2.0-or-later compatibility now, not during a time-pressured phase.

8. **[Medium] Document a template-sync cadence** — track upstream `obs-plugintemplate` releases (watch the repo) and rebase `cmake/` and `.github/` changes quarterly.

9. **[Low] Add Linux prebuilt hashes to `buildspec.json`** — prevents silent CI degradation as Linux support is exercised.

10. **[Low] Add `SPDX-License-Identifier` tags** to source files for tooling compatibility.
