# Phase 8: Dock UI - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions captured in `08-CONTEXT.md` — this log preserves the discussion.

**Date:** 2026-05-04
**Phase:** 08-dock-ui
**Mode:** auto (user pre-specified all gray areas)

## Areas Discussed

1. Qt version and infrastructure
2. Dock type and OBS registration
3. Update mechanism (polling rate)
4. Data sharing and thread safety
5. Table columns and layout
6. Status display (color coding)
7. Scope boundaries (read-only vs. configuration)

## Decisions Log

### Qt Version and Infrastructure
- **User decision:** Qt6 (OBS 30+ requirement)
- **Rationale:** OBS 30+ mandates Qt6; Qt5 is deprecated.
- **Implementation impact:** Set `ENABLE_QT=ON` in `CMakeLists.txt`; add `src/av_sync_dock.cpp` and `src/av_sync_dock.h`; use `AUTOMOC`.

### Dock Type
- **User decision:** `QDockWidget` subclass registered via `obs_frontend_add_dock_by_id`
- **Rationale:** Standard OBS dock pattern; survives workspace layouts and OBS restarts.
- **Implementation impact:** Subclass `QDockWidget`, create internally, pass to `obs_frontend_add_dock_by_id` in `obs_module_load`.

### Update Mechanism
- **User decision:** Timer-based (`QTimer` at 2 Hz, i.e., 500 ms interval)
- **Rationale:** Simple, non-blocking, sufficient for a human-readable status panel. No need for push/observer complexity.
- **Implementation impact:** `QTimer` inside dock widget; on timeout, iterate tracked filters and refresh table.
- **Note:** ROADMAP.md originally suggested ~4 Hz; user's 2 Hz decision is locked.

### Data Sharing
- **User decision:** UI thread reads `_Atomic int status` and atomic `sync_enabled` directly from filter structs.
- **Rationale:** Avoids complex marshaling or message passing; leverages existing atomic fields.
- **Expansion during discussion:** For the dock to display **Offset (ms)** and **Confidence** columns, `smoothed_offset_ms` and `last_confidence` must also be made atomic (or otherwise UI-thread-safe). Decision: add `_Atomic float smoothed_offset_ms` and `_Atomic float last_confidence` to `struct av_sync_filter_data`, updated by the analysis thread with `atomic_store` and read by the UI thread with `atomic_load`.
- **Additional derived decision:** A global, mutex-protected linked list of active filter instances is required so the dock can discover which filters exist without scanning all OBS sources on every timer tick.

### Table Columns
- **User decision:** Source Name | Status | Offset (ms) | Confidence | Reference
- **Rationale:** Gives the operator everything needed at a glance.
- **Implementation impact:** Five-column `QTableWidget`.

### Status Display
- **User decision:** Color-coded status:
  - Measuring = yellow/amber
  - Synced = green
  - Out of Range = red
  - Disabled = gray
- **Rationale:** Immediate visual recognition of system health during a live show.
- **Implementation impact:** Set `QTableWidgetItem` foreground brush based on atomic `status` and `sync_enabled` values. Disabled state (sync_enabled == false) takes precedence over computed status.

### Scope
- **User decision:** Read-only monitoring table; configuration stays in filter properties (Phase 5)
- **Rationale:** Keeps Phase 8 focused and avoids duplicating configuration UI. Per-filter properties already handle reference source selection and enable/disable.
- **Implementation impact:** No edit triggers on table; no configuration widgets in dock.

## Auto-Resolved Decisions

The following were not explicitly listed by the user but were derived during analysis to satisfy the stated decisions:

- **Global filter instance list:** A mutex-protected list maintained in `av_sync_filter_create`/`destroy` so the dock can enumerate active filters efficiently.
- **Atomic float fields:** `_Atomic float smoothed_offset_ms` and `_Atomic float last_confidence` added to filter struct for UI thread access.
- **Empty state message:** Single-row placeholder when zero filters are active.
- **Read-only table:** `NoEditTriggers` on `QTableWidget`.
- **No focus stealing:** `Qt::NoFocus` policy on table.

## External Research

None required — all decisions were user-provided or derived from existing codebase patterns.

## Corrections Made

None — all assumptions confirmed by user-provided decisions.

## Deferred Ideas

- Configurable EMA alpha via dock UI — v2
- Global plugin settings panel — deferred from Phase 5
- Per-scene reference switching — deferred from Phase 6
- Manual approve-before-apply — out of scope per PROJECT.md

---

*Phase: 08-dock-ui*
*Discussion logged: 2026-05-04*
