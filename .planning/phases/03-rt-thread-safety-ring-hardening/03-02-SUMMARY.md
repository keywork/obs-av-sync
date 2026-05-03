---
phase: 3
plan: 2
key-files.modified:
  - src/ring_buffer.h
  - src/ring_buffer.c
  - CMakeLists.txt
requirements-completed:
  - SYNC-04
---

# Plan 03-02 Summary: Upgrade ring buffer to SPSC atomics

## Tasks Completed

| Task | Title | Commit |
|------|-------|--------|
| T01 | Add stdatomic.h include to ring_buffer.h and declare cursor type | `6f0ef97` |
| T02 | Update struct av_sync_ring — change total_written to _Atomic size_t and add oldest_timestamp_ns | `903e137` |
| T03 | Update av_sync_ring_write to use atomic store with release ordering and maintain oldest_timestamp_ns | `903e137` |
| T04 | Update av_sync_ring_get_stats to use relaxed atomic read and read oldest_timestamp_ns directly | `bf82f55` |
| T05 | Add C11 standard guard to CMakeLists.txt | `aae4f03` |
| — | Add MSVC /experimental:c11atomics flag to enable C11 _Atomic support | `70a5da0` |

## Verification Results

### 1. Build Check
```
cmake -S . -B build_x64 -G "Visual Studio 18 2026" -A x64 -DENABLE_FRONTEND_API=OFF -DENABLE_QT=OFF
cmake --build build_x64 --config RelWithDebInfo
```
**Result:** PASS — zero errors, zero warnings about `_Atomic`.

### 2. Atomic Usage
```
grep -n "atomic_" src/ring_buffer.c
```
**Result:** PASS — shows `atomic_load_explicit` (relaxed) in `av_sync_ring_write`, `atomic_store_explicit` (release) in `av_sync_ring_write`, and `atomic_load_explicit` (relaxed) in `av_sync_ring_get_stats`.

### 3. No Plain Increment
```
grep -n "total_written +=" src/ring_buffer.c
```
**Result:** PASS — zero results.

### 4. oldest_timestamp_ns
```
grep -n "oldest_timestamp_ns" src/ring_buffer.c
```
**Result:** PASS — shows assignment in `av_sync_ring_write` (both first-write and full-ring paths) and direct read `out->oldest_timestamp_ns = r->oldest_timestamp_ns;` in `av_sync_ring_get_stats`.

### 5. CMake Standard
```
grep -n "C_STANDARD" CMakeLists.txt
```
**Result:** PASS — shows `C_STANDARD 11` and `C_STANDARD_REQUIRED ON`.

## Deviations / Issues

### MSVC requires `/experimental:c11atomics`

The plan assumed that `set_property(TARGET ... PROPERTY C_STANDARD 11)` (or 17) would be sufficient to enable C11 `<stdatomic.h>` on MSVC. On the build machine (MSVC 19.50.35725, Visual Studio 2022 / BuildTools 18), `/std:c17` alone still defines `__STDC_NO_ATOMICS__`, causing `#error "C atomic support is not enabled"` in `vcruntime_c11_stdatomic.h`.

**Fix applied:** Added `target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /experimental:c11atomics)` inside the `if(MSVC)` block in `CMakeLists.txt`. This is an additional commit (`70a5da0`) beyond the original T05.

**Impact:** The `_Atomic` keyword and `<stdatomic.h>` now compile successfully on Windows. The acceptance criteria (`C_STANDARD 11` and `C_STANDARD_REQUIRED ON` in CMakeLists.txt) are still satisfied.

### CMake generator mismatch

The `windows-x64` preset is hardcoded to `Visual Studio 17 2022`, but the system has `Visual Studio 18 2026`. Local builds used `-G "Visual Studio 18 2026"` directly. This is an environment issue, not a code deviation.

## Files Modified

- `src/ring_buffer.h` — Added `#include <stdatomic.h>`
- `src/ring_buffer.c` — Upgraded `struct av_sync_ring` with `_Atomic size_t total_written` and `uint64_t oldest_timestamp_ns`; rewrote `av_sync_ring_write` with acquire/release atomics and eager `oldest_timestamp_ns` maintenance; rewrote `av_sync_ring_get_stats` to read atomically and read `oldest_timestamp_ns` directly
- `CMakeLists.txt` — Added `C_STANDARD 11`, `C_STANDARD_REQUIRED ON`, and MSVC-specific `/experimental:c11atomics` flag

## Notes

- `av_sync_ring_stats_t.total_written` remains `uint64_t` (public stats type unchanged) per must_haves.
- `bzalloc` zeroes memory, so `total_written` is correctly initialized to 0 without a separate `atomic_init` call.
- The SPSC design is now ready for Plan 3 (`av_sync_ring_read` window-copy API with caller-owned cursor).
