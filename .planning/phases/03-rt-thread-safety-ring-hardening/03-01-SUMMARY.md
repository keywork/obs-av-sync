---
phase: 3
plan: 1
subsystem: av_sync_filter
tags: [rt-thread-safety, heap-allocation, downmix]
requires: [SYNC-04]
key-files.created: []
key-files.modified: [src/av_sync_filter.c]
key-decisions:
  - Moved ring buffer and downmix scratch allocation from audio callback to filter create time
  - Replaced stack VLA with heap-allocated per-filter buffer sized to sample_rate * AV_SYNC_MAX_CHUNK_S
requirements-completed: [SYNC-04]
duration: "~25 minutes"
completed: "2026-05-03T14:04:01-04:00"
---

# Phase 3 Plan 1: Move ring + downmix buffer allocation to av_sync_filter_create — Summary

One-liner: Eliminated all heap allocation on the OBS real-time audio thread by moving `av_sync_ring_create` and the downmix scratch buffer into `av_sync_filter_create`.

## Tasks Completed
| Task | Title | Commit |
|------|-------|--------|
| T01 | Add downmix_scratch and downmix_capacity fields to av_sync_filter_data | b61809f |
| T02 | Add AV_SYNC_MAX_CHUNK_S macro and remove AV_SYNC_DOWNMIX_SCRATCH macro | b7e40e2 |
| T03 | Update av_sync_filter_create to allocate ring and downmix buffer at create time | 28de4a3 |
| T04 | Update av_sync_filter_destroy to free the downmix scratch buffer | f0bde01 |
| T05 | Remove lazy-init block from av_sync_filter_audio and switch downmix to heap buffer | f66e36a |

## Deviations from Plan
- **Macro placement fix (commit 40d5c08):** The plan placed `#define AV_SYNC_MAX_CHUNK_S 1` immediately before `av_sync_filter_audio`, after `av_sync_filter_create`. This caused a compile error because `av_sync_filter_create` uses the macro on line 74 (before the macro's definition on line 98). Fixed by moving the macro to the top of the file alongside the other macros (`AV_SYNC_RING_SECONDS`, etc.). No functional impact.

## Verification Results
- **Build check:** PASSED — `cmake -S . -B build_x64 -G "Visual Studio 18 2026" -A x64` and `cmake --build build_x64 --config RelWithDebInfo` succeeded with zero errors.
- **Code inspection (heap allocation grep):** PASSED — `av_sync_ring_create` and `bzalloc` appear only inside `av_sync_filter_create`, never inside `av_sync_filter_audio`. No `malloc`/`calloc` anywhere in the file.
- **Code inspection (old scratch removal):** PASSED — `AV_SYNC_DOWNMIX_SCRATCH` and `float scratch[` return zero matches.
- **Code inspection (new field usage):** PASSED — `downmix_scratch` and `downmix_capacity` appear in struct declaration, allocate in `_create`, free in `_destroy`, and pointer use in `_audio`.

## Self-Check: PASSED
