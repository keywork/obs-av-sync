# Code Conventions

## Naming Conventions

### Functions
- All plugin-specific functions use the `av_sync_` prefix: `av_sync_register_filter()`, `av_sync_ring_create()`, `av_sync_ring_destroy()`, `av_sync_ring_write()`, `av_sync_ring_get_stats()`
- OBS callback functions registered in `obs_source_info` use the `av_sync_filter_` prefix: `av_sync_filter_get_name()`, `av_sync_filter_create()`, `av_sync_filter_destroy()`, `av_sync_filter_audio()`
- Static (file-local) functions are named with the full `av_sync_filter_` prefix; public API functions are non-static and declared in headers
- Function names are `snake_case` throughout

### Structs and Types
- Internal (opaque) structs declared in `.c` files only: `struct av_sync_ring`, `struct av_sync_filter_data`
- Public-facing types use the `av_sync_` prefix with `_t` suffix for typedefs: `av_sync_ring_t`, `av_sync_ring_stats_t`
- Plain output/stats structs are `typedef struct { ... } av_sync_ring_stats_t` — defined inline in the header
- Opaque handles forward-declared in headers via `typedef struct av_sync_ring av_sync_ring_t;`

### Macros and Constants
- `SCREAMING_SNAKE_CASE` with the `AV_SYNC_` prefix: `AV_SYNC_FILTER_ID`, `AV_SYNC_DIAG_DETAILED_CALLBACKS`, `AV_SYNC_DIAG_LOG_INTERVAL_NS`, `AV_SYNC_RING_SECONDS`, `AV_SYNC_DOWNMIX_SCRATCH`
- Plugin-wide identity constants `PLUGIN_NAME` and `PLUGIN_VERSION` are `extern const char *` from `plugin-support.h`

### Variables
- Local variables: `snake_case`, short but descriptive (`ts`, `gap`, `planes`, `frames`, `inv_planes`, `scratch`, `rs`, `write_pos`, `first_chunk`)
- Struct members: `snake_case` without prefix (`callback_count`, `total_frames`, `sample_rate`, `ring`, `oversize_skips`)
- Pointer-to-data parameter pattern: raw `data_ptr` cast immediately to typed pointer at function top: `struct av_sync_filter_data *data = data_ptr;`

---

## File Organization

### Header / Source Split
- Every module has a paired `.h` / `.c`: `av_sync_filter.h` + `av_sync_filter.c`, `ring_buffer.h` + `ring_buffer.c`
- `plugin-main.c` is the OBS module entry point only — minimal code, no logic
- `plugin-support.h` is a template-provided utility header (logging bridge)

### Include Guards
- `#pragma once` used exclusively — no `#ifndef` include guards

### Include Ordering (observed in `av_sync_filter.c`)
1. OBS system headers: `<obs-module.h>`, `<util/bmem.h>`, `<plugin-support.h>`
2. Standard C headers: `<inttypes.h>`
3. Local project headers: `"av_sync_filter.h"`, `"ring_buffer.h"`

In `ring_buffer.c`:
1. OBS utility: `<util/bmem.h>`
2. Standard C: `<string.h>`
3. Local: `"ring_buffer.h"`

In `ring_buffer.h`:
1. Standard C only: `<stddef.h>`, `<stdint.h>`

### C++ Compatibility
All headers wrap public declarations in `#ifdef __cplusplus extern "C" { ... }` guards to allow future C++ consumers.

---

## C Style

### Brace Style (from `.clang-format`)
- `BreakBeforeBraces: Custom` with `AfterFunction: true` — **function bodies** have the opening brace on a new line (Allman style for functions)
- Control structures (`if`, `for`, `while`) have braces on the same line (`AfterControlStatement: false`)
- No short single-line `if` blocks (`AllowShortIfStatementsOnASingleLine: false`)
- No short single-line loops (`AllowShortLoopsOnASingleLine: false`)

### Indentation
- **Tab width 8**, `UseTab: ForContinuationAndIndentation` — hard tabs for indentation, tabs for continuation
- `IndentWidth: 8`, `ContinuationIndentWidth: 8`
- Column limit: **120 characters**

### Pointer Placement
- `PointerAlignment: Right` — pointer `*` attaches to the variable name: `float *samples`, `av_sync_ring_t *r`, `struct av_sync_filter_data *data`

### Typedef Usage
- Typedef used for opaque handles and output structs; internal `struct` definitions left without typedef inside `.c` files
- `bzalloc` / `bfree` (OBS allocators) used instead of `malloc` / `free` throughout

### Designated Initializers
- `obs_source_info` populated with designated initializers (`.id = ...`, `.type = ...`, etc.)

### Casting
- Explicit C-style casts where needed: `(size_t)`, `(uint64_t)`, `(double)`, `(float)`, `(const float *)`
- `UNUSED_PARAMETER(x)` macro used for unused OBS callback parameters

---

## Comments & Documentation

### File Header
Every `.c` and `.h` file begins with the same GPL-2.0-or-later block comment naming the project, copyright holder, and license URL.

### Inline Comments
- Block comments `/* ... */` used for all inline documentation (C89-compatible style, not `//`)
- Comments explain *why*, not *what*, for non-obvious logic: downmix scratch size rationale, ring write-position arithmetic, timestamp derivation
- Phase markers note when code was introduced and what comes next: `/* Phase 2b: single-writer ... Cross-thread atomics land in Phase 3 ... */`

### API Documentation
- Public functions in headers documented with a leading `/* ... */` block on the declaration where behavior is non-obvious (e.g., `av_sync_ring_write` note about single-producer semantics and oldest-sample overwrite)
- Short/obvious functions (create/destroy) are undocumented beyond naming

---

## CMake Style

### Gersemi Config (`.gersemirc`)
- Line length: **120** (matches `.clang-format`)
- Indent: **2 spaces** for CMake files
- `list_expansion: favour-inlining` — short lists stay on one line

### General
- Template CMake helpers in `cmake/` are not modified
- Plugin-level `CMakeLists.txt` uses `target_sources(${CMAKE_PROJECT_NAME} PRIVATE …)` to add new `.c` / `.cpp` files
- Feature toggles `ENABLE_FRONTEND_API` and `ENABLE_QT` in `CMakeLists.txt` default `OFF` until UI work begins

---

## Error Handling

### Null / Invalid Input
- Guard clauses at the top of every function that could receive NULL or zero values:
  ```c
  if (!r || !samples || n == 0) { return; }
  if (capacity_samples == 0 || sample_rate == 0) { return NULL; }
  if (!audio || audio->frames == 0) { return audio; }
  ```
- Functions return `NULL` on allocation failure (via `bzalloc` which internally asserts on OOM)

### No errno-style Return Codes
- Void functions silently return early on invalid input; callers must check NULL returns from `_create()` functions
- The `av_sync_ring` is allocated lazily (only when `sample_rate > 0` is known) and callers check `if (data->ring && ...)` before use

### OBS API Errors
- `obs_get_audio_info()` return checked; `sample_rate` left 0 if it fails, ring not created

---

## Logging

### Macro / Function
- `obs_log(log_level, format, ...)` — wrapper defined in `plugin-support.h` that prepends `[PLUGIN_NAME]` to every message via the underlying `blogva()` call

### Log Levels Used
- `LOG_INFO` — all current messages (lifecycle events, diagnostics, rollups)
- No `LOG_WARNING`, `LOG_ERROR`, or `LOG_DEBUG` calls yet (expected once error paths are more developed)

### Patterns
- Filter lifecycle: `"filter created on '%s'"` / `"filter destroyed on '%s'"` with parent source name
- First N callbacks logged in detail (controlled by `AV_SYNC_DIAG_DETAILED_CALLBACKS = 5`)
- Rolling 5-second window summary logged via `"passthrough rollup cb=… t=…s frames=… eff=…Hz …"` using `PRIu64` / `PRIu32` format specifiers from `<inttypes.h>`
- Ring stats included in rollup: fill percentage, `total_written`, `oversize_skips`
- Module load/unload: `"plugin loaded successfully (version %s)"` / `"plugin unloaded"`
