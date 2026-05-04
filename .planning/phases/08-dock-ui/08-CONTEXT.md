# Phase 8: Dock UI - Context

**Gathered:** 2026-05-04
**Status:** Ready for planning

## Phase Boundary

An OBS dock panel shows all tracked camera sources with their current sync status, detected offsets, and correction state — always visible without blocking the scene editor. The dock is read-only; all configuration remains in the per-source filter properties panel built in Phase 5.

Requirements: UI-01, UI-02, UI-03, UI-04

## Implementation Decisions

### Qt Infrastructure
- **D-01:** Qt6 (required by OBS 30+). Set `ENABLE_QT=ON` in `CMakeLists.txt`. New C++ source files: `src/av_sync_dock.cpp` and `src/av_sync_dock.h`.
- **D-02:** Register the dock in `obs_module_load` via `obs_frontend_add_dock_by_id` with a unique identifier string (e.g., `"obs-av-sync-dock"`). Save the returned `QDockWidget*` pointer. Remove/destroy the dock in `obs_module_unload`.
- **D-03:** Use `AUTOMOC ON` in CMake (already configured in template when `ENABLE_QT=ON`). No `.ui` files — build the table layout programmatically in C++.

### Dock Type and Layout
- **D-04:** `QDockWidget` subclass containing a `QTableWidget`. The table is read-only (`setEditTriggers(QAbstractItemView::NoEditTriggers)`). No inline editing, no buttons, no configuration controls.
- **D-05:** Table columns (left to right): **Source Name** | **Status** | **Offset (ms)** | **Confidence** | **Reference**
  - Source Name: `obs_source_get_name` of the filter's parent source.
  - Status: textual label + color-coded indicator.
  - Offset (ms): smoothed offset from the filter's smoother state.
  - Confidence: last raw confidence from GCC-PHAT.
  - Reference: the filter's `reference_source_name` (or "(none)").
- **D-06:** Minimum table height shows ~4 rows (typical production setup: 2–4 cameras). Table scrolls vertically if more filters are added.
- **D-07:** Dock window title: "AV Sync Status" (translatable via `obs_module_text`).

### Data Sharing and Thread Safety
- **D-08:** The UI thread reads filter state directly from each `struct av_sync_filter_data` instance. The following fields must be made UI-thread-safe (analysis thread writes, UI thread reads):
  - `_Atomic int status` — already exists (0=Measuring, 1=Synced, 2=Out of Range).
  - `_Atomic bool sync_enabled` — already exists.
  - `_Atomic float smoothed_offset_ms` — **must be added** to the filter struct; analysis thread stores with `atomic_store`, UI thread reads with `atomic_load`.
  - `_Atomic float last_confidence` — **must be added** to the filter struct; same atomic access pattern.
- **D-09:** Active filter instances are tracked via a global linked list (or simple array) protected by a `pthread_mutex_t` (or `std::mutex` in C++ wrapper). The list is maintained in `av_sync_filter_create` (add) and `av_sync_filter_destroy` (remove). The dock UI timer callback locks this mutex briefly to copy the instance list before reading individual filter states.
- **D-10:** The filter's `reference_source_name` field is UI-thread-only (set in `av_sync_filter_update`, read in destroy and now in dock). No additional synchronization needed for this field because both writer and reader run on the OBS main/UI thread.

### Update Mechanism
- **D-11:** Timer-based polling using `QTimer` inside the dock widget. Interval: **500 ms** (2 Hz).
- **D-12:** On each timer tick:
  1. Lock the global filter instance mutex.
  2. Copy the list of `struct av_sync_filter_data*` pointers.
  3. Unlock the mutex.
  4. For each instance, read atomic fields (`status`, `sync_enabled`, `smoothed_offset_ms`, `last_confidence`) and the parent source name.
  5. Rebuild or update `QTableWidget` rows to match the current instance list.
- **D-13:** Do NOT attempt to lock or block on any filter-internal mutex during the audio callback path. The global instance list mutex is only touched on filter create/destroy (rare) and UI timer (2 Hz), so contention is negligible.

### Status Display
- **D-14:** Color-coded status indicators:
  - **Measuring** (status == 0): yellow/amber — the smoother has not yet accumulated 3 valid measurements.
  - **Synced** (status == 1): green — ≥3 valid measurements and residual offset within range.
  - **Out of Range** (status == 2): red — confidence below threshold OR offset magnitude exceeds limits.
  - **Disabled** (sync_enabled == false): gray — tracking is disabled for this source; overrides the color regardless of the underlying `status` value.
- **D-15:** Status text in the table matches the color state: "Measuring", "Synced", "Out of Range", or "Disabled".
- **D-16:** Use `QTableWidgetItem::setForeground` with `QBrush(QColor(...))` for color coding. Choose colors that are accessible in both OBS light and dark themes (e.g., Qt's `QPalette` or hardcoded RGB values that work on dark backgrounds since OBS default is dark).

### Empty State
- **D-17:** When zero filter instances are active, the dock shows a single-row placeholder message: "No cameras are being tracked. Add the AV Sync filter to a source to begin." (translatable). The table headers remain visible.

### Lifecycle and Resilience
- **D-18:** The dock must survive source add, remove, and rename without crashing. The timer callback must validate that each filter pointer is still valid before dereferencing. Because the list is copied under the mutex, and filters are only removed after the analysis thread is joined in `av_sync_filter_destroy`, a simple pointer check against NULL is sufficient.
- **D-19:** The dock must not steal focus (`Qt::NoFocus` policy on the table widget) and must not block the OBS scene editor. Standard `QDockWidget` behavior satisfies this.
- **D-20:** If a source is renamed in OBS, the dock will pick up the new name on the next timer tick because `obs_source_get_name` is called fresh each poll.

### Claude's Discretion
- Exact RGB color values for the four status colors — choose standard, accessible colors.
- Whether to use `QTableWidget` with individual `QTableWidgetItem`s vs a `QAbstractTableModel` + `QTableView` — `QTableWidget` is simpler for a read-only, small dataset (≤10 rows).
- Whether to sort the table by source name or preserve creation order — default to alphabetical by source name for stability.
- Whether to show a small "live" dot/pulse animation next to the dock title — not required; static updates at 2 Hz are sufficient.

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements and Roadmap
- `.planning/REQUIREMENTS.md` §UI-01, UI-02, UI-03, UI-04 — Dock UI requirements
- `.planning/ROADMAP.md` §Phase 8 — Phase goal, plans, and success criteria
- `.planning/PROJECT.md` — Project vision and principles
- `docs/ARCHITECTURE.md` — Data flow and OBS API touchpoints

### Existing Code (MUST read before planning)
- `src/av_sync_filter.c` — Filter struct with `_Atomic int status`, `_Atomic bool sync_enabled`, and smoother state fields
- `src/av_sync_filter.h` — Filter registration (`av_sync_register_filter`)
- `src/smoother.h` — Status value mapping: `smoother_get_status()` returns 0=Measuring, 1=Synced, 2=Out of Range
- `src/plugin-main.c` — Module entry point (`obs_module_load` / `obs_module_unload`)
- `CMakeLists.txt` — `ENABLE_FRONTEND_API=ON`, `ENABLE_QT=OFF` (must be changed to ON), Qt6 configuration

### Prior Phase Context
- `.planning/phases/05-reference-tap-source-config/05-CONTEXT.md` — Filter properties UI, `obs_data_t` serialization, reference source selection
- `.planning/phases/06-continuous-sync-engine/06-CONTEXT.md` — Analysis thread, smoother status states, atomic status field design

## Existing Code Insights

### Reusable Assets
- `_Atomic int status` and `_Atomic bool sync_enabled` in `struct av_sync_filter_data` — already atomic, UI thread can read directly.
- `smoother_get_status()` — maps smoother state to the three status values (0/1/2) that the dock will display.
- `reference_tap_get_ring()` and `reference_tap_get_sample_rate()` — reference tap globals (not directly used by dock, but relevant for context).

### Established Patterns
- C11 atomics for cross-thread data sharing (`_Atomic` fields in filter struct).
- Per-filter `bzalloc` / `bfree` lifecycle managed by OBS source system.
- `obs_log` for diagnostic output.
- `obs_module_text` / `obs_module_get_string` for translatable strings (`data/locale/en-US.ini`).

### Integration Points
- `obs_module_load` / `obs_module_unload` — dock widget creation and destruction hooks.
- `av_sync_filter_create` / `av_sync_filter_destroy` — add/remove filter instances from the global list tracked by the dock.
- `obs_frontend_add_dock_by_id` — OBS frontend API for registering a QDockWidget.
- `obs_enum_sources` / `obs_source_get_name` — for resolving source names (alternative to tracking in filter struct).

## Specific Ideas

- No specific visual references — standard OBS dock appearance is acceptable.
- The dock should feel like OBS's built-in "Stats" dock: clean table, no chrome, purely informational.

## Deferred Ideas

- Configurable EMA alpha or slew-rate cap via the dock UI — deferred to v2; configuration stays in filter properties for v1.
- Global plugin settings panel for default reference source — deferred from Phase 5, still not in scope.
- Per-scene reference switching — deferred from Phase 6.
- Manual approve-before-apply mode — out of scope per PROJECT.md.

---

*Phase: 08-dock-ui*
*Context gathered: 2026-05-04*
