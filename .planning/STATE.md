# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-03)

**Core value:** Cameras stay in sync with the house audio automatically, every show, without the operator touching anything.
**Current focus:** Milestone 1 complete — all 8 phases finished. Ready for Milestone 2 planning or v1 release preparation.

## Status

**Milestone:** 1 — Hands-Off AV Sync
**Phase:** 8 of 8 (Dock UI) — ✓ Complete
**Last updated:** 2026-05-04 after Phase 8 completion and final verification

## Phase Completion

| Phase | Name | Status |
|-------|------|--------|
| 1 | Plugin Foundation | ✓ Complete |
| 2 | Ring Buffer | ✓ Complete |
| 3 | RT-Thread Safety & Ring Hardening | ✓ Complete |
| 4 | GCC-PHAT Offset Engine | ✓ Complete |
| 5 | Reference Tap & Source Configuration | ✓ Complete |
| 6 | Continuous Sync Engine | ✓ Complete |
| 7 | ONVIF Drift Evaluation | ✓ Complete |
| 8 | Dock UI | ✓ Complete |

## Open Items

- None — Milestone 1 is complete.

## Phase 8 Post-Execution Notes

- Code review (`08-REVIEW.md`) found 5 issues (1 critical, 2 warnings, 2 info).
- 3 issues fixed and verified in commit `9ff8289` (CR-01, WR-01, WR-02).
- 2 info items deferred to future milestones (IN-01 through IN-05 documented in review).
- Fix log: `.planning/phases/08-dock-ui/08-REVIEW-FIX.md`
- Qt6 dock with color-coded status table (Material Design palette), timer-based refresh at 2 Hz (500 ms interval)
- Atomic filter fields (`_Atomic int status`, `_Atomic int sync_enabled`, `_Atomic double smoothed_offset_ms`, `_Atomic double last_confidence`) for thread-safe UI reads without locking
- Timer-based refresh at 2 Hz gives smooth updates without hammering the UI thread
- **Verification discovery:** SPSC round-trip test (`test_ring_spsc.c`) was failing due to a pre-existing test bug introduced in Phase 3 commit `7e90739`. The test verified every sample value but used a ring capacity of 4800 for 480,000 total samples, guaranteeing reader lapping on multi-core Windows. Fixed by sizing the ring to hold all data (`CAPACITY = CHUNKS * CHUNK_SIZE`) and allocating the read buffer on the heap to avoid stack overflow. Both tests now pass.
- 36/36 GCC-PHAT synthetic tests pass (< 1 ms accuracy, confidence > 1.0)
- 1/1 SPSC ring round-trip test passes (480,000 samples)

## Phase 7 Post-Execution Notes

- Verdict: **Defer** ONVIF clock sync for v1
- Research covered: ONVIF Device Management spec, NTP/PTP accuracy, SOAP RTT analysis, OBS timing APIs, client library survey, integration paths
- Key finding: No ONVIF mechanism reliably achieves ≤ 5 ms on generic cameras
- Recommendation document: `docs/ONVIF-EVAL.md` with 7 sections, ~3,725 words
- Revisit conditions: OBS adds media timing APIs, camera fleet upgrades to PTP, or v2 needs silent-period drift tracking

## Phase 6 Post-Execution Notes

- Code review (`06-REVIEW.md`) found 7 issues (2 critical, 5 warnings).
- 6 issues fixed and verified in commit `322ad44`; 3 warnings deferred to Phase 7/8 (WR-02, WR-03, WR-05).
- Fix log: `.planning/phases/06-continuous-sync-engine/06-REVIEW-FIX.md`
- 36/36 GCC-PHAT synthetic tests pass (< 1 ms accuracy, confidence > 1.0)
- 1/1 SPSC ring round-trip test passes (480,000 samples)
- Key design decisions from Phase 6:
  1. Per-filter analysis thread at 500 ms cadence
  2. EMA smoother (alpha = 0.3) with confidence gating and slew-rate cap (±20 ms per update)
  3. Drift tracking and stream restart detection via `total_written` monotonicity check
  4. Atomic `sync_enabled` and atomic reference-tap globals for thread safety

## Phase 5 Post-Execution Notes

- Code review (`05-REVIEW.md`) found 6 issues (2 critical, 4 warnings).
- All 6 issues fixed and verified.
- Fix log: `.planning/phases/05-reference-tap-source-config/05-REVIEW-FIX.md`
- 36/36 GCC-PHAT synthetic tests pass (< 1 ms accuracy, confidence > 1.0)
- 1/1 SPSC ring round-trip test passes
- Key design decisions from Phase 5:
  1. Reference tap design: global singleton with mutex-protected shared ring
  2. Per-filter properties: reference source dropdown + enable toggle
  3. Settings persistence via `obs_data_t` (filter settings serialized in OBS scenes)

## Phase 4 Post-Execution Notes

- Code review found 2 minor issues (info level), both fixed.
- Fix log: `.planning/phases/04-gcc-phat-offset-engine/04-REVIEW-FIX.md`
- 36/36 synthetic tests pass (< 1 ms accuracy, confidence > 1.0)
- Key bugs caught during testing:
  1. `pffft_transform` → `pffft_transform_ordered` (frequency ordering)
  2. Cross-spectrum sign: `x_tgt * conj(x_ref)` (positive delay convention)

## Phase 3 Post-Execution Notes

- Code review (`03-REVIEW.md`) found 7 issues (1 critical, 3 warnings, 3 info).
- All 7 issues fixed in 3 commits (`7e90739`, `9080ef9`, `4549114`).
- Fix log: `.planning/phases/03-rt-thread-safety-ring-hardening/03-REVIEW-FIX.md`
