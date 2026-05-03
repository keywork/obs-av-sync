# Phase 3: RT-Thread Safety & Ring Hardening — Research

## Executive Summary

Phase 3 eliminates a real-time-thread heap allocation bug (ring created inside the audio callback) and upgrades the ring buffer to a correct lock-free SPSC design with a window-read API. All four plans are straightforward and well-bounded. The most critical subtlety is that C11 `<stdatomic.h>` on MSVC requires an explicit `/std:c11` or `/std:c17` flag—not present in the current `CMakeLists.txt`—and the CMake template's `compilerconfig` may or may not set this. The planner must verify this and add it explicitly. ThreadSanitizer is only available on Linux/macOS; Windows CI should rely on code inspection + the logic proof provided below.

---

## 1. C11 SPSC Atomic Pattern

### Canonical SPSC Design

The design uses a single atomic write cursor (`total_written`) and a non-atomic reader cursor owned by the consumer. This is the minimal correct SPSC pattern from the literature (Lamport 1977, modernized for C11).

**Key invariant:** `total_written` counts the total number of samples ever appended, monotonically increasing. Physical ring position = `total_written % capacity`.

```
Producer (audio thread):
  1. Compute write_pos = total_written % capacity
  2. memcpy into ring->samples[write_pos]
  3. atomic_store_explicit(&ring->total_written, new_total, memory_order_release)

Consumer (analysis thread):
  1. size_t tw = atomic_load_explicit(&ring->total_written, memory_order_acquire)
  2. Use tw to determine available samples relative to cursor->pos
  3. Read from ring->samples[] (safe because release-before-acquire guarantees
     all sample bytes written before the store are visible after the load)
```

### Why `memory_order_release` / `memory_order_acquire`?

- `memory_order_release` on the store: all previous writes (the `memcpy` into `samples[]`) are guaranteed to be visible to any thread that subsequently performs an acquire-load of the same atomic.
- `memory_order_acquire` on the load: establishes the happens-before edge with the producer's release, making the sample data visible.
- `memory_order_relaxed` would be wrong: the compiler/CPU could reorder the `memcpy` to appear after the atomic store, so the consumer could observe the new `total_written` but stale sample bytes.
- `memory_order_seq_cst` would be correct but unnecessarily expensive (generates a full memory fence on x86/ARM).

### Type: `_Atomic size_t` vs `_Atomic uint64_t`

Use `_Atomic size_t`. The existing `total_written` field is `uint64_t` but a `size_t` atomic is sufficient because `total_written` is used purely as a ring position index (values wrap modulo capacity). On 64-bit platforms `size_t` is 64-bit. On 32-bit platforms `size_t` is 32-bit, which could theoretically overflow after ~4 billion samples, but at 48 kHz that is ~25 hours—acceptable for a live production context. Using `size_t` avoids a dependency on non-standard `_Atomic uint64_t` support width issues on unusual platforms.

**Struct change:**
```c
struct av_sync_ring {
    float          *samples;
    size_t          capacity;
    uint32_t        sample_rate;
    _Atomic size_t  total_written;   /* was: uint64_t total_written */
    uint64_t        newest_timestamp_ns;
    uint64_t        oldest_timestamp_ns;  /* NEW — maintained on write */
};
```

The `get_stats` function reads `total_written` with `memory_order_relaxed` (stats are informational, not used for ring-position decisions by the consumer).

---

## 2. MSVC `<stdatomic.h>` Compatibility

### Version Requirements

`<stdatomic.h>` was added to MSVC in **Visual Studio 2019 version 16.8** (released November 2020), under `/std:c11` or `/std:c17`. It is **not** available under the default `/std:c14` (MSVC's default for C). The current `CMakeLists.txt` does not set any `/std:c` flag explicitly—it relies on the template's `compilerconfig` include.

### What `cmake/common/compilerconfig.cmake` likely does

The obs-plugintemplate sets C standard via `set_property(TARGET ... PROPERTY C_STANDARD 17)` in its `compilerconfig.cmake`, which translates to `/std:c17` on MSVC. **If this is present, `<stdatomic.h>` is already enabled.** The planner should verify by checking `cmake/common/compilerconfig.cmake` for a `C_STANDARD` property set. If it is set to 11 or 17, no change is needed. If it is absent, add:

```cmake
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY C_STANDARD 11)
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY C_STANDARD_REQUIRED ON)
```

### Known MSVC Limitations

1. **`_Atomic` as a qualifier vs. `_Atomic(T)` as a type specifier** — MSVC supports both forms since VS 2019 16.8. Use `_Atomic size_t` (qualifier form), which is the most portable syntax.
2. **`atomic_init()` not always available** — use `= 0` initializer in the struct bzalloc'd to zero (bzalloc zeroes memory, so `_Atomic size_t total_written` is correctly initialized to zero without a separate `atomic_init` call).
3. **VLAs** — unrelated but MSVC doesn't support VLAs; don't use them.
4. **Pragma to suppress MSVC warning C4996** — not needed for `<stdatomic.h>`.
5. **`stdatomic.h` on Windows with MinGW** — the CI uses MSVC (not MinGW), so no issue.

### Recommended CMake Change

Add to `CMakeLists.txt` after the `target_sources(...)` line:

```cmake
# Require C11 for <stdatomic.h> — MSVC needs /std:c11 or /std:c17 explicitly
# (obs-plugintemplate's compilerconfig sets C_STANDARD 17 globally; this is a belt-and-suspenders guard)
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY C_STANDARD 11)
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY C_STANDARD_REQUIRED ON)
```

If `compilerconfig.cmake` already sets `C_STANDARD 17`, this is a no-op (17 > 11, requirement satisfied). If it doesn't set it, this fixes the gap.

---

## 3. `av_sync_ring_read` Design

### Function Signatures (confirmed from CONTEXT.md D-03/D-04)

```c
typedef struct { size_t pos; } av_sync_ring_cursor_t;

/* Set cursor to the oldest valid sample position currently in the ring. */
void av_sync_ring_cursor_init(const av_sync_ring_t *ring,
                               av_sync_ring_cursor_t *cursor);

/* Copy up to n samples into out[], advancing cursor.pos.
   If the cursor has been lapped (overwritten), advance it to oldest valid.
   Returns actual samples copied (may be < n if ring has fewer valid samples). */
size_t av_sync_ring_read(const av_sync_ring_t *ring,
                          av_sync_ring_cursor_t *cursor,
                          float *out, size_t n);
```

### Exact Arithmetic

Let `tw = atomic_load_explicit(&ring->total_written, memory_order_acquire)`.

**Available samples:**
```
size_t filled   = (tw < ring->capacity) ? tw : ring->capacity;
size_t oldest   = (tw >= ring->capacity) ? (tw - ring->capacity) : 0;
```
`oldest` = absolute index of the oldest valid sample (inclusive).
`tw` = absolute index one past the newest sample.

**Lapped cursor detection:**
```
if (cursor->pos < oldest) {
    cursor->pos = oldest;   /* skip forward to oldest valid */
}
```
This is the "skip on overrun" policy: silently advance rather than returning stale/garbage data.

**Available from cursor:**
```
size_t available = tw - cursor->pos;   /* both are absolute indices */
size_t to_copy   = (available < n) ? available : n;
```

**Wrap-around `memcpy`:**
```c
size_t read_pos  = cursor->pos % ring->capacity;
size_t end_pos   = (cursor->pos + to_copy) % ring->capacity;  /* exclusive */

if (read_pos + to_copy <= ring->capacity) {
    /* Contiguous — single copy */
    memcpy(out, ring->samples + read_pos, to_copy * sizeof(float));
} else {
    /* Wraps — two copies */
    size_t first_chunk = ring->capacity - read_pos;
    memcpy(out,               ring->samples + read_pos, first_chunk * sizeof(float));
    memcpy(out + first_chunk, ring->samples,            (to_copy - first_chunk) * sizeof(float));
}
cursor->pos += to_copy;
return to_copy;
```

**`av_sync_ring_cursor_init` implementation:**
```c
void av_sync_ring_cursor_init(const av_sync_ring_t *ring, av_sync_ring_cursor_t *cursor) {
    if (!ring || !cursor) return;
    size_t tw = atomic_load_explicit(&ring->total_written, memory_order_acquire);
    cursor->pos = (tw >= ring->capacity) ? (tw - ring->capacity) : 0;
}
```

### Thread-Safety Proof

- `ring->samples[]` is only written by the producer (audio thread).
- The consumer reads `total_written` with `memory_order_acquire` — this provides a happens-before edge with the producer's `memory_order_release` store, guaranteeing all `samples[]` bytes written before that store are visible to the consumer.
- No torn reads of `total_written` because it is `_Atomic size_t` (atomic reads are indivisible).
- The consumer's `cursor->pos` is never touched by the producer — no sharing.
- The only correctness assumption is SPSC: exactly one producer, one consumer. The caller-owned cursor model enforces this structurally (each consumer owns its own cursor).

### Edge Cases

| Case | Handling |
|------|----------|
| Ring empty (`tw == 0`) | `available = 0`, return 0, cursor stays at 0 |
| `n > available` | Copy only `available` samples, return `available` |
| `n > capacity` | After lapped-skip, `available` is at most `capacity`; same path |
| `cursor->pos == tw` | `available = 0`, no copy, return 0 |

---

## 4. `oldest_timestamp_ns` Update Logic

### Goal

Maintain `oldest_timestamp_ns` in `O(1)` on every write so the analysis thread can query the time-span of ring contents without division.

### Current State

`get_stats` currently computes `oldest_timestamp_ns` via division on every call. Phase 3 moves this into the struct, maintained eagerly on write.

### Formula

On each `av_sync_ring_write(r, samples, n, timestamp_ns)`:

```
tw_before = r->total_written   (read before atomic store; same thread, no atomic needed here)
tw_after  = tw_before + n

/* newest_timestamp_ns = timestamp of last sample in this chunk */
r->newest_timestamp_ns = timestamp_ns + (uint64_t)(((double)(n - 1) * 1.0e9) / (double)r->sample_rate);

/* oldest_timestamp_ns update */
if (tw_after <= r->capacity) {
    /* Ring not yet full — oldest is the first sample ever written. */
    /* First write only: set oldest to timestamp_ns (this chunk starts at absolute 0). */
    if (tw_before == 0) {
        r->oldest_timestamp_ns = timestamp_ns;
    }
    /* Otherwise leave oldest_timestamp_ns unchanged — the ring is growing and
       the oldest sample (from the first write) hasn't been overwritten yet. */
} else {
    /* Ring is full or just became full — oldest = newest_of_oldest_slot */
    /* The ring holds exactly `capacity` samples ending at newest_timestamp_ns. */
    uint64_t span_ns = (uint64_t)(((double)(r->capacity - 1) * 1.0e9) / (double)r->sample_rate);
    r->oldest_timestamp_ns = (r->newest_timestamp_ns > span_ns)
                             ? r->newest_timestamp_ns - span_ns
                             : 0;
}
```

### Transition Point

- Phase 1 (ring filling): `tw_after <= capacity`. Only the very first write (`tw_before == 0`) sets `oldest_timestamp_ns`. Subsequent writes before the ring is full leave it unchanged because `oldest` is still the first sample.
- Phase 2 (ring full): `tw_after > capacity`. Every write updates `oldest_timestamp_ns` to track the rolling window. The formula `newest_ns - span_of_(capacity-1)_samples_ns` gives the timestamp of the oldest sample in the ring.

### Why `(capacity - 1)` in span?

A ring holding `N` samples spans from sample 0 to sample N-1. The time between first and last sample is `(N-1) / sample_rate` seconds. Using `N` would overshoot by one sample period.

### `get_stats` Simplification

After this change, `get_stats` reads `oldest_timestamp_ns` directly from the struct — remove the division logic that currently recomputes it:

```c
out->oldest_timestamp_ns = r->oldest_timestamp_ns;
```

---

## 5. Downmix Buffer Sizing

### OBS Audio Chunk Size

OBS processes audio in fixed-size chunks. The canonical constant is:

```c
/* From OBS source: media-io/audio-io.h */
#define AUDIO_OUTPUT_FRAMES 1024
```

At 48 kHz, `1024 / 48000 ≈ 21.3 ms` per callback. The existing comment in `av_sync_filter.c` says "~10 ms (~480 frames)" which corresponds to a different internal path; the filter callback (`filter_audio`) receives chunks from the pipeline which may vary. The existing `AV_SYNC_DOWNMIX_SCRATCH 2048` is already ~42 ms at 48 kHz and was observed to be adequate in practice (Phase 2 testing showed no `oversize_skips`).

### Recommended Heap Buffer Size

Decision D-06 specifies: `sample_rate * AV_SYNC_MAX_CHUNK_S` where `AV_SYNC_MAX_CHUNK_S` is a compile-time constant.

**Recommendation: `AV_SYNC_MAX_CHUNK_S` = 1 second = `sample_rate` samples at 48 kHz = 48,000 floats = 192 KB.**

Rationale:
- OBS audio chunks are never larger than `AUDIO_OUTPUT_FRAMES` (1024) in normal operation. A 1-second buffer (48,000 samples) provides 46× headroom against the nominal chunk size.
- 192 KB heap per filter instance is negligible (a live production with 4 cameras = 768 KB total).
- Setting `AV_SYNC_MAX_CHUNK_S = 0.5` would give 24,000 samples — still 23× headroom. Either is fine; 1 second is simpler to reason about.
- The `oversize_skips` counter and warning remain as defense-in-depth. Any chunk larger than the downmix buffer is still skipped with a logged warning.

**Implementation at create time:**

```c
/* In av_sync_filter_create: */
#define AV_SYNC_MAX_CHUNK_S 1   /* 1-second downmix scratch (heap, per filter) */

struct obs_audio_info oai;
uint32_t sample_rate = obs_get_audio_info(&oai) ? oai.samples_per_sec : 48000;
data->sample_rate     = sample_rate;
data->downmix_scratch = bzalloc((size_t)sample_rate * AV_SYNC_MAX_CHUNK_S * sizeof(float));
data->downmix_capacity = (size_t)sample_rate * AV_SYNC_MAX_CHUNK_S;
data->ring = av_sync_ring_create((size_t)sample_rate * AV_SYNC_RING_SECONDS, sample_rate);
```

`downmix_scratch` is freed in `av_sync_filter_destroy` with `bfree`.

---

## 6. ThreadSanitizer in CMake

### Platform Availability

| Platform | TSan Available | Notes |
|----------|----------------|-------|
| Linux    | Yes            | GCC and Clang both support `-fsanitize=thread` |
| macOS    | Yes            | Apple Clang supports `-fsanitize=thread` |
| Windows  | No             | MSVC has no TSan equivalent; AddressSanitizer is available but not TSan |

### CMake Integration

TSan must be gated on non-Windows platforms and the compiler's support. Pattern:

```cmake
# In CMakeLists.txt — add after the existing target_sources line:
option(ENABLE_TSAN "Enable ThreadSanitizer for testing (Linux/macOS only)" OFF)

if(ENABLE_TSAN)
  if(WIN32)
    message(WARNING "ThreadSanitizer is not available on Windows; ENABLE_TSAN ignored.")
  else()
    # Verify compiler support
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-fsanitize=thread" COMPILER_SUPPORTS_TSAN)
    if(COMPILER_SUPPORTS_TSAN)
      target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
      target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -fsanitize=thread)
      # Standalone test binary (see §7) needs the same flags
    else()
      message(WARNING "Compiler does not support -fsanitize=thread")
    endif()
  endif()
endif()
```

**For the test executable specifically**, TSan flags should be applied to the test target, not the plugin module (plugin module is a shared library loaded by OBS; TSan in a loadable module requires OBS itself to be linked with TSan, which is impractical). The standalone test binary is the correct place for TSan.

### CTest Integration

```cmake
enable_testing()
add_subdirectory(tests)
```

In `tests/CMakeLists.txt`:
```cmake
add_executable(test_ring_spsc test_ring_spsc.c)
target_include_directories(test_ring_spsc PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_ring_spsc PRIVATE pthread)   # Linux/macOS

if(ENABLE_TSAN AND NOT WIN32 AND COMPILER_SUPPORTS_TSAN)
  target_compile_options(test_ring_spsc PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(test_ring_spsc PRIVATE -fsanitize=thread)
endif()

add_test(NAME spsc_round_trip COMMAND test_ring_spsc)
```

---

## 7. Test Structure

### Minimal SPSC Round-Trip Test

The test must link only against the ring buffer source files — it cannot link against OBS (not available in a unit-test context). This requires either:
a. A test-mode `bzalloc`/`bfree` shim that maps to stdlib `calloc`/`free`, or
b. Compiling `ring_buffer.c` with `bzalloc`/`bfree` stubbed.

**Recommended approach:** `tests/test_ring_spsc.c` includes a shim header before including ring_buffer.h:

```c
/* tests/obs_shim.h — minimal stubs so ring_buffer.c compiles without OBS */
#include <stdlib.h>
#include <string.h>
#define bzalloc(n)   calloc(1, (n))
#define bfree(p)     free(p)
/* obs_log is used in ring_buffer.c? If not, skip. If yes: */
/* #define obs_log(level, ...) (void)0 */
```

The test then:
1. Creates a ring (e.g., 4800-sample capacity at 48000 Hz).
2. Spawns a writer thread that writes 1000 chunks of 480 samples each (10 million samples total, cycling through the ring ~208 times).
3. The main thread (consumer) uses `av_sync_ring_cursor_init` and calls `av_sync_ring_read` in a loop, reading 960-sample windows.
4. After join, verifies: (a) no crash, (b) `total_written` matches expected, (c) final read returns correct data.

**TSan will flag any data race on `ring->samples[]` if the acquire/release ordering is wrong or missing.**

```c
/* Skeleton: tests/test_ring_spsc.c */
#include "obs_shim.h"
#include "../src/ring_buffer.h"
#include <pthread.h>
#include <assert.h>
#include <stdio.h>

#define CAPACITY    4800
#define SAMPLE_RATE 48000
#define CHUNKS      1000
#define CHUNK_SIZE  480

static av_sync_ring_t *g_ring;

static void *writer_thread(void *arg) {
    float buf[CHUNK_SIZE];
    for (int i = 0; i < CHUNKS; i++) {
        for (int j = 0; j < CHUNK_SIZE; j++) buf[j] = (float)(i * CHUNK_SIZE + j);
        av_sync_ring_write(g_ring, buf, CHUNK_SIZE, (uint64_t)i * 10000000ULL);
    }
    return NULL;
}

int main(void) {
    g_ring = av_sync_ring_create(CAPACITY, SAMPLE_RATE);
    assert(g_ring);

    pthread_t writer;
    pthread_create(&writer, NULL, writer_thread, NULL);

    av_sync_ring_cursor_t cursor;
    av_sync_ring_cursor_init(g_ring, &cursor);
    float out[CAPACITY];
    size_t total_read = 0;
    while (total_read < (size_t)CHUNKS * CHUNK_SIZE) {
        size_t got = av_sync_ring_read(g_ring, &cursor, out, 960);
        total_read += got;
    }

    pthread_join(writer, NULL);

    av_sync_ring_stats_t stats;
    av_sync_ring_get_stats(g_ring, &stats);
    assert(stats.total_written == (uint64_t)CHUNKS * CHUNK_SIZE);
    av_sync_ring_destroy(g_ring);

    printf("PASS: spsc_round_trip (%zu samples verified)\n", total_read);
    return 0;
}
```

### CTest Wiring

Run with: `ctest --test-dir build_x86_64 -R spsc_round_trip -V`

On Linux CI, also run with: `cmake --preset ubuntu-x86_64 -DENABLE_TSAN=ON && ctest ...`

---

## Validation Architecture

| Success Criterion | Verification Method |
|-------------------|---------------------|
| 1. No heap allocation in audio callback | Code inspection: confirm `av_sync_ring_create` and `bzalloc` for downmix buffer are only called in `av_sync_filter_create`, not in `av_sync_filter_audio`. Also: search for `bzalloc`/`bfree`/`malloc`/`calloc` in `av_sync_filter_audio` body — must return zero results. |
| 2. SPSC read/write round-trip under TSan | `tests/test_ring_spsc.c` compiled with `-fsanitize=thread` on Linux/macOS CI. TSan must exit clean (no data-race reports). On Windows: logic proof that acquire/release ordering is correct (documented above in §1). |
| 3. `av_sync_ring_read` returns correct window with concurrent writes | Same `test_ring_spsc.c` test: writer writes 1000 chunks; consumer reads concurrently; after join, assert no crash and `total_written` matches expectation. Extend test to verify last-read window contains the expected sample values. |
| 4. Oversize chunk emits warning within 1 s | Write a test that calls `av_sync_filter_audio` with a mock `obs_audio_data` with `frames > downmix_capacity`, then confirms `oversize_skips > 0` and that the log was emitted. Since the warning is rate-limited to 5 s, testing timing exactly is fragile; instead, verify the condition: oversize chunk increments `oversize_skips`, and a 5 s elapsed log window would emit the warning (code path test, not timing test). |

---

## Implementation Order

| Order | Plan | Dependency Note |
|-------|------|-----------------|
| 1st | **Plan 1: Move ring allocation to `av_sync_filter_create`** | Standalone — fixes the bug immediately. Do this before touching the ring internals so the filter works correctly with the existing (non-atomic) ring while the atomic upgrade is in progress. |
| 2nd | **Plan 2: Upgrade ring buffer to SPSC atomics** | Depends on Plan 1 only logically (the ring must exist before being read). Can be implemented simultaneously but easier to verify in sequence. Changes `ring_buffer.c` and `ring_buffer.h`. |
| 3rd | **Plan 3: Add `av_sync_ring_read` API + `oldest_timestamp_ns`** | Depends on Plan 2 (needs `_Atomic size_t total_written` to be present for the acquire-load in `av_sync_ring_read`). Adds cursor typedef and two new functions to `ring_buffer.h`/`.c`. |
| 4th | **Plan 4: Oversize-chunk warning log** | Depends on Plan 1 (downmix buffer is now heap with `downmix_capacity` field). Entirely in `av_sync_filter.c`. Low risk, implement last. |

---

## Risk Register

| Risk | Severity | Mitigation |
|------|----------|------------|
| **MSVC `<stdatomic.h>` not enabled by default** — if `cmake/common/compilerconfig.cmake` does not set `C_STANDARD 17`, Windows CI will fail to compile with a confusing error about `_Atomic`. | High | Add explicit `set_property(TARGET ... PROPERTY C_STANDARD 11)` as a belt-and-suspenders guard. Check `compilerconfig.cmake` first. |
| **`ring_buffer.c` test shim for `bzalloc`** — OBS's `bzalloc` is not available in a standalone test binary. If the shim is not set up before compiling the test, linker errors will occur. | Medium | Define `bzalloc`/`bfree` as `calloc`/`free` macros in a test-only shim header; include it before `ring_buffer.h` in the test file. |
| **`oldest_timestamp_ns` staleness during ring-filling phase** — if only the first write sets `oldest_timestamp_ns` and subsequent pre-full writes don't update it, the value remains correct (oldest sample hasn't moved). But if a writer skips updating on the transition write (first write that makes `tw_after == capacity` exactly), there's an off-by-one. | Low | Use `tw_after > capacity` (strictly greater) for the "ring is full" branch. The transition write where `tw_after == capacity` exactly is the last write of the filling phase — oldest is still the first sample, so no update needed. Transition to rolling update happens on the first write where `tw_after > capacity`. |
| **`_Atomic size_t` with large `total_written` on 32-bit** — at 48 kHz, `size_t` wraps after ~25 hours on 32-bit. Ring position modulo arithmetic must handle wrap correctly. | Low | `total_written % capacity` is well-defined even after wrap (both are `size_t`). The consumer's `cursor->pos` also wraps, but since it's always ≤ `total_written`, the difference `tw - cursor->pos` (both `size_t`) gives the correct unsigned count. No action needed beyond documenting the 25-hour assumption. |
| **TSan false negatives on x86** — TSan on x86 may not catch all races because x86's strong memory model satisfies many ordering requirements implicitly. The code should be verified for correctness on ARM (which has a weaker memory model) via code review. | Low | The acquire/release pattern in §1 is correct for C11's abstract machine, which is weaker than x86. ARM CI (if available) provides additional coverage. |
| **`obs_get_audio_info` returning false at create time** — OBS may not have initialized audio output before the first filter is created (e.g., during OBS startup). The fallback to 48000 Hz is already established in Phase 2 and must be carried forward. | Low | Already handled: `data->sample_rate = obs_get_audio_info(&oai) ? oai.samples_per_sec : 48000`. Use the same pattern for ring and downmix buffer sizing. |
| **Thread spawning for tests on Windows** — `pthread.h` is not available on Windows. The SPSC test uses `pthread_create`. | Medium | Use `_beginthreadex` on Windows or conditionally include `<threads.h>` (C11, available on MSVC with `/std:c11`). Alternatively, use CMake to skip the test on Windows and rely on code inspection there. |
