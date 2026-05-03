# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-03)

**Core value:** Cameras stay in sync with the house audio automatically, every show, without the operator touching anything.
**Current focus:** Phase 5 — Reference Tap & Source Configuration

## Status

**Milestone:** 1 — Hands-Off AV Sync
**Phase:** 5 of 8 (Reference Tap & Source Configuration)
**Last updated:** 2026-05-03 after code review fixes (Phase 4 fully complete)

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

- Phase 5 Plan 1 (05-01): Reference Tap — ✓ Complete
- Phase 5 Plan 2 (05-02): Source Configuration UI — ✓ Complete
- Phase 5 Plan 3 (05-03): Settings Persistence + Lifecycle Handling — ✓ Complete

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
