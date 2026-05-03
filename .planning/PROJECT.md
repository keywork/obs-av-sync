# obs-av-sync

## What This Is

An OBS Studio plugin that automatically measures per-source audio offsets between IP camera onboard audio and a designated house audio reference (USB audio interface from physical mixer), then applies corrections via OBS's built-in `sync_offset` — hands-off, with no manual intervention needed during live productions. A polished status dock inside OBS shows all tracked cameras with their current sync state and detected offsets.

## Core Value

Cameras stay in sync with the house audio automatically, every show, without the operator touching anything.

## Requirements

### Validated

- ✓ OBS plugin module structure (plugin-main.c, filter registration) — existing
- ✓ Per-source audio filter skeleton (av_sync_filter.c) with OBS callback integration — existing
- ✓ Mono float32 ring buffer (10s at 48 kHz) with timestamp tracking — existing
- ✓ Cross-platform CMake build system (Windows x64, macOS, Ubuntu) — existing
- ✓ CI/CD pipeline (GitHub Actions: build, format check, signing) — existing

### Active

- [ ] GCC-PHAT cross-correlation engine for offset measurement
- [ ] Reference audio tap (house mix from USB audio interface as measurement anchor)
- [ ] Per-camera offset smoothing and hysteresis before applying sync_offset
- [ ] Automatic `obs_source_set_sync_offset()` application (hands-off during show)
- [ ] OBS dock UI showing all tracked cameras, detected offsets, and sync status
- [ ] Per-source filter property panel (select reference source, enable/disable tracking)
- [ ] ONVIF clock sync exploration as complement to GCC-PHAT for drift correction
- [ ] Unit tests for DSP layer (synthetic signal GCC-PHAT with known delays)

### Out of Scope

- Manual sync control / approve-before-apply workflow — user wants fully automatic
- Camera audio in final output mix — camera audio is sync reference only; house audio goes out
- RTSP direct connection — RTMP proxy is the established setup; no need to change
- OBS plugin browser submission — personal use first; community release deferred

## Context

Live production setup: 2–4 IP cameras connected via RTMP proxy (Media Sources in OBS), house audio from physical mixer via USB audio interface. Cameras have onboard audio baked into the RTMP stream — this audio is used as the sync reference signal, not in the final mix. Both a fixed per-camera offset and ongoing drift during long shows are observed.

ONVIF is supported by the cameras — clock sync (NTP/PTP via ONVIF) may complement GCC-PHAT for tracking slow drift without relying purely on audio correlation.

The codebase is Phase 1–2 complete: plugin skeleton, filter registration, ring buffer, and CI are in place. The core DSP engine (GCC-PHAT), reference tap architecture, smoother, and UI are all still to be built.

Key concern from codebase map: `av_sync_ring_create` is currently called from inside the audio callback on the first frame — heap allocation on OBS's real-time audio thread. This must be fixed before Phase 3.

## Constraints

- **Language**: C for OBS entry point (plugin-main.c); C++ allowed for DSP/state code
- **FFT**: Must be GPL-compatible and build on all 3 platforms — PFFFT (planned) or KissFFT; FFTW excluded
- **Threading**: No blocking or heap allocation on OBS audio callbacks (real-time thread)
- **Licensing**: GPL-2.0-or-later to match OBS; no Apache-2.0 with patent clauses
- **Platform**: Windows primary; keep portable — no Win32-only APIs in core
- **OBS API**: sync_offset is in nanoseconds; positive = delay video relative to audio

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Filter-based approach (not capture callback) | Per-source state, user-visible config, cleaner lifecycle | — Pending |
| GCC-PHAT for offset measurement | Standard technique for time-delay estimation under noise (Knapp & Carter 1976) | — Pending |
| PFFFT as FFT backend | BSD-like license, cross-platform, no GPL pollution | — Pending |
| RTMP proxy (not RTSP direct) | Better performance for IP cameras; already established in production | ✓ Good |
| Camera audio = sync reference only | House audio (USB interface) is the output mix; camera audio only used to measure offset | ✓ Good |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-05-03 after initialization*
