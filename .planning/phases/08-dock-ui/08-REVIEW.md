# Phase 8 Code Review Report

**Scope:** Qt6 dock widget (`av_sync_dock.cpp/h`), filter atomic telemetry & instance list (`av_sync_filter.c/h`), module entry (`plugin-main.c`), build config (`CMakeLists.txt`), localization (`data/locale/en-US.ini`).

**Date:** 2026-05-04

---

## Summary

Phase 8 introduces a Qt6 dock widget that polls atomic telemetry fields from a mutex-protected linked list of AV sync filter instances. The architecture is sound: atomic reads give the UI lock-free access to smoothed offsets and status, the instance list is consistently protected by `g_instance_mutex`, and all user-visible strings are routed through `obs_module_text`. However, a **critical use-after-free** exists when filter creation fails after the instance node has already been linked into the global list. A handful of defensive-programming gaps and UI polish items should also be addressed before the phase is considered complete.

---

## Findings

### Critical

- **CR-01** (`av_sync_filter.c:135-142,154-188`): The `av_sync_instance_node` is inserted into `g_instance_list` before downstream heap allocations (`downmix_scratch`, `ring`, `analysis_ref_buf`, `analysis_src_buf`) are validated. If any of those allocations fail, `av_sync_filter_create` frees `data` and returns `NULL`, but the node remains in the list with a dangling `data` pointer. Because OBS does **not** call `destroy` when `create` returns `NULL`, the orphaned node is never removed. The next dock refresh will enumerate it and pass the freed pointer to `enum_callback`, causing immediate use-after-free/crash when the callback dereferences `data->source` or the atomic fields.
  → **Fix:** Move the node insertion to the very end of `av_sync_filter_create` (after all allocations succeed and just before `return data`), or explicitly unlink and `bfree(node)` on every failure path.

### Warning

- **WR-01** (`av_sync_dock.cpp:199-201`): `g_dock = new AVSyncDock()` uses standard C++ `new`, which throws `std::bad_alloc` on failure; it cannot return `nullptr` unless `std::nothrow` is supplied. The subsequent `if (!g_dock) return false;` is dead code and misleads readers about failure modes.
  → **Fix:** Either remove the null check, switch to `new (std::nothrow) AVSyncDock()`, or wrap the allocation in `try/catch`.

- **WR-02** (`av_sync_filter.c:549-556`): `av_sync_filter_enum_instances` does not validate its `cb` argument before dereferencing it. While the only current caller passes a valid function pointer, this is a public API surface; a future caller passing `NULL` will crash.
  → **Fix:** Add an early-exit guard: `if (!cb) return;`.

- **WR-03** (`av_sync_dock.cpp:150-165`): Status colors are hardcoded RGB values (Material Design palette). They are legible on OBS's default dark theme, but contrast on light/custom themes is untested, and red/green pairing is problematic for deuteranopia/protanopia users.
  → **Fix:** Consider deriving colors from `QApplication::palette()` (e.g., `QPalette::Highlight` for active, `QPalette::Text` for disabled) or supplementing color with iconography/text weight to indicate status.

### Info

- **IN-01** (`av_sync_dock.cpp:124-136`): The empty-state message (`AVSync.Dock.NoCameras`) is placed only in column 0; columns 1–4 receive blank items. For a cleaner visual presentation, the message cell could span all five columns via `QTableWidgetItem::setData(Qt::UserRole, ...)` with a spanning delegate, or the row could simply be left with a single spanning item.

- **IN-02** (`av_sync_dock.cpp:114-117`): `std::sort` is used, but `<algorithm>` is not explicitly included in `av_sync_dock.cpp`. Compilation currently relies on transitive includes from Qt headers; adding the explicit include improves portability.

- **IN-03** (`av_sync_dock.cpp:138-189`): `populateTable` allocates new `QTableWidgetItem` objects for every cell on every 500 ms timer tick. For typical camera counts (≤10) this is negligible, but if the table grows large it becomes a measurable allocation churn. Consider an in-place update strategy (reuse existing items, only create when row count changes) as a future optimization.

- **IN-04** (`av_sync_filter.c:135-136`): If `bzalloc(sizeof(*node))` fails, `av_sync_filter_create` silently continues without adding the filter to the instance list. The filter will function normally but will be invisible to the dock UI, which could confuse users.
  → **Fix:** Log a warning when the node allocation fails.

- **IN-05** (`CMakeLists.txt:51-62`): `find_package(Qt6 COMPONENTS Widgets Core)` is not guarded by a `Qt6_FOUND` check inside the `if(ENABLE_QT)` block. If Qt6 is absent, CMake will proceed to `target_link_libraries` with non-existent targets and fail at configure time rather than gracefully disabling the UI.
  → **Fix:** Wrap the link and property settings in `if(Qt6_FOUND)`.

---

## Positive Observations

- **Correct atomic telemetry pattern.** `_Atomic` fields (`status`, `sync_enabled`, `smoothed_offset_ms`, `last_confidence`) are written exclusively by the analysis thread and read lock-free by the UI thread. The choice of `memory_order_relaxed` for telemetry is appropriate—stale values for a few milliseconds are harmless for a status display.
- **Thread join before teardown.** `av_sync_filter_destroy` joins the analysis thread and then removes the instance from the list and frees memory. This ordering eliminates an entire class of use-after-free races in the analysis thread.
- **Consistent mutex scope for instance enumeration.** `av_sync_filter_enum_instances` holds `g_instance_mutex` for the entire callback loop, guaranteeing that the snapshot vector sees a consistent view and that no filter can be destroyed mid-enumeration.
- **Complete localization coverage.** Every user-visible string in both the C filter and C++ dock code routes through `obs_module_text`, and all keys have matching entries in `data/locale/en-US.ini`.
- **Safe Qt object lifecycle.** `table_` and `timer_` are parented to `AVSyncDock`, and the timer is explicitly stopped in the destructor before Qt's child-deletion sequence runs. This prevents queued timeout events from touching destroyed state.
- **Proper OBS dock registration.** `obs_frontend_add_dock_by_id` / `obs_frontend_remove_dock` are used with a stable string ID, and the dock object is correctly deleted after removal.
- **Defensive null checks in getters.** `av_sync_filter_get_parent_name`, `av_sync_filter_get_status`, and the other atomic getters all validate `data != NULL` before dereferencing, making the API tolerant of edge cases.

---

## Reviewer Notes

- **Build verification:** Not performed as part of this review. Recommend a clean build with `-DENABLE_QT=ON` on Windows, macOS, and Linux after CR-01 is fixed.
- **Runtime verification:** Recommend adding a temporary failure injection (e.g., force `bzalloc` to return `NULL` for `downmix_scratch`) to confirm that CR-01 is fully resolved and that no other leaks exist on partial-create failure paths.
