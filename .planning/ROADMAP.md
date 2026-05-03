# Roadmap — obs-av-sync

## Milestone 1: Hands-Off AV Sync

**Goal:** A live production can run with 2–4 IP cameras and house audio fully auto-synced, with status visible in an OBS dock.

---

### Completed

#### Phase 1: Plugin Foundation ✓
Already done: OBS module registration, filter skeleton (`av_sync_filter.c`), CI/CD across Windows/macOS/Linux.

#### Phase 2: Ring Buffer ✓
Already done: mono float32 ring buffer (`ring_buffer.c`) with timestamp tracking, downmix, and diagnostic rollup logging.

---

### Active

#### Phase 3: RT-Thread Safety & Ring Hardening
**Goal:** Eliminate the heap-allocation bug on the OBS audio thread and upgrade the ring buffer to a thread-safe SPSC design with a read API, so all later DSP work rests on a correct foundation.
**Requirements:** SYNC-04
**Plans:**
1. Move ring allocation to `av_sync_filter_create` — call `obs_get_audio_info()` at create time (48 kHz default fallback) so `av_sync_ring_create` is never called inside `av_sync_filter_audio`
2. Upgrade ring buffer to SPSC atomics — replace plain `uint64_t` head with `_Atomic size_t` write cursor; add acquire/release ordering for Phase 4 analysis-thread reads
3. Add `av_sync_ring_read` window-copy API — extract a contiguous time-aligned float window with consumer-cursor bookkeeping; needed by the GCC-PHAT engine
4. Add oversize-chunk warning log — rate-limited `blog()` warning whenever `oversize_skips` increments so silent frame loss becomes visible during a show
**Success criteria:**
1. Adding the filter to a source no longer triggers any heap allocation inside the audio callback (verified by heap-profiling or code inspection)
2. Ring buffer compiles and passes a basic SPSC read/write round-trip under ThreadSanitizer or equivalent
3. `av_sync_ring_read` returns the correct sample window when called from a second thread while the audio thread writes concurrently
4. An oversize chunk during a soak test now emits a warning to the OBS log within 1 second of the drop

---

#### Phase 4: GCC-PHAT Offset Engine
**Goal:** Given two buffered audio streams (reference + target), produce a nanosecond offset estimate that passes unit tests at ≥10 dB SNR across the full ±500 ms range.
**Requirements:** SYNC-01, SYNC-05
**Plans:**
1. Vendor PFFFT — add as a `FetchContent` or git submodule in `CMakeLists.txt`; verify BSD-style licence against GPL-2.0-or-later compatibility; document the verification in `docs/`
2. Implement `gcc_phat.cpp` — pure function `estimate_offset(ref, target, n, rate) → {offset_ns, confidence}`; pipeline: Hann window → PFFFT forward → cross-spectrum → PHAT whitening → PFFFT inverse → peak pick → parabolic sub-sample interpolation; confidence = peak-to-sidelobe ratio
3. Wire CMake test scaffold (`tests/` + CTest target) and write GCC-PHAT unit tests — synthetic signals with known delays, Gaussian noise, SNR 10–40 dB, offset range ±500 ms
**Success criteria:**
1. All unit tests pass: measured offset within 1 ms of ground-truth delay at SNR ≥ 10 dB across the full ±500 ms range
2. PFFFT builds cleanly on all three platforms (Windows, macOS, Linux) via CI
3. `ctest --test-dir build` exits 0 with no failures
4. `estimate_offset` returns a meaningful confidence score that visibly decreases as SNR decreases in the test suite

---

#### Phase 5: Reference Tap & Source Configuration
**Goal:** Users can designate any OBS audio source as the house reference and enable/disable tracking per camera; both settings persist across OBS restarts.
**Requirements:** REF-01, REF-02, REF-03
**Plans:**
1. Implement `reference_tap.cpp` — singleton that attaches an `obs_source_add_audio_capture_callback` to the designated reference source, writes PCM into a shared reference ring (SPSC-safe), and cleanly detaches on source removal or plugin unload
2. Add filter properties (`get_properties` / `update` hooks in `av_sync_filter.cpp`) — reference source dropdown (populated from `obs_enum_sources`), enable/disable toggle; wire `ENABLE_FRONTEND_API=ON` in CMakeLists.txt
3. Persist settings to scene collection — ensure `obs_data_t` serialization round-trips reference source name and enable state through OBS save/load; add startup validation that the saved source still exists
**Success criteria:**
1. User can pick any OBS audio source as the reference from the filter properties panel without editing any file
2. Per-camera enable/disable toggle takes effect immediately without restarting OBS
3. Restarting OBS with a saved scene collection restores the reference source and enable state exactly as configured
4. Removing the designated reference source from OBS does not crash the plugin; it logs a warning and holds the last known offset

---

#### Phase 6: Continuous Sync Engine
**Goal:** Offset measurements run continuously on a background thread, are smoothed and slew-rate-limited, and are applied automatically to each camera source — no operator action required during a live show.
**Requirements:** SYNC-02, SYNC-03, DRIFT-01
**Plans:**
1. Spawn per-filter analysis thread — fires at ~2 Hz; reads a 2–4 s overlapping window from the per-source ring and the shared reference ring; calls `estimate_offset`; thread lifecycle tied to filter create/destroy
2. Implement `smoother.cpp` — exponential weighting of offset history; reject measurements below confidence threshold; slew-rate cap (configurable max Δ per update) to prevent jarring jumps; hold last valid offset when reference or source is silent
3. Apply offset via `obs_source_set_sync_offset` — convert smoothed offset to nanoseconds; call on analysis thread (OBS API is thread-safe for this); handle muted sources, stream restarts, and sample-rate mismatches gracefully
4. Drift tracking over long shows — smoother accumulates corrections over the session lifetime; no periodic reset; verified by a timed soak scenario
**Success criteria:**
1. Adding the filter to two camera sources with house audio as reference causes both sources to be automatically corrected within 10 seconds of stream start — no manual steps
2. Measured residual offset stays below 20 ms during a continuous 60-minute session with two cameras
3. Temporarily silencing the reference source (mute for 10 s then unmute) causes the plugin to hold the last offset, not reset to zero
4. A source that starts with a 300 ms offset reaches ≤20 ms residual within 30 seconds

---

#### Phase 7: ONVIF Drift Evaluation
**Goal:** Document whether ONVIF clock sync is a viable complement to GCC-PHAT for tracking slow drift, with a concrete recommendation.
**Requirements:** DRIFT-02
**Plans:**
1. Research ONVIF clock-sync feasibility — review ONVIF Device Management spec (NTP/PTP via ONVIF); identify what OBS exposes about RTMP/Media source timing; assess whether clock deltas are accessible from the plugin layer without a direct ONVIF connection
2. Prototype or evaluate — attempt a minimal proof-of-concept (or document why it is not feasible) against the cameras in the target production setup; measure achievable accuracy vs. GCC-PHAT
3. Write recommendation doc — `docs/ONVIF-EVAL.md` with: method tested, accuracy measured, integration complexity, verdict (adopt / defer / skip) and rationale
**Success criteria:**
1. `docs/ONVIF-EVAL.md` exists and contains a clear adopt/defer/skip verdict with supporting data
2. If adopted: a prototype shows clock-delta accuracy within 5 ms for the target cameras
3. If deferred/skipped: the document explains the specific technical blocker so the decision can be revisited later without re-doing the research

---

#### Phase 8: Dock UI
**Goal:** An OBS dock panel shows all tracked camera sources with their current sync status, detected offsets, and correction state — always visible without blocking the scene editor.
**Requirements:** UI-01, UI-02, UI-03, UI-04
**Plans:**
1. Enable Qt6 dock infrastructure — set `ENABLE_QT=ON` and `ENABLE_FRONTEND_API=ON` in CMakeLists.txt; register a `QDockWidget` via `obs_frontend_add_dock`; confirm dock survives source add/remove/rename without crashing
2. Build source status table — `QTableWidget` or custom model with one row per filter instance; columns: source name, status (Synced / Measuring / Out of Range), detected offset (ms), applied correction (ms); data polled from filter instances at ~4 Hz on the UI thread
3. Implement status indicator logic — map internal smoother state to the three status values; expose via a thread-safe atomic status field on each filter instance read by the dock
4. Non-intrusive layout and lifecycle — dock must not steal focus, must handle zero tracked sources gracefully, and must not prevent the OBS scene editor from rendering
**Success criteria:**
1. Opening the dock from the OBS menu shows a live table with one row per camera source that has the filter applied
2. Detected offset (ms) and applied correction (ms) update visibly as the analysis thread runs — no manual refresh required
3. Each source row shows the correct status indicator: "Measuring" during the first correlation window, "Synced" once residual is stable, "Out of Range" if confidence falls below threshold
4. Adding or removing the filter from a source updates the dock table within 2 seconds without crashing OBS
5. The dock does not block or visibly stutter the OBS scene editor during normal operation
