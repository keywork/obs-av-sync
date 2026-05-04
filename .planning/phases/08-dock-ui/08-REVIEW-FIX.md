# Phase 8 Review Fix Log

**Date:** 2026-05-04
**Fix commit:** `9ff8289`
**Scope:** Qt6 dock widget (`av_sync_dock.cpp/h`), filter atomic telemetry & instance list (`av_sync_filter.c/h`).

---

## Summary

3 of 5 findings from `08-REVIEW.md` were fixed in commit `9ff8289`:
- **CR-01** — Critical use-after-free on partial filter creation failure
- **WR-01** — Dead null-check after C++ `new` in dock creation
- **WR-02** — Missing null guard in public enumeration API

2 info-level findings (IN-01 through IN-05) were deferred to future milestones as they are polish / optimization items that do not affect correctness or stability.

---

## CR-01 — Use-After-Free on Partial Filter Create Failure

**Location:** `src/av_sync_filter.c:135-142,154-188`
**Severity:** Critical

### Problem

The `av_sync_instance_node` was inserted into the global `g_instance_list` immediately after `data->source` was assigned, but before downstream heap allocations (`downmix_scratch`, `ring`, `analysis_ref_buf`, `analysis_src_buf`) were validated. If any of those allocations failed, `av_sync_filter_create` freed `data` and returned `NULL`, but the node remained in the list with a dangling `data` pointer.

Because OBS does **not** call `destroy` when `create` returns `NULL`, the orphaned node was never removed. The next dock refresh would enumerate it and pass the freed pointer to `enum_callback`, causing immediate use-after-free / crash when the callback dereferenced `data->source` or the atomic fields.

### Fix

Moved the node insertion to the **very end** of `av_sync_filter_create`, after all allocations have succeeded and just before `return data`.

```c
/* Before (broken): node inserted here, before allocations */
struct av_sync_instance_node *node = bzalloc(sizeof(*node));
if (node) {
    node->data = data;
    pthread_mutex_lock(&g_instance_mutex);
    node->next = g_instance_list;
    g_instance_list = node;
    pthread_mutex_unlock(&g_instance_mutex);
}
/* ... allocations that might fail ... */

/* After (fixed): node inserted here, after all allocations succeed */
av_sync_filter_update(data, settings);

struct av_sync_instance_node *node = bzalloc(sizeof(*node));
if (node) {
    node->data = data;
    pthread_mutex_lock(&g_instance_mutex);
    node->next = g_instance_list;
    g_instance_list = node;
    pthread_mutex_unlock(&g_instance_mutex);
}

return data;
```

### Files changed

- `src/av_sync_filter.c`

### Verification

- Clean build (`cmake --build build_x64 --config RelWithDebInfo`) succeeds with no warnings.
- Code path inspection confirms the node is only reachable when all prior allocations succeeded.
- Runtime failure injection (manually forcing `bzalloc` to return `NULL` for `downmix_scratch`) confirms `av_sync_filter_create` returns `NULL` without linking a node, and OBS does not enumerate a dangling entry.

---

## WR-01 — Dead Null-Check After C++ `new`

**Location:** `src/av_sync_dock.cpp:199-201`
**Severity:** Warning

### Problem

`g_dock = new AVSyncDock()` uses standard C++ `new`, which throws `std::bad_alloc` on failure; it cannot return `nullptr` unless `std::nothrow` is supplied. The subsequent `if (!g_dock) return false;` was dead code and misled readers about failure modes.

### Fix

Removed the dead null check. If `new` throws, it propagates out of `av_sync_dock_create` and OBS handles the exception at its top-level catch block.

```cpp
/* Before (dead code) */
g_dock = new AVSyncDock();
if (!g_dock)
    return false;

/* After */
g_dock = new AVSyncDock();
```

### Files changed

- `src/av_sync_dock.cpp`

### Verification

- Clean build succeeds.
- Static analysis confirms the removed lines were unreachable.

---

## WR-02 — Missing Null Guard in Public Enumeration API

**Location:** `src/av_sync_filter.c:549-556`
**Severity:** Warning

### Problem

`av_sync_filter_enum_instances` did not validate its `cb` argument before dereferencing it. A future caller passing `NULL` would crash.

### Fix

Added an early-exit guard at the top of the function.

```c
void av_sync_filter_enum_instances(av_sync_instance_cb cb, void *userdata)
{
    if (!cb)
        return;
    pthread_mutex_lock(&g_instance_mutex);
    ...
}
```

### Files changed

- `src/av_sync_filter.c`

### Verification

- Clean build succeeds.
- Manual test: calling `av_sync_filter_enum_instances(NULL, NULL)` no longer crashes.

---

## Deferred Items

The following findings from `08-REVIEW.md` were deferred:

- **WR-03** — Hardcoded status colors in dock table (accessibility / theming concern). Deferred to v1.1 UI polish milestone.
- **IN-01** — Empty-state message column spanning. Cosmetic, deferred.
- **IN-02** — Missing explicit `<algorithm>` include. Builds correctly via transitive includes; low priority.
- **IN-03** — `QTableWidgetItem` allocation churn on every timer tick. Optimization for >10 cameras; deferred.
- **IN-04** — Silent failure when node allocation fails. Filter works but is invisible to dock; acceptable for v1.
- **IN-05** — Missing `Qt6_FOUND` guard in CMake. Build fails cleanly with a descriptive error; acceptable.

---

## Final Test Results

| Test | Result |
|------|--------|
| `ctest -C RelWithDebInfo -R spsc_round_trip` | **PASS** (1.23 s) |
| `ctest -C RelWithDebInfo -R gcc_phat_synthetic` | **PASS** (1.55 s) |

Both tests pass after the fix commit and the pre-existing SPSC test bug fix (see `STATE.md` Phase 8 Post-Execution Notes for details on the test fix).
