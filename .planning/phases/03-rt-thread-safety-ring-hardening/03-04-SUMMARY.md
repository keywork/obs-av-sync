---
phase: 3
plan: 4
key-files.modified:
  - src/av_sync_filter.c
requirements-completed:
  - SYNC-04
---

# Plan 03-04 Summary: Add oversize-chunk rate-limited warning log

## Objective
Emit a rate-limited `LOG_WARNING` to the OBS log whenever an audio chunk is skipped due to exceeding `downmix_capacity`, making previously-silent frame loss visible during a live show.

## Tasks Completed

### T01 — Verify oversize_skips increment uses downmix_capacity (Gate Check)
**Status:** ✓ Passed without code changes

- Verified `src/av_sync_filter.c` line 130 contains: `if (audio->frames > data->downmix_capacity)`
- Verified `src/av_sync_filter.c` does NOT contain `audio->frames > AV_SYNC_DOWNMIX_SCRATCH`
- This confirmed Plan 1's `downmix_capacity` field was present and correct, satisfying the precondition for Plan 4.

### T02 — Add rate-limited oversize warning inside the existing rollup log gate
**Status:** ✓ Completed

- Added a `LOG_WARNING` block inside the existing `AV_SYNC_DIAG_LOG_INTERVAL_NS` rollup gate (lines 154–160 of `src/av_sync_filter.c`).
- The warning fires only when `data->oversize_skips > 0`.
- The warning includes: `oversize_skips` count, source name, and `data->downmix_capacity`.
- The warning format string intentionally does NOT include `frames=%u` (the current callback's frame count is not the oversize chunk that was skipped).
- The warning appears BEFORE the existing `LOG_INFO "passthrough rollup"` log.
- No new timer or gate was created — rate limiting is provided by the existing 5-second rollup window.

## Commit

| Task | Hash | Message |
|------|------|---------|
| T02 | `cf4edfd` | `feat(03-04): Add rate-limited oversize warning inside existing rollup log gate` |

## Verification Results

### Build
```
cmake --build build_x64 --config RelWithDebInfo
```
**Result:** ✓ Succeeded with no new warnings or errors.

Build output:
```
  plugin-support.vcxproj -> ...\build_x64\RelWithDebInfo\plugin-support.lib
  av_sync_filter.c
     Creating library ...\obs-av-sync.lib and object ...\obs-av-sync.exp
  Generating code
  Finished generating code
  obs-av-sync.vcxproj -> ...\RelWithDebInfo\obs-av-sync.dll
  Copy obs-av-sync to rundir	Copy obs-av-sync resources to rundir
  test_ring_spsc.vcxproj -> ...\RelWithDebInfo\test_ring_spsc.exe
```

### Grep Checks

| Check | Pattern | Result |
|-------|---------|--------|
| Warning presence | `LOG_WARNING` | ✓ Exactly 1 match (line 157) |
| Warning message | `oversize chunk skip` | ✓ Present on line 158 |
| No `frames=%u` in warning | `frames=%u` in `av_sync_filter.c` | ✓ Only appears in detailed callback `LOG_INFO` (line 149), not in warning |
| Rate-limiting gate | `AV_SYNC_DIAG_LOG_INTERVAL_NS` | ✓ 2 matches: definition (line 32) and single gate usage (line 153) |
| `oversize_skips > 0` in gate | `data->oversize_skips > 0` | ✓ Present inside rollup gate (line 154) |
| `downmix_capacity` usage | `downmix_capacity` | ✓ 5 matches: struct field, create-time init, bzalloc, oversize condition (line 130), warning format (line 159) |
| No duplicate timer | `window_start_ns = ts` | ✓ Exactly 1 match (line 174) |

## Deviations / Issues

None. All tasks executed exactly as specified in the plan. No deviations were encountered.

## Requirements Satisfied

- **SYNC-04:** Rate-limited oversize-chunk warning log is now emitted inside the existing 5-second diagnostic rollup window, making silent frame loss visible to operators.
