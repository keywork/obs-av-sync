# Phase 7 Research Document — ONVIF Drift Evaluation

---

## 7. OBS Timing API Survey

### 7.1 Found APIs

#### 1. `obs_source_get_sync_offset`
**Source:** `obsproject/obs-studio/libobs/obs.h:L1202-L1203`

```c
/** Sets the audio sync offset (in nanoseconds) for a source */
EXPORT void obs_source_set_sync_offset(obs_source_t *source, int64_t offset);

/** Gets the audio sync offset (in nanoseconds) for a source */
EXPORT int64_t obs_source_get_sync_offset(const obs_source_t *source);
```

**Behavior:** Returns the **user-set or plugin-applied** audio sync offset in nanoseconds. It does **not** return a measured or inferred offset. The value is stored in `source->sync_offset` (`int64_t`).  
**Applicable to:** All sources with audio output.  
**Note:** There is a known issue (#7912) where calling `obs_source_set_sync_offset` once may not take effect until called a second time with a different value.

---

#### 2. `obs_source_get_audio_timestamp`
**Source:** `obsproject/obs-studio/libobs/obs.h:L1537` and `libobs/obs-source.c:L5489`

```c
EXPORT bool obs_source_audio_pending(const obs_source_t *source);
EXPORT uint64_t obs_source_get_audio_timestamp(const obs_source_t *source);
EXPORT void obs_source_get_audio_mix(const obs_source_t *source, struct obs_source_audio_mix *audio);
```

**Behavior:** Returns `source->audio_ts` — the **internal mixing cursor** for the source's audio, not the original container PTS. This timestamp is rewritten by OBS during `source_output_audio_data` to align with OBS's audio mixing timeline.  
**What it is NOT:** It does **not** return the FFmpeg frame PTS, the wall-clock receive time, or the camera's clock time.  
**Applicable to:** All sources that output audio.

---

#### 3. `obs_output_get_total_bytes`
**Source:** `obsproject/obs-studio/libobs/obs.h:L2066`

```c
EXPORT uint64_t obs_output_get_total_bytes(const obs_output_t *output);
EXPORT int obs_output_get_frames_dropped(const obs_output_t *output);
EXPORT int obs_output_get_total_frames(const obs_output_t *output);
```

**Behavior:** Returns aggregate output statistics for an `obs_output_t` (stream/recording). `get_total_bytes` calls into the output's `info.get_total_bytes` callback if implemented.  
**Applicable to:** Output encoders/streamers only.  
**Relevance to timing:** Byte/frame counters do not expose timing metadata, only throughput.

---

#### 4. `obs_output_get_active_delay`
**Source:** `obsproject/obs-studio/libobs/obs.h:L1937` and `libobs/obs-output-delay.c:L200`

```c
/** Gets the currently set delay value, in seconds. */
EXPORT uint32_t obs_output_get_delay(const obs_output_t *output);

/** If delay is active, gets the currently active delay value, in seconds. */
EXPORT uint32_t obs_output_get_active_delay(const obs_output_t *output);
```

**Behavior:** Returns the configured replay-buffer delay in seconds. `obs_output_get_active_delay` returns `output->active_delay_ns / 1e9`.  
**Applicable to:** Outputs with delay enabled.  
**Relevance to timing:** This is a user-configured output buffer, not a measure of source ingest timing.

---

#### 5. `obs_source_audio_pending`
**Source:** `obsproject/obs-studio/libobs/obs-source.c:L5485`

```c
bool obs_source_audio_pending(const obs_source_t *source)
{
    /* ... */
    return (is_composite_source(source) || is_audio_source(source)) ? source->audio_pending : true;
}
```

**Behavior:** Returns whether the source has pending audio data that has not yet been rendered.  
**Applicable to:** All audio-capable sources.

---

### 7.2 APIs NOT Found

| API Candidate                                          | Search Scope                                           | Result                                                            |
| ------------------------------------------------------ | ------------------------------------------------------ | ----------------------------------------------------------------- |
| `obs_source_get_timestamp`                               | `libobs/obs.h`, `libobs/obs-source.c`, `libobs/obs-source.h` | **Not found** — no generic timestamp getter exists                    |
| `obs_source_get_timing`                                  | Same as above                                          | **Not found**                                                         |
| `obs_source_get_frame_interval`                          | Same as above                                          | **Not found**                                                         |
| `obs_source_get_audio_timestamp` returning container PTS | `libobs/obs-source.c` source-output path                 | **Not found** — `audio_ts` is the mixing cursor, not PTS                |
| `obs_source_get_next_audio_timestamp`                    | `libobs/obs.h`, PR #11176                                | **Not merged** — open experimental PR from 2024-08, not in master     |
| `obs_source_get_receive_timestamp`                       | Entire repo                                            | **Not found** — OBS does not record wall-clock receive time per frame |
| `obs_source_get_decoder_latency`                         | Entire repo                                            | **Not found**                                                         |

---

## 8. Media Source (RTMP) Timing Metadata

### 8.1 How `audio->timestamp` is generated for Media Sources

OBS Media Sources use the shared `media-playback` layer (`shared/media-playback/media-playback/media.c`). The critical path is `mp_media_next_audio`:

**Source:** `obsproject/obs-studio/shared/media-playback/media-playback/media.c:L341-L362`

```c
void mp_media_next_audio(mp_media_t *m)
{
    struct mp_decode *d = &m->a;
    struct obs_source_audio audio = {0};
    AVFrame *f = d->frame;

    if (!mp_media_can_play_frame(m, d))
        return;

    d->frame_ready = false;
    if (!m->a_cb)
        return;

    /* ... plane setup ... */

    audio.samples_per_sec = f->sample_rate * m->speed / 100;
    audio.speakers = convert_speaker_layout(f->ch_layout.nb_channels);
    audio.format = convert_sample_format(f->format);
    audio.frames = f->nb_samples;
    audio.timestamp = m->full_decode ? d->frame_pts
                     : m->base_ts + d->frame_pts - m->start_ts + m->play_sys_ts - base_sys_ts;

    if (audio.format == AUDIO_FORMAT_UNKNOWN)
        return;

    m->a_cb(m->opaque, &audio);
}
```

For normal (non-full-decode) playback, the timestamp is computed as:

```
timestamp = base_ts + frame_pts - start_ts + play_sys_ts - base_sys_ts
```

Where:
- `frame_pts` = FFmpeg decoded frame PTS (in stream timebase, converted to nanoseconds)
- `start_ts` = first PTS of the current playback segment
- `base_ts` = accumulated offset across loops/resets
- `play_sys_ts` = `os_gettime_ns()` when playback became active
- `base_sys_ts` = global static set once at module load to `os_gettime_ns()`

**Key finding:** The original container PTS is **rewritten into OBS's wall-clock domain**. The plugin layer never sees `frame_pts` directly.

---

### 8.2 Additional timestamp processing in `source_output_audio_data`

**Source:** `obsproject/obs-studio/libobs/obs-source.c` (audio output path)

After `media-playback` delivers the frame, `source_output_audio_data` further manipulates the timestamp:

```c
/* detects 'directly' set timestamps as long as they're within a certain threshold */
if (uint64_diff(in.timestamp, os_time) < MAX_TS_VAR) {
    source->timing_adjust = 0;
    source->timing_set = true;
    using_direct_ts = true;
}

if (!source->timing_set) {
    reset_audio_timing(source, in.timestamp, os_time);
} else if (source->next_audio_ts_min != 0) {
    diff = uint64_diff(source->next_audio_ts_min, in.timestamp);
    if (diff > MAX_TS_VAR && !using_direct_ts)
        handle_ts_jump(source, source->next_audio_ts_min, in.timestamp, diff, os_time);
    else if (diff < TS_SMOOTHING_THRESHOLD) {  /* 70 ms */
        if (source->async_unbuffered && source->async_decoupled)
            source->timing_adjust = os_time - in.timestamp;
        in.timestamp = source->next_audio_ts_min;
    }
}

in.timestamp += source->timing_adjust;
in.timestamp += sync_offset;        /* user/plugin sync offset */
in.timestamp -= source->resample_offset;
```

**Implications:**
- Timestamps are **smoothed** if they differ by < 70 ms from the expected cadence
- Large jumps (> 2 s, `MAX_TS_VAR`) trigger a **timing reset** that discards buffer state
- The `sync_offset` is baked into the mixing timestamp
- What the filter receives via `filter_audio` has already been through **two layers** of remapping

---

### 8.3 What is NOT exposed

| Information                     | Accessible from plugin? | Evidence                                                            |
| ------------------------------- | ----------------------- | ------------------------------------------------------------------- |
| Original container PTS/DTS      | **No**                      | Rewritten in `mp_media_next_audio` before callback                    |
| Wall-clock receive timestamp    | **No**                      | `av_read_frame` is called in `mp_media_thread`; no receive-time logging |
| Stream timebase (e.g., 1/90000) | **No**                      | `AVStream->time_base` is used internally but not exposed              |
| Network jitter / buffer depth   | **No**                      | No API or signal for ingest buffer statistics                       |
| Decoder latency (frame age)     | **No**                      | No timestamp comparison between decode and present                  |

---

## 9. OBS Internal Timing Mechanisms

### 9.1 Source Signals (Plugin-visible)

**Source:** `obsproject/obs-studio/libobs/obs-source.c:L126-L165`

```c
static const char *source_signals[] = {
    "void destroy(ptr source)",
    "void remove(ptr source)",
    "void update(ptr source)",
    "void save(ptr source)",
    "void load(ptr source)",
    "void activate(ptr source)",
    "void deactivate(ptr source)",
    "void show(ptr source)",
    "void hide(ptr source)",
    "void mute(ptr source, bool muted)",
    /* ... push-to-mute, push-to-talk ... */
    "void enable(ptr source, bool enabled)",
    "void rename(ptr source, string new_name, string prev_name)",
    "void volume(ptr source, in out float volume)",
    "void update_properties(ptr source)",
    "void update_flags(ptr source, int flags)",
    "void audio_sync(ptr source, int out int offset)",   /* <-- sync offset changed */
    "void audio_balance(ptr source, in out float balance)",
    "void audio_mixers(ptr source, in out int mixers)",
    "void audio_monitoring(ptr source, int type)",
    "void audio_activate(ptr source)",
    "void audio_deactivate(ptr source)",
    "void filter_add(ptr source, ptr filter)",
    "void filter_remove(ptr source, ptr filter)",
    "void reorder_filters(ptr source)",
    "void transition_start(ptr source)",
    "void transition_video_stop(ptr source)",
    "void transition_stop(ptr source)",
    "void media_play(ptr source)",
    "void media_pause(ptr source)",
    "void media_restart(ptr source)",
    "void media_stopped(ptr source)",
    "void media_next(ptr source)",
    "void media_previous(ptr source)",
    "void media_started(ptr source)",
    "void media_ended(ptr source)",
    NULL,
};
```

**Verdict:** The `audio_sync` signal fires when the sync offset is changed, but it carries the **new offset value**, not a measurement of drift. There are **no signals** for:
- Timestamp jumps
- Buffer underrun/overrun
- Network jitter
- Clock drift

**Plugin-visible?** Yes, via `obs_source_get_signal_handler`, but **not useful for drift tracking**.

---

### 9.2 Debug Logging (Not plugin-visible)

**Source:** `obsproject/obs-studio/libobs/obs-source.c` (`handle_ts_jump`, `source_output_audio_data`)

OBS logs timestamp jumps at `LOG_DEBUG` level:

```c
blog(LOG_DEBUG,
     "Timestamp for source '%s' jumped by '%" PRIu64 "', "
     "expected value %" PRIu64 ", input value %" PRIu64,
     source->context.name, diff, expected, ts);
```

And audio placement when `DEBUG_AUDIO == 1`:

```c
blog(LOG_DEBUG, "frames: %lu, size: %lu, placement: %lu, base_ts: %llu, ts: %llu", ...);
```

**Verdict:** Internal debugging only. No runtime API to query these metrics.

---

### 9.3 Output Statistics (Plugin-visible, wrong layer)

`obs_output_get_frames_dropped`, `obs_output_get_total_frames`, and `obs_output_get_total_bytes` are public APIs, but they measure the **output encoding/streaming** layer. A plugin can call them, but they reveal nothing about per-source ingest timing or camera clock drift.

---

### 9.4 `obs_source_get_audio_mix` (Plugin-visible)

Returns the actual mixed audio buffers. While a plugin could theoretically analyze these buffers externally, the returned `obs_source_audio_mix` contains **no timing metadata** beyond what `obs_source_get_audio_timestamp` already provides.

---

## 10. OBS vs. ONVIF Timing Gap Analysis

| Timing Question                                               | OBS Can Answer                                                                    | ONVIF Can Answer                                                                                |
| ------------------------------------------------------------- | --------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| What is the current per-source AV sync offset?                | **Yes** — `obs_source_get_sync_offset` returns the applied offset (user or plugin set). | **No** — ONVIF has no visibility into OBS pipeline state.                                           |
| What is the camera's current wall-clock time?                 | **No** — OBS never queries the camera's clock.                                        | **Yes** — `GetSystemDateAndTime` returns the camera's system time (subject to network RTT).           |
| What is the end-to-end ingest delay (network + decode)?       | **No** — No receive timestamp or latency API exists.                                  | **Partial** — Camera may report encoder delay, but the RTMP network path is opaque to ONVIF.        |
| Is the source experiencing clock drift relative to reference? | **No** — All timestamps are remapped to OBS's timeline; original PTS is discarded.    | **Yes** — If camera clock is queried and compared to a common NTP reference, drift can be detected. |
| What is the original stream PTS/timebase?                     | **No** — `audio->timestamp` is remapped to nanoseconds since `base_sys_ts`.               | **No** — ONVIF Device Management does not expose media-layer PTS.                                   |
| Is there network jitter on the RTMP ingest?                   | **No** — No jitter or buffer-depth statistics per source.                             | **No** — ONVIF operates over HTTP/SOAP, not the RTMP transport.                                     |
| Does the source have pending/unrendered audio?                | **Yes** — `obs_source_audio_pending`.                                                   | **No** — Not a camera-side concept.                                                                 |

### 10.1 Can clock deltas be inferred without ONVIF?

**No.** From the plugin layer, the following critical pieces of information are **unavailable**:

1. **Camera wall-clock time** — OBS has no protocol to query it.
2. **Original container PTS** — Rewritten in `mp_media_next_audio` before any filter sees it.
3. **Receive timestamp** — `av_read_frame` consumes packets but does not tag them with a wall-clock receive time accessible to plugins.
4. **Network jitter or buffer depth** — No API exposes RTMP ingest statistics per source.

The `audio->timestamp` field visible in `filter_audio` is an **OBS-internal mixing cursor**. Two cameras with identical content but different network delays will present **indistinguishable** timestamps to a filter, because OBS normalizes them into its own continuous timeline via `base_sys_ts`, `play_sys_ts`, and `timing_adjust`.

### 10.2 What ONVIF would actually add

ONVIF `GetSystemDateAndTime` would allow the plugin to know the **camera's clock**. If that clock drifts relative to the house reference (or a shared NTP server), the drift can be measured **independently of audio content**. However:

- ONVIF measures **camera clock drift**, not RTMP transport jitter.
- The RTMP proxy/ingest path introduces its own variable delay that ONVIF cannot observe.
- Achievable accuracy is bounded by network RTT (typically 1–10 ms over LAN).
- The camera must actually implement `GetSystemDateAndTime` accurately (some budget cameras return cached or imprecise values).

### 10.3 Relationship to GCC-PHAT

The existing GCC-PHAT + EMA smoother measures **end-to-end offset**, which implicitly includes:
- Camera clock drift
- Encoder buffering
- Network transport variation
- OBS ingest buffering

ONVIF would **not replace** GCC-PHAT. At best, it could provide an independent **camera-clock-drift** measurement that might help disambiguate whether observed long-term drift is due to:
- (A) Camera clock running fast/slow, or
- (B) Network path changing (e.g., RTMP proxy buffer growth).

Because GCC-PHAT already captures the total effect of (A) + (B), ONVIF is only valuable if knowing the camera clock separately enables a better correction strategy — e.g., predicting drift before it manifests in audio correlation.

### 10.4 Verdict for Plan 07-03

The gap that ONVIF would fill is **narrow**: it provides camera clock visibility that OBS entirely lacks, but it does not solve transport-layer jitter, and the existing GCC-PHAT engine already measures the composite result of clock drift + network variation. The decision in Plan 07-03 should weigh whether ≤ 5 ms camera-clock accuracy is achievable in practice and whether that knowledge improves the smoother beyond what audio correlation alone provides.
