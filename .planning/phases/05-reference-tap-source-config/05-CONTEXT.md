# Phase 5: Reference Tap & Source Configuration - Context

**Gathered:** 2026-05-03
**Status:** Ready for planning

## Phase Boundary

Users can designate any OBS audio source as the house reference and enable/disable tracking per camera; both settings persist across OBS restarts.

Requirements: REF-01, REF-02, REF-03

## Implementation Decisions

### Reference Tap Architecture
- **D-01:** Global singleton reference tap. One `obs_source_add_audio_capture_callback` attached to the designated reference source, writing into a single shared `av_sync_ring_t` (SPSC-safe, same ring_buffer.c from Phase 3). All per-camera filter instances read from this shared ring for GCC-PHAT analysis. A single `pthread_mutex_t` or `CRITICAL_SECTION` protects the callback attach/detach and ring pointer.
- **D-02:** Reference tap lives in a new C file `src/reference_tap.c` (and `reference_tap.h`) with internal static state: `reference_source_name` (string, max 256), `reference_source` (obs_source_t*), `reference_ring` (av_sync_ring_t*), `reference_callback` (obs_source_audio_capture_callback), and a mutex for thread-safe attach/detach.

### Reference Source Lifecycle
- **D-03:** If the designated reference source is removed from OBS, the plugin logs a `LOG_WARNING`, detaches the audio callback, and holds the last known offset in the per-filter state. No crash, no reset to zero. The user must re-select a reference source from the filter properties.
- **D-04:** No auto-detection by name on source add. Manual re-selection only — avoids surprising behavior when sources with matching names are created for other purposes.

### Enable/Disable Toggle Behavior
- **D-05:** When a filter is disabled (`sync_enabled == false`), the filter continues to downmix and write to its per-source ring buffer (zero warm-up cost on re-enable), but the analysis thread (Phase 6) stops running GCC-PHAT and stops calling `obs_source_set_sync_offset`. The audio callback stays active so re-enable is instantaneous.
- **D-06:** `sync_enabled` default is `true` when the filter is first added.

### Settings Scope
- **D-07:** Per-filter properties UI. Each filter instance has:
  - A dropdown populated from `obs_enum_sources()` listing all OBS audio sources (filtered to those with audio output flags).
  - An enable/disable checkbox ("Enable AV Sync Tracking").
- **D-08:** Reference source selection is per-filter, not global. This allows advanced users to use different reference sources per camera (e.g., multiple house audio mixes). The typical use case is all cameras selecting the same source.

### Settings Serialization
- **D-09:** `obs_data_t` keys: `reference_source_name` (string) and `sync_enabled` (bool). Both are read in `av_sync_filter_create` via `obs_data_getstring` / `obs_data_get_bool` and saved in `av_sync_filter_update` (or wherever OBS calls the update hook).
- **D-10:** Default values: `reference_source_name = ""` (empty = none selected), `sync_enabled = true`.
- **D-11:** Startup validation in `av_sync_filter_create`: if `reference_source_name` is non-empty, call `obs_get_source_by_name` to verify the source still exists. If not found, log a warning and leave the reference unset.

### Filter Properties UI
- **D-12:** Implement `get_properties` and `update` hooks in `av_sync_filter.c`. The `get_properties` callback returns an `obs_properties_t*` with:
  - `obs_properties_add_list` (type `OBS_COMBO_TYPE_LIST`, format `OBS_COMBO_FORMAT_STRING`) for reference source selection
  - `obs_properties_add_bool` for the enable/disable toggle
- **D-13:** The reference source dropdown is populated dynamically in `get_properties` by iterating `obs_enum_sources()` and filtering for sources with `obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO`.

### OBS Frontend API
- **D-14:** Set `ENABLE_FRONTEND_API=ON` in `CMakeLists.txt` so the filter can use `obs_frontend_add_event_callback` to listen for source removal events (for D-03 lifecycle handling). This also enables future dock UI work (Phase 8).

### Claude's Discretion
- UI label text (e.g., "Reference Source" vs "House Audio Source") — standard OBS terminology preferred.
- Whether to add a "Refresh" button for re-scanning sources — defer to Phase 8 dock UI.

## Canonical References

### Requirements
- `.planning/REQUIREMENTS.md` §REF-01, REF-02, REF-03 — Reference tap and source configuration requirements
- `.planning/ROADMAP.md` §Phase 5 — Phase goal, plans, and success criteria

### Architecture
- `.planning/PROJECT.md` — Project vision and principles
- `docs/ARCHITECTURE.md` — Data flow and OBS API touchpoints

### Existing Code
- `src/av_sync_filter.c` — Current filter implementation (no properties UI yet)
- `src/plugin-main.c` — Module entry point
- `src/ring_buffer.h` / `src/ring_buffer.c` — SPSC ring buffer (reused for reference tap)

## Existing Code Insights

### Reusable Assets
- `av_sync_ring_create` / `av_sync_ring_destroy` / `av_sync_ring_write` / `av_sync_ring_read` — the reference tap reuses the same ring buffer with no changes
- `av_sync_filter.c` struct — can be extended with `reference_source_name` and `sync_enabled` fields

### Established Patterns
- C11 atomics for thread safety (`_Atomic size_t total_written`)
- Heap allocation at create time, never in audio callback
- `obs_log` for diagnostic output

### Integration Points
- `obs_module_load` / `obs_module_unload` — reference tap init/cleanup hooks added here
- `av_sync_filter_create` / `av_sync_filter_destroy` — per-filter reference source validation and ring access
- `av_sync_filter_update` — OBS calls this when user changes filter properties

## Deferred Ideas

- Auto-detect reference source by name on source add — deferred to Phase 6 or later
- Global plugin settings panel for default reference — deferred to Phase 8 (dock UI)
- Source removal event listener via `obs_frontend_add_event_callback` — partially needed for D-03, full implementation deferred if simple name check in create is sufficient
