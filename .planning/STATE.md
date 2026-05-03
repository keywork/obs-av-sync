# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-03)

**Core value:** Cameras stay in sync with the house audio automatically, every show, without the operator touching anything.
**Current focus:** Phase 6 — Continuous Sync Engine

## Status

**Milestone:** 1 — Hands-Off AV Sync
**Phase:** 6 of 8 (Continuous Sync Engine)
**Last updated:** 2026-05-03 after Phase 5 completion

## Phase Completion

| Phase | Name | Status |
|-------|------|--------|
| 1 | Plugin Foundation | ✓ Complete |
| 2 | Ring Buffer | ✓ Complete |
| 3 | RT-Thread Safety & Ring Hardening | ✓ Complete |
| 4 | GCC-PHAT Offset Engine | ✓ Complete |
| 5 | Reference Tap & Source Configuration | ✓ Complete |
| 6 | Continuous Sync Engine | ⬜ Not started |
| 7 | ONVIF Drift Evaluation | ⬜ Not started |
| 8 | Dock UI | ⬜ Not started |

## Open Items

- Phase 6 Plan 1 (06-01): Continuous Sync Loop — Not started
- Phase 6 Plan 2 (06-02): Hysteresis & Confidence Thresholds — Not started
- Phase 6 Plan 3 (06-03): Smoothing / EMA Offset Application — Not started

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
