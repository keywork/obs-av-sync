# ONVIF Clock-Sync Evaluation

**Date:** 2026-05-04
**Requirement:** DRIFT-02
**Adoption Threshold:** ≤ 5 ms clock-delta accuracy

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Methods Evaluated](#methods-evaluated)
   - [NTP via ONVIF](#ntp-via-onvif)
   - [PTP via ONVIF](#ptp-via-onvif)
   - [GetSystemDateAndTime SOAP Polling](#getsystemdateandtime-soap-polling)
   - [OBS Timing Metadata](#obs-timing-metadata)
   - [Method Comparison Table](#method-comparison-table)
3. [Accuracy Analysis](#accuracy-analysis)
   - [NTP Accuracy Scenarios](#ntp-accuracy-scenarios)
   - [PTP Accuracy Scenarios](#ptp-accuracy-scenarios)
   - [GetSystemDateAndTime Accuracy Scenarios](#getsystemdateandtime-accuracy-scenarios)
   - [≤ 5 ms Threshold Assessment](#5-ms-threshold-assessment)
   - [Cumulative Error with GCC-PHAT](#cumulative-error-with-gcc-phat)
   - [Camera-Specific Caveats](#camera-specific-caveats)
4. [Integration Complexity](#integration-complexity)
   - [Direct Integration](#direct-integration)
   - [External Tool Integration](#external-tool-integration)
   - [Complexity Comparison](#complexity-comparison)
   - [Engineering Effort Estimates](#engineering-effort-estimates)
5. [Comparison with GCC-PHAT](#comparison-with-gcc-phat)
   - [GCC-PHAT Baseline Summary](#gcc-phat-baseline-summary)
   - [ONVIF Complement Value Proposition](#onvif-complement-value-proposition)
   - [Overlap and Redundancy](#overlap-and-redundancy)
   - [Production Scenario Decision Matrix](#production-scenario-decision-matrix)
   - [Conclusion](#conclusion)
6. [Verdict & Rationale](#verdict--rationale)
7. [Revisit Criteria](#revisit-criteria)

---

## Executive Summary

This document evaluates whether the Open Network Video Interface Forum (ONVIF) Device Management Service can provide clock-synchronization data that improves automatic audio-video (AV) sync in OBS Studio beyond what the existing GCC-PHAT (Generalized Cross-Correlation with Phase Transform) engine already achieves. The evaluation was conducted as part of Phase 7 (DRIFT-02) and synthesizes specification review, accuracy analysis, and OBS timing metadata investigation documented in `.planning/phases/07-onvif-drift-evaluation/07-RESEARCH.md`.

**The verdict is: Defer.**

No ONVIF mechanism reliably achieves the ≤ 5 ms adoption threshold on generic cameras. The Network Time Protocol (NTP) client built into typical ONVIF Profile S cameras delivers 1–10 ms accuracy — a range that straddles the threshold and depends heavily on firmware quality. The Precision Time Protocol (PTP, IEEE 1588) is accurate but effectively absent from the target camera class and requires hardware timestamping plus PTP-aware switches. The only universally available ONVIF primitive, `GetSystemDateAndTime`, is subject to 2–8 ms of error from Simple Object Access Protocol (SOAP) round-trip time (RTT) asymmetry and camera-side processing jitter. Meanwhile, the existing GCC-PHAT + Exponential Moving Average (EMA) smoother already measures the composite end-to-end offset (camera clock drift + network transport + OBS ingest buffering) with < 1 ms synthetic accuracy and ≤ 20 ms residual in production. Integrating ONVIF would add significant build, runtime, and security complexity for a marginal, unguaranteed improvement in camera-clock visibility alone.

---

## Methods Evaluated

### NTP via ONVIF

NTP (RFC 5905) is the default clock-discipline mechanism on most IP cameras. ONVIF exposes NTP configuration through optional `GetNTP` / `SetNTP` operations in the Device Management Service, but it does **not** expose NTP client status (stratum, offset, jitter) through standard interfaces. The camera's internal NTP client synchronizes its system clock independently; the plugin would have to infer sync quality indirectly or assume the camera is well-synchronized.

Camera support is broad — virtually every ONVIF camera supports NTP — but firmware implementations vary wildly. Budget camera System-on-Chips (SoCs) often use lightweight embedded NTP clients with coarse poll intervals (64–1024 s) and no kernel discipline, leading to multi-millisecond wander. ONVIF `GetNTP` only returns the server list, not the sync state, so a generic plugin cannot determine whether the camera clock is currently disciplined or free-running. For a full treatment of NTP accuracy on IP cameras, see `07-RESEARCH.md` §2.

### PTP via ONVIF

PTP (IEEE 1588) can achieve sub-microsecond to sub-millisecond accuracy with hardware timestamping. However, **ONVIF does not standardize PTP** — it is not defined in the Device Management Service WSDL or Core Specification. Where PTP exists, it is exposed through vendor-specific APIs or GigE Vision registers, not ONVIF SOAP.

Prevalence in the target camera class (generic ONVIF Profile S security cameras) is extremely low. PTP requires hardware timestamping support in the Ethernet PHY/MAC, which most budget and mid-range camera SoCs omit. Achieving the best-case < 1 µs accuracy also requires a GPS-disciplined PTP grandmaster clock and PTP-aware network switches (boundary or transparent clocks). For generic live-production cameras, PTP is effectively unavailable. See `07-RESEARCH.md` §3.

### GetSystemDateAndTime SOAP Polling

`GetSystemDateAndTime` is the **only mandatory** clock primitive across all ONVIF devices (Core Specification §5.1.2). It does not require authentication and returns the camera's current UTC estimate in a SOAP response. The client can poll this endpoint and compare the returned time to its own clock to estimate a clock delta.

The fundamental limitation is that the returned `UTCDateTime` is generated by the camera's OS/firmware, not hardware-timestamped at the network layer. The accuracy is bounded by:

1. **Network RTT** — typically 0.2–5 ms on a local gigabit LAN, but 5–20+ ms on WiFi or congested links.
2. **Path asymmetry** — switch buffering, camera CPU scheduling, and interrupt-request (IRQ) coalescence make the request and response paths unequal. The classical NTP offset formula (`offset = ((t2 - t1) + (t3 - t4)) / 2`) assumes symmetry, which is often violated.
3. **Camera SOAP processing time** — camera firmware may take tens to hundreds of milliseconds to serialize the XML response on a busy SoC. This processing delay is indistinguishable from network delay unless the camera timestamps reception and transmission internally, which ONVIF does not require.

Repeated sampling and outlier rejection could reduce noise, but the floor remains RTT-dependent. See `07-RESEARCH.md` §4.

### OBS Timing Metadata

Before committing to an ONVIF integration, we investigated whether OBS already exposes enough timing metadata to infer clock deltas without a direct camera connection. The investigation covered `obs_source_get_sync_offset`, `obs_source_get_audio_timestamp`, `obs_output_get_frames_dropped`, `obs_output_get_active_delay`, and the internal `media-playback` layer.

**Key finding:** OBS does not expose the original container Presentation Timestamp (PTS), Decoding Timestamp (DTS), wall-clock receive time, or stream timebase to plugins. The `audio->timestamp` field visible in `filter_audio` is an **OBS-internal mixing cursor** that has been rewritten through `base_sys_ts`, `play_sys_ts`, and `timing_adjust` smoothing. Two cameras with identical content but different network delays present indistinguishable timestamps to a filter. The gap is real and uncloseable from inside OBS alone. See `07-RESEARCH.md` §7–10.

### Method Comparison Table

| Method | Accuracy Floor | Camera Support | Integration Complexity | Plugin-Layer Feasible |
|--------|----------------|----------------|------------------------|----------------------|
| NTP (camera internal client) | 1–10 ms typical | Universal (but no status visibility) | Low (assumes NTP is working) | Indirect — cannot read sync state |
| PTP (IEEE 1588) | < 1 µs – 1 ms (hardware) | Rare on generic ONVIF cameras | Very high (requires grandmaster, managed switches) | Infeasible — not an ONVIF service |
| `GetSystemDateAndTime` SOAP RTT | 2–8 ms typical | Universal (mandatory) | Medium (HTTP client + XML parser + credential management) | Feasible on background thread |
| OBS timing metadata | N/A (no camera clock data) | N/A — OBS API limitation | N/A | No — no relevant timing APIs exist |

---

## Accuracy Analysis

### NTP Accuracy Scenarios

| Scenario | Expected Accuracy | Notes |
|----------|-------------------|-------|
| **Best-case:** Dedicated LAN, local NTP server, tuned NICs | 0.1–1 ms | Requires GPS-rubidium server and optimized client; achievable in broadcast facilities but not typical live productions |
| **Typical:** Gigabit LAN, unmanaged switch, consumer/prosumer camera firmware | 1–5 ms | Assumes all cameras sync to the same local server; lower bound (~1 ms) requires reasonable firmware |
| **Worst-case:** WiFi-linked cameras, long poll intervals, CPU-loaded SoC | 5–20 ms | Camera firmware may use 1024 s poll intervals with no kernel discipline; jitter from video encoding competes with NTP daemon scheduling |

### PTP Accuracy Scenarios

| Scenario | Expected Accuracy | Notes |
|----------|-------------------|-------|
| **Best-case:** Hardware timestamping, PTP-aware switches, GPS grandmaster | < 1 µs – 100 µs | Industrial GigE Vision territory; irrelevant to generic ONVIF Profile S cameras |
| **Typical:** Software timestamping on a real-time OS | 10–100 µs | Requires RTOS and CPU isolation; not found on security-camera SoCs |
| **Worst-case:** Software timestamping on general-purpose OS | 1–10 ms | Windows or untuned Linux; asymmetry and scheduling jitter dominate |

### GetSystemDateAndTime Accuracy Scenarios

| Scenario | Expected Accuracy | Notes |
|----------|-------------------|-------|
| **Best-case:** Low RTT (< 1 ms), symmetric path, fast camera SOAP stack | 0.5–2 ms | Requires high-end camera firmware and a quiet LAN; rare in practice |
| **Typical:** 2–5 ms RTT, moderate asymmetry, consumer camera firmware | 2–8 ms | A single outlier RTT (e.g., camera busy with video encoding) can throw off the estimate by > 10 ms |
| **Worst-case:** WiFi, congested LAN, or slow camera CPU | 5–20 ms | SOAP processing time inflates RTT without corresponding network delay; asymmetry is unmeasured |

### ≤ 5 ms Threshold Assessment

| Method | Meets ≤ 5 ms under typical conditions? |
|--------|----------------------------------------|
| NTP (camera internal) | **Marginal** — lower bound (~1 ms) satisfies, upper bound (~10 ms) does not; depends on firmware quality |
| PTP | **Yes** (if available) — but unavailable on target cameras; verdict is infeasible, not insufficiently accurate |
| `GetSystemDateAndTime` SOAP RTT | **Marginal** — typical range is 2–8 ms, often exceeding 5 ms; statistically noisy |

No ONVIF method satisfies the threshold reliably and universally on generic cameras.

### Cumulative Error with GCC-PHAT

If ONVIF clock sync were integrated alongside the existing GCC-PHAT engine, the two error sources would combine as follows:

GCC-PHAT measures the **composite end-to-end offset**, which includes camera clock drift, encoder buffering, network transport variation, and OBS ingest buffering. ONVIF, at best, measures only the **camera clock drift** component. The residual error after applying an ONVIF-based correction would still include network and OBS-side variation that GCC-PHAT already captures. Because the ONVIF error (1–10 ms) is large relative to the GCC-PHAT synthetic accuracy (< 1 ms), adding ONVIF would increase total variance in scenarios where the camera clock is already well-synchronized, without improving measurement of the transport layer.

If the errors were statistically independent, the combined standard deviation would be `sqrt(var_gcc + var_onvif)`. Since `var_onvif` (≈ 4–25 ms²) dominates `var_gcc` (≈ 0.001 ms²), the combined error would be driven almost entirely by ONVIF. There is no evidence that ONVIF and GCC-PHAT errors are anti-correlated in a way that would reduce total variance.

### Camera-Specific Caveats

- Some vendors (e.g., Axis) expose NTP client status through proprietary ONVIF extensions or parallel APIs (VAPIX `getNTPInfo`). A generic plugin cannot rely on these.
- Budget cameras may return cached or imprecise `UTCDateTime` values from `GetSystemDateAndTime`, especially if the camera's real-time clock (RTC) is the only time source and NTP is not configured.
- Cameras set to `Manual` time mode (per the `DateTimeType` enum) have no external discipline at all; their clocks drift at the rate of the local crystal, typically 20–100 parts per million (ppm).

---

## Integration Complexity

### Direct Integration

Embedding an ONVIF client directly inside the plugin is technically feasible and operationally preferable to an external helper.

- **Library choice:** A hand-rolled HTTP client using libcurl plus an XML parser (expat or tinyxml2) is the lightest path (~500–2000 lines of code for `GetSystemDateAndTime` and WS-Security digest authentication). gSOAP-generated stubs are standard but produce megabytes of generated C++ code. `sr99622/libonvif` (LGPL-2.1) is an alternative but carries unused GUI/AI baggage. All evaluated libraries are compatible with the project's GPL-2.0-or-later license.
- **Threading model:** The plugin already spawns a background analysis thread per filter instance (`pthread_create` in `av_sync_filter_create`, `pthread_join` in `av_sync_filter_destroy`). ONVIF polling would run on a secondary background thread (or the same thread at a lower duty cycle). The critical Phase 3 constraint — no blocking or heap allocation on OBS audio callbacks — is not violated because all network I/O happens off the real-time audio path.
- **Build system impact:** CMake would need `find_package(CURL)` and `find_package(Expat)` (or equivalent), plus cross-platform fallback logic. Both dependencies are ubiquitous on Linux, available via vcpkg on Windows, and present on macOS through Homebrew or the system SDK. No changes to the OBS module entry point (`plugin-main.c`) are required.
- **Runtime impact:** Parsing a ~1 KB SOAP response every 500 ms–1 s consumes < 1% CPU on modern hardware. Memory footprint is negligible (~tens of KB per camera for HTTP buffers and XML state). This is small compared to the existing GCC-PHAT analysis, which performs Fast Fourier Transforms (FFTs) on 4-second windows.
- **Security considerations:** ONVIF digest authentication requires storing camera credentials (username/password) in plugin settings. OBS `obs_data_t` serialization is not encrypted; credentials would be written to the scene collection JSON in plaintext. HTTPS with certificate validation is supported by libcurl but adds certificate-management complexity. Camera IP addresses and ports must be configurable per source.

### External Tool Integration

A standalone helper process that queries cameras and feeds clock-deltas to the plugin via inter-process communication (IPC) is technically possible but introduces operational friction.

- **Architecture:** A separate executable (or script) runs alongside OBS, polls cameras via ONVIF, and writes clock-deltas to a named pipe, localhost socket, or shared memory segment that the plugin reads.
- **Operational complexity:** The user must either launch the helper manually or the plugin must auto-launch it. Auto-launch requires process management (spawn, monitor, restart) that is non-trivial and error-prone across Windows, macOS, and Linux.
- **Latency of delivery:** Even low-latency IPC (< 1 ms) consumes 20% of the 5 ms accuracy budget. File polling (100 ms–1 s) is far too slow for real-time drift tracking.
- **Deployment:** Distributing a second executable complicates the CMake build, packaging, and installer for all three platforms. The OBS plugin template is designed for a single shared library per platform.

### Complexity Comparison

| Dimension | Direct Integration | External Tool |
|-----------|-------------------|---------------|
| Build | Medium — add libcurl + expat to CMake; cross-platform dependency resolution | High — second executable target, separate packaging, installer changes |
| Runtime | Low — single process, no IPC overhead | Medium — IPC latency + second process memory footprint |
| Security | Medium — credential storage in OBS settings; HTTPS cert validation | Medium — same credential problem, plus IPC endpoint access control |
| Operational | Low — user configures camera IP in filter properties, nothing else to run | High — user must ensure helper is running; plugin must detect and recover from helper crashes |

### Engineering Effort Estimates

| Path | Estimated Effort |
|------|-----------------|
| Direct integration (hand-rolled libcurl + expat) | 1–2 weeks — includes HTTP client, XML parsing, WS-Security digest auth, background thread wiring, settings UI, and cross-platform CMake integration |
| Direct integration (gSOAP-generated stubs) | 1–2 weeks — similar timeline but more time spent on generated-code build integration and binary-size mitigation |
| External tool (libcurl + expat helper + IPC) | 2–4 weeks — helper process, IPC protocol design, process lifecycle management, packaging, and installer changes exceed the direct path |

---

## Comparison with GCC-PHAT

### GCC-PHAT Baseline Summary

The existing sync engine, built in Phase 6, provides the following baseline:

- **Measurement frequency:** 2 Hz (one estimate every 500 ms)
- **Analysis window:** 4 seconds of mono audio at the source sample rate
- **Smoother:** EMA with α = 0.3, plus a slew-rate cap of ±20 ms per update
- **Accuracy:** < 1 ms synthetic (known-delay test signals), ≤ 20 ms residual in live production once converged
- **Failure mode:** Requires non-silent audio on both the reference and the target source; cannot measure offset during silent periods or when one source is muted
- **Resolution:** ~63 µs per sample at 16 kHz (the analysis downmix rate)

The GCC-PHAT implementation (`src/gcc_phat.cpp`) uses PFFFT for the FFT stage, PHAT whitening to suppress reverberation, and parabolic sub-sample interpolation for peak refinement. Confidence is reported as a peak-to-sidelobe ratio.

### ONVIF Complement Value Proposition

Despite the accuracy and complexity concerns, ONVIF would add value in a narrow set of scenarios where GCC-PHAT alone is insufficient:

1. **Silent audio periods:** When the reference or camera source is silent, GCC-PHAT cannot compute a cross-correlation. An independent clock-delta measurement from ONVIF could maintain drift tracking during these gaps.
2. **CPU load reduction:** If the camera clock is known to be stable (e.g., disciplined by a local NTP server with verified low jitter), the plugin could reduce GCC-PHAT measurement frequency or skip analysis windows, lowering CPU usage.
3. **Long-term drift tracking without continuous audio analysis:** For very long shows (multiple hours), ONVIF could provide a slower, lower-resolution drift check that complements the faster GCC-PHAT loop.
4. **Initial offset estimation before audio convergence:** At stream startup, GCC-PHAT requires several seconds of audio to converge (≈ 6–10 s). A pre-existing ONVIF clock delta could provide a coarse initial `sync_offset` while the audio path warms up.

### Overlap and Redundancy

In normal production conditions — both reference and camera sources carry continuous audio — ONVIF and GCC-PHAT measure overlapping phenomena. GCC-PHAT already captures the **total** offset, which is the sum of camera clock drift, encoder delay, network transport variation, and OBS ingest buffering. ONVIF would only reveal the camera clock component, leaving the transport layer opaque.

Because the RTMP proxy/ingest path introduces its own variable delay that ONVIF cannot observe, knowing the camera clock alone does not uniquely determine the correction needed. The plugin would still need GCC-PHAT to measure the composite result. ONVIF is therefore not a substitute; at best, it is a secondary input.

### Production Scenario Decision Matrix

| Scenario | Preferred Method | Rationale |
|----------|-----------------|-----------|
| Normal show (continuous audio, stable network) | **GCC-PHAT** | Higher accuracy (< 1 ms synthetic), no network dependency, no credential management |
| Silent camera (camera mic muted or off) | **Neither** | GCC-PHAT fails; ONVIF provides camera clock but not transport delay, so no actionable correction can be computed |
| High CPU load (many sources, limited cores) | **GCC-PHAT** | ONVIF adds XML parsing and HTTP overhead; reducing GCC-PHAT frequency is simpler than adding ONVIF |
| Startup / stream initialization | **GCC-PHAT** | Converges in ~6–10 s; ONVIF would require camera IP, credentials, and network reachability before any estimate is possible |
| Long show with suspected camera clock drift | **GCC-PHAT + possible ONVIF** | GCC-PHAT already tracks slow drift via EMA accumulation; ONVIF could theoretically disambiguate clock vs. network drift, but accuracy is too marginal to trust |

### Conclusion

ONVIF is **redundant** as a primary sync method and **marginal** as a complement. GCC-PHAT already measures the composite offset that matters for AV sync. ONVIF's narrower camera-clock measurement does not improve the correction strategy enough to justify the integration cost, and its accuracy floor (1–10 ms) is too close to the GCC-PHAT production residual (≤ 20 ms) to provide a decisive benefit.

---

## Verdict & Rationale

**The verdict is: Defer.**

ONVIF clock sync is not adopted for Milestone 1 (v1). The decision is based on three lines of evidence.

**First, accuracy is insufficiently reliable.** The only universally available ONVIF mechanism, `GetSystemDateAndTime`, achieves 2–8 ms accuracy under typical conditions with consumer and prosumer cameras. This range frequently exceeds the 5 ms adoption threshold. NTP internal to the camera is similarly marginal at 1–10 ms, and the plugin cannot read the camera's NTP sync status portably to determine whether the camera is actually well-disciplined. PTP would satisfy the threshold but is infeasible for generic ONVIF Profile S cameras because it requires hardware timestamping and managed network infrastructure that the target production setups do not possess.

**Second, the benefit over GCC-PHAT is unclear.** The existing audio-correlation engine already measures end-to-end offset with < 1 ms synthetic accuracy and applies corrections automatically. ONVIF would measure only the camera-clock component, leaving the RTMP transport layer unobserved. Because the plugin's ultimate goal is to minimize the per-source AV offset that the audience sees, the composite measurement (GCC-PHAT) is more directly actionable than the partial measurement (ONVIF). The hypothetical benefits — silent-period tracking, CPU reduction, and startup assistance — are either unaddressable by ONVIF alone (silent periods still lack transport delay data) or achievable through simpler means (tuning GCC-PHAT parameters).

**Third, integration complexity is non-trivial for a deferred value proposition.** Direct integration requires adding libcurl and an XML parser to the CMake build, managing per-camera IP addresses and credentials in OBS settings, and spawning additional background threads. Security concerns (plaintext credential storage, HTTP vs. HTTPS) add further engineering overhead. An external tool path is even more complex, requiring IPC design, process management, and installer changes. Given that the accuracy ceiling is marginal and the benefit over the existing engine is unproven, the engineering effort is better allocated to Phase 8 (dock UI) and v2 feature work.

**Unblock conditions that would change the verdict to Adopt:**

1. OBS adds a public API that exposes per-source media timing metadata (original PTS, receive timestamp, or decoder latency), eliminating the need for ONVIF by allowing clock-delta inference from the existing media pipeline.
2. The target camera fleet is upgraded to models with guaranteed sub-millisecond NTP discipline or native PTP support, and the production network is equipped with PTP-aware switches and a grandmaster clock.
3. A future plugin architecture requires sync during extended silent periods, and a companion transport-delay measurement mechanism (e.g., RTP timestamp analysis) is developed to pair with ONVIF camera-clock data.

---

## Revisit Criteria

The following observable conditions should trigger a re-evaluation of this verdict. Each criterion is falsifiable — it can be checked against future OBS release notes, camera fleet changes, or plugin requirements.

- **OBS Media Source timing API added:** If OBS adds `obs_source_get_audio_timestamp` returning the original container PTS, or introduces any API for per-source receive timestamps, decoder latency, or network jitter, the primary motivation for ONVIF (camera clock visibility) is reduced or eliminated. **Check:** Review OBS changelog and `libobs/obs-source.h` at each major OBS release.

- **Camera fleet upgrade to PTP-capable or disciplined-NTP models:** If the production environment replaces generic ONVIF Profile S cameras with industrial GigE Vision cameras (PTP hardware timestamping) or broadcast cameras with guaranteed < 1 ms NTP discipline, the accuracy floor improves enough to make ONVIF valuable. **Check:** Inventory camera models and verify PTP support in datasheets or via `GetSystemDateAndTime` + NTP probe tests.

- **Requirement for silent-period drift tracking:** If v2 or later requirements explicitly demand drift compensation during audio silence (e.g., for music performances with long quiet passages), ONVIF becomes the only available clock-delta path. **Check:** Review `REQUIREMENTS.md` and roadmap at the start of each milestone planning cycle.

- **GCC-PHAT production residual exceeds 20 ms consistently:** If live production data shows that GCC-PHAT + EMA cannot hold residual offset below 20 ms due to camera clock drift dominating transport variation, an independent clock measurement might help. **Check:** Analyze diagnostic logs from the analysis thread after each show; look for sustained upward drift in `smoothed_offset_ms` that outpaces the slew-rate cap.

**Recommended review trigger:** Revisit this decision at the start of Milestone 2 planning, or immediately if any of the above criteria is met.
