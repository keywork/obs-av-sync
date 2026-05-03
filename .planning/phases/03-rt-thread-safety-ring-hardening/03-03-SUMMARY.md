---
phase: 3
plan: 3
title: Add av_sync_ring_read window-copy API with caller-owned cursor and test infrastructure
key-files.created:
  - tests/obs_shim.h
  - tests/test_ring_spsc.c
  - tests/CMakeLists.txt
  - tests/util/bmem.h
key-files.modified:
  - src/ring_buffer.h
  - src/ring_buffer.c
  - CMakeLists.txt
  - .gitignore
requirements-completed:
  - SYNC-04
---

# Plan 03-03 Completion Report

## Tasks Completed

All 7 planned tasks (T01–T07) were completed successfully:

| Task | Title | Commit |
|------|-------|--------|
| T01 | Add `av_sync_ring_cursor_t` typedef and read API declarations to `ring_buffer.h` | `da68807` |
| T02 | Implement `av_sync_ring_cursor_init` in `ring_buffer.c` | `fac67de` |
| T03 | Implement `av_sync_ring_read` in `ring_buffer.c` | `a08990a` |
| T04 | Create `tests/obs_shim.h` — OBS header stubs for standalone test compilation | `58c58a3` |
| T05 | Create `tests/test_ring_spsc.c` — SPSC round-trip test | `de9d1e7` |
| T06 | Create `tests/CMakeLists.txt` — CTest wiring with TSan support | `582efd0` |
| T07 | Update root `CMakeLists.txt` — add `enable_testing`, `add_subdirectory(tests)`, and `ENABLE_TSAN` option | `d662100` |

Additional fix commits were required during verification:

| Fix | Description | Commit |
|-----|-------------|--------|
| F01 | Add `tests/util/bmem.h` stub and MSVC C11 atomics flag for test target | `3dacfae` |
| F02 | Update `tests/util/bmem.h` stub with `bzalloc`/`bfree` definitions | `802da80` |

## Deviations and Issues

1. **Gitignore exclusion of `tests/` directory**  
   The repository `.gitignore` uses an exclude-everything pattern (`/*`) that did not re-include `tests/`. Added `!/tests` to `.gitignore` so test files are tracked.

2. **`<util/bmem.h>` include in `ring_buffer.c`**  
   The plan assumed `obs_shim.h` macros would be sufficient for standalone compilation, but `ring_buffer.c` directly includes `<util/bmem.h>` (an OBS header). Created `tests/util/bmem.h` as a minimal stub that defines `bzalloc`/`bfree` via standard `calloc`/`free`, allowing the test target to compile without linking OBS.

3. **MSVC C11 atomics for test target**  
   The root `CMakeLists.txt` already sets `/experimental:c11atomics` for the plugin target, but `tests/CMakeLists.txt` (created per the plan) did not replicate this for `test_ring_spsc`. MSVC 19.50 requires this flag for `<stdatomic.h>` support. Added the flag to `tests/CMakeLists.txt`.

4. **CTest multi-config invocation on Windows**  
   The Visual Studio generator is multi-config; `ctest` requires `-C RelWithDebInfo` to locate the test executable. Direct execution of the test binary works without this flag.

5. **Linux TSan verification**  
   Not performed locally (Windows host). The CI pipeline will validate `ENABLE_TSAN=ON` on the `ubuntu-ci-x86_64` preset.

## Verification Results

### 1. Plugin Build (Windows)

```
cmake -S . -B build_x64 -G "Visual Studio 18 2026" -A x64
cmake --build build_x64 --config RelWithDebInfo
```

**Result:** PASS — `obs-av-sync.dll` built successfully. `test_ring_spsc.exe` built successfully.

### 2. Test Execution (Windows)

```
.\build_x64\tests\RelWithDebInfo\test_ring_spsc.exe
```

**Output:**
```
PASS: spsc_round_trip (480000 samples read, 480000 written)
```

**Result:** PASS

### 3. Read API Declarations (`src/ring_buffer.h`)

```
52: } av_sync_ring_cursor_t;
56: void av_sync_ring_cursor_init(av_sync_ring_t *ring, av_sync_ring_cursor_t *cursor);
63: size_t av_sync_ring_read(av_sync_ring_t *ring, av_sync_ring_cursor_t *cursor,
```

**Result:** PASS — all three symbols present.

### 4. Acquire Ordering (`src/ring_buffer.c`)

```
128: 	size_t tw = atomic_load_explicit(&ring->total_written, memory_order_acquire);
141: 	size_t tw = atomic_load_explicit(&ring->total_written, memory_order_acquire);
```

**Result:** PASS — `memory_order_acquire` appears in both `av_sync_ring_cursor_init` and `av_sync_ring_read`.

### 5. Test Shim (`tests/obs_shim.h`)

```
14: #define bzalloc(n)  calloc(1, (n))
15: #define bfree(p)    free(p)
```

**Result:** PASS — both macro definitions present.

### 6. CTest Wiring (`CMakeLists.txt`)

```
55: enable_testing()
71: add_subdirectory(tests)
```

**Result:** PASS — both directives present.

### 7. Linux TSan (CI)

**Status:** Deferred to CI — local host is Windows; TSan is not available on MSVC.

## Philosophy Compliance

- **Early Exit:** All new functions (`av_sync_ring_cursor_init`, `av_sync_ring_read`) use guard clauses at the top.
- **Parse Don't Validate:** The read API parses available sample count from the atomic cursor, then performs a single wrap-aware copy.
- **Atomic Predictability:** `av_sync_ring_read` is a pure function of `ring`, `cursor`, and `n`; the only side effect is advancing `cursor->pos`.
- **Fail Fast:** Invalid parameters return 0 immediately; lapped cursor silently advances to oldest valid rather than returning garbage.
- **Intentional Naming:** `cursor->pos`, `oldest`, `available`, `to_copy`, `read_pos`, `first_chunk` read as an English sentence.

## Next Steps

- Phase 3 Plan 4: Rate-limited oversize-chunk warning log + ring-buffer stress test.
