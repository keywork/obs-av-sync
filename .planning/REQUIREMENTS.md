# Requirements — obs-av-sync

## v1 Requirements

### Core Sync Engine (DSP / Measurement)

- [ ] **SYNC-01**: Plugin measures audio offset between each tracked camera source and the designated house reference source using GCC-PHAT cross-correlation
- [ ] **SYNC-02**: Offset measurements are smoothed with hysteresis to prevent thrashing on noisy or transient estimates
- [ ] **SYNC-03**: Corrected offset is applied automatically via `obs_source_set_sync_offset()` with no user action required during a live show
- [ ] **SYNC-04**: Ring buffer allocation is moved off the OBS audio callback thread (fix existing real-time-thread heap allocation in `av_sync_ring_create`)
- [ ] **SYNC-05**: GCC-PHAT engine is unit-tested with synthetic signals at known delays (≥10 dB SNR, ±500 ms range)

### Reference Tap / Source Configuration

- [ ] **REF-01**: User designates exactly one OBS audio source as the house audio reference (not hardcoded — any OBS source works)
- [ ] **REF-02**: Per-camera filter lets user enable or disable sync tracking for that individual source
- [ ] **REF-03**: Reference source and per-source enable state persist across OBS restarts (saved to scene collection)

### Drift Correction

- [ ] **DRIFT-01**: Plugin tracks and compensates for slow drift over the duration of a show, not only the initial offset
- [ ] **DRIFT-02**: ONVIF clock sync is evaluated as a complement to GCC-PHAT for drift handling and a recommendation is documented

### OBS Dock UI

- [ ] **UI-01**: A docked panel in OBS lists all tracked camera sources with their current sync status
- [ ] **UI-02**: Dock displays detected offset (in ms) and applied correction for each tracked source
- [ ] **UI-03**: Dock shows a per-source status indicator (e.g., Synced / Measuring / Out of Range)
- [ ] **UI-04**: Dock is non-intrusive and does not block the OBS scene editor

## v2 Requirements (Deferred)

- Manual approve-before-apply mode (user confirms offset before it is applied)
- OBS plugin browser submission packaging
- Multi-house-reference support (e.g., per-scene reference switching)
- ONVIF-based auto-discovery of camera sources in OBS

## Out of Scope

- Camera audio in the output mix — camera audio is the sync reference signal only; house audio (USB interface) is the program output
- RTSP direct connection — RTMP proxy is the established production setup
- Windows-only APIs in core — plugin must remain portable across Windows, macOS, and Linux

## Traceability

| REQ-ID   | Phase | Status |
|----------|-------|--------|
| SYNC-01  | 4     | ⬜ Not started |
| SYNC-02  | 6     | ⬜ Not started |
| SYNC-03  | 6     | ⬜ Not started |
| SYNC-04  | 3     | ⬜ Not started |
| SYNC-05  | 4     | ⬜ Not started |
| REF-01   | 5     | ⬜ Not started |
| REF-02   | 5     | ⬜ Not started |
| REF-03   | 5     | ⬜ Not started |
| DRIFT-01 | 6     | ⬜ Not started |
| DRIFT-02 | 7     | ⬜ Not started |
| UI-01    | 8     | ⬜ Not started |
| UI-02    | 8     | ⬜ Not started |
| UI-03    | 8     | ⬜ Not started |
| UI-04    | 8     | ⬜ Not started |
