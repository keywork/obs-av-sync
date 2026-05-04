# Plan 07-02 Summary: Evaluate OBS Timing Metadata

**Date:** 2026-05-04
**Plan:** 07-02
**Requirement:** DRIFT-02

## What was investigated

1. OBS public timing APIs (`obs-source.h`, `obs-output.h`, `obs-source.c`)
2. Media Source (FFmpeg / RTMP ingest) timestamp generation (`media-playback/media.c`)
3. OBS internal timing mechanisms (signals, debug logging, output stats)
4. Gap analysis: what OBS provides vs. what ONVIF could provide

## Key findings

### OBS timing APIs are limited to user-visible state
- `obs_source_get_sync_offset` returns applied offset, not a measurement.
- `obs_source_get_audio_timestamp` returns the **internal mixing cursor**, not the original container PTS.
- `obs_output_get_frames_dropped` / `get_total_bytes` are output-layer counters, irrelevant to ingest timing.
- **No API exists** for: receive timestamps, decoder latency, stream timebase, network jitter, or original PTS.

### Media Source timestamps are rewritten before filters see them
In `mp_media_next_audio`, FFmpeg `frame_pts` is mapped to OBS wall-clock time via:

```c
timestamp = base_ts + frame_pts - start_ts + play_sys_ts - base_sys_ts
```

Then `source_output_audio_data` smooths small deviations (< 70 ms) and adds `sync_offset`. The result is an **OBS-normalized timeline** that erases camera-clock information.

### OBS signals do not expose timing metadata
The standard `source_signals[]` array includes `audio_sync` (fires on offset change) and media lifecycle signals, but nothing for timestamp jumps, buffering, or drift.

### The gap is real and uncloseable from inside OBS alone
| Needed for drift tracking | OBS provides?  | ONVIF provides?                |
| ------------------------- | -------------- | ------------------------------ |
| Camera wall-clock time    | **No**             | **Yes** (via `GetSystemDateAndTime`) |
| Original container PTS    | **No**             | **No**                             |
| Receive/ingest timestamp  | **No**             | **No**                             |
| Network jitter per source | **No**             | **No**                             |
| End-to-end offset         | **Yes** (GCC-PHAT) | **No**                             |

### Implication for Plan 07-03
ONVIF can add **camera clock visibility** that OBS completely lacks, but it does not solve transport-layer variation. GCC-PHAT already measures the composite (clock + network) offset. The value of ONVIF depends on whether knowing the camera clock separately improves drift prediction enough to justify the integration complexity and accuracy limits (network RTT ≈ 1–10 ms).

## Acceptance criteria check

| Criterion                                                            | Status                                       |
| -------------------------------------------------------------------- | -------------------------------------------- |
| `07-RESEARCH.md` contains `## OBS Timing API Survey`                     | ✅ Section 7                                 |
| ≥ 5 timing-related API candidates listed with found/not-found status | ✅ 7 found + 7 not-found documented          |
| Exact C signatures quoted for found APIs                             | ✅                                           |
| `07-RESEARCH.md` contains `### Media Source (RTMP) Timing Metadata`      | ✅ Section 8                                 |
| Explains how `audio->timestamp` is generated                           | ✅ With source code citation                 |
| `07-RESEARCH.md` contains `### OBS Internal Timing Mechanisms`           | ✅ Section 9                                 |
| ≥ 2 mechanisms listed with plugin-visible verdict                    | ✅ Signals, logging, output stats, audio mix |
| `07-RESEARCH.md` contains `## OBS vs. ONVIF Timing Gap Analysis`         | ✅ Section 10                                |
| Table with ≥ 4 rows                                                  | ✅ 7 rows                                    |
| Concluding paragraph on ONVIF necessity                              | ✅ Section 10.4                              |

## Downstream recommendation

Feed these findings directly into **Plan 07-03** (Proof-of-Concept or Verdict). The critical question for 07-03 is:

> *Given that OBS provides no alternative path to camera clock data, does ONVIF `GetSystemDateAndTime` achieve ≤ 5 ms accuracy in practice, and does that knowledge improve drift handling beyond GCC-PHAT alone?*
