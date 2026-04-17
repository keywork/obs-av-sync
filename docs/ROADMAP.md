# Roadmap

Phased plan. Each phase is a shippable state — not every phase ships publicly, but each is internally testable before moving on.

## Phase 0 — Project setup (done)

- Fork of obs-plugintemplate, renamed and re-licensed.
- `README`, `CLAUDE.md`, `docs/ARCHITECTURE.md`, this roadmap.
- GPL-2.0 LICENSE (inherited from template).
- CI from template already covers Windows/macOS/Linux build.

## Phase 1 — Skeleton plugin loads in OBS

**Goal:** plugin compiles, installs, and logs on load/unload in OBS 31.x on Windows.

- Build locally via CMake preset; confirm `.dll` lands in OBS plugin dir.
- Confirm OBS log shows `[obs-av-sync] plugin loaded successfully (version 0.1.0)`.
- No functional behavior yet.

**Exit criteria:** user can install the plugin and see it in OBS "Help → About → Licenses" (or equivalent) without crashing OBS.

## Phase 2 — Audio tap + ring buffer

**Goal:** receive audio data from arbitrary OBS sources and buffer recent samples in a thread-safe ring.

- Register an audio-capture callback on target sources (`obs_source_add_audio_capture_callback`), or register a filter of type `OBS_SOURCE_TYPE_FILTER | OBS_SOURCE_AUDIO`. Decision: **filter type** (gives per-instance state + settings UI for free).
- Implement a mono lock-free ring buffer per source, fed from the audio thread, drained by the analysis thread.
- Resample/downmix as needed — decide on 16 kHz mono for analysis (cheap FFTs, plenty of bandwidth for correlation).
- Log buffer fill level for debugging.

**Exit criteria:** adding the filter to a source causes per-source ring to fill continuously without drops during 10-minute soak test.

## Phase 3 — Offline offset measurement (GCC-PHAT)

**Goal:** given two buffered audio streams (reference + target), output a single offset estimate in nanoseconds.

- Pick FFT lib: **PFFFT** (BSD-like, small, fast). Vendor as submodule.
- Window of 2–4 seconds, Hann-windowed, 50 % overlap.
- Compute `X(f) * conj(Y(f)) / |X(f) * conj(Y(f))|`, IFFT, pick peak, parabolic interpolation on peak bin → sub-sample precision.
- Unit-test with synthetic signals: known delay + noise + known SNR.
- Expose via a command (e.g. an obs-hotkey or log-only button) that dumps current estimate to log.

**Exit criteria:** synthetic-signal tests pass across ±500 ms delays with < 1 ms error at SNR ≥ 10 dB.

## Phase 4 — Continuous sync engine

**Goal:** measurement runs continuously, results are smoothed, and the filter applies them to its parent source.

- Analysis thread per filter, firing at ~2 Hz.
- Kalman-style or exponential smoothing of offset estimates; reject low-confidence measurements (peak-to-sidelobe ratio threshold).
- Slew-rate-limit actual applied offset to avoid jarring jumps on scene/source changes.
- Call `obs_source_set_sync_offset()` on the parent source.
- Handle edge cases: source muted, reference silent (just hold last offset), sample-rate mismatch, stream start/stop.

**Exit criteria:** live session with two cameras + house-audio reference keeps measured residual offset < 20 ms for an hour.

## Phase 5 — UI (filter properties)

**Goal:** user-facing controls via OBS source filter properties.

- Reference source picker (dropdown of audio sources).
- "Enabled" toggle.
- Read-only status fields: current offset, confidence, last update.
- Min/max offset clamp controls.
- Requires `ENABLE_FRONTEND_API` (not `ENABLE_QT` if we stick to filter properties).

**Exit criteria:** non-developer can configure the plugin with only the OBS UI.

## Phase 6 — Dock UI (optional)

**Goal:** single dock panel showing per-source sync state across the whole scene collection.

- Qt6 dock (`ENABLE_QT=ON`).
- Table of tracked sources, live offset graphs, manual override.

**Exit criteria:** dock doesn't crash on source add/remove/rename.

## Phase 7 — Release polish

- Installer packaging (template handles Windows/macOS; Linux .deb/.rpm optional).
- Signed Windows binaries if feasible.
- macOS notarization (template supports but needs secrets).
- Changelog, GitHub release with semver tag (`0.1.0`).

## Non-goals (for now)

- Video-based sync (lip movement, clapperboard detection) — audio correlation is the scope.
- NDI-specific transport handling — we work against whatever OBS exposes as an audio source.
- Multi-reference sync (sync against a mix of references) — single reference per filter instance.
