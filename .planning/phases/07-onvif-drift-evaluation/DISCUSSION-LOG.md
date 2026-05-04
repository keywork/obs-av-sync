# Phase 7: ONVIF Drift Evaluation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions captured in `07-CONTEXT.md` — this log preserves the analysis and auto-decisions.

**Date:** 2026-05-04
**Phase:** 07-onvif-drift-evaluation
**Mode:** auto-decide (user prefers "research and do best")
**Areas analyzed:** Evaluation Method, ONVIF Spec Focus, Prototype Scope, Target Camera Assumptions, Integration Approach, Accuracy Threshold, Document Structure, OBS Timing Metadata, ONVIF Library Stack

---

## Gray Areas Auto-Resolved

### 1. Evaluation Method
- **Gray area:** Literature review + spec analysis vs. hardware testing
- **Auto-decision:** Start with literature/spec review as primary; hardware testing only if cameras are accessible and spec review shows promise.
- **Rationale:** Phase 7 is a research and documentation phase. Hardware dependency introduces uncontrollable delay. The deliverable is a recommendation document (`docs/ONVIF-EVAL.md`), which can be completed via spec analysis alone.
- **Downstream impact:** Planner should allocate time for reading ONVIF Device Management spec and OBS API docs, not for setting up a hardware test lab.

### 2. ONVIF Spec Focus
- **Gray area:** NTP (Simple) vs. PTP (Precision) vs. ONVIF `GetSystemDateAndTime`
- **Auto-decision:** Evaluate all three, then focus on the one(s) with the best practical accuracy on generic ONVIF cameras.
- **Rationale:** Each mechanism has different accuracy floors and camera support levels. A complete evaluation requires comparing them.
  - NTP: ~1–10 ms over LAN, almost universally supported.
  - PTP: < 1 ms, but requires hardware timestamping and is rare on budget IP cameras.
  - `GetSystemDateAndTime`: SOAP round-trip subject to network jitter; accuracy depends on RTT.
- **Downstream impact:** Researcher must review ONVIF Core Specification §Device Service — Clock Synchronization primitives.

### 3. Prototype Scope
- **Gray area:** Minimal code prototype vs. pure document evaluation
- **Auto-decision:** Document evaluation first; prototype only if spec review shows clear promise and a feasible integration path.
- **Rationale:** Building a prototype before understanding the spec risks wasted effort. The success criteria for this phase are document-oriented ("`docs/ONVIF-EVAL.md` exists and contains a clear verdict"). A prototype is only required for an "adopt" verdict.
- **Downstream impact:** Planner should structure the phase as: Research → Document → Gate (prototype yes/no) → Prototype (conditional).

### 4. Target Camera Assumptions
- **Gray area:** Generic ONVIF cameras vs. specific models
- **Auto-decision:** Generic ONVIF Profile S/G cameras; document camera-specific limitations as caveats.
- **Rationale:** The plugin is meant to work with arbitrary ONVIF cameras, not just the current production setup. A generic evaluation produces a more durable recommendation.
- **Downstream impact:** Researcher should not assume specific camera firmware behaviors unless they are representative of the broader Profile S/G ecosystem.

### 5. Integration with Existing Plugin
- **Gray area:** Direct ONVIF connection from plugin vs. external tool
- **Auto-decision:** Evaluate both paths; document why direct connection may be infeasible from an OBS plugin.
- **Rationale:** OBS plugins run inside OBS's process. Making outbound SOAP/HTTP calls from a plugin may block threads, introduce security concerns, or conflict with OBS's threading model. An external tool decouples the network stack but adds inter-process communication complexity.
- **Downstream impact:** Researcher must assess OBS plugin threading constraints (no blocking on audio thread, but background threads are allowed) and evaluate IPC options (named pipe, localhost socket, file polling).

---

## Additional Decisions Derived from Analysis

### 6. Accuracy Threshold (D-11)
- **Decision:** Adoption requires ≤ 5 ms clock-delta accuracy.
- **Evidence:** ROADMAP.md success criterion #2 states: "If adopted: a prototype shows clock-delta accuracy within 5 ms for the target cameras."
- **Comparison baseline:** Phase 6 GCC-PHAT achieves < 1 ms synthetic accuracy and ≤ 20 ms live production residual. ONVIF must offer a meaningful complement (e.g., handles silent audio, reduces CPU) to justify integration.

### 7. OBS Timing Metadata Investigation (D-15 / D-16)
- **Decision:** Investigate whether OBS Media Sources expose timing metadata (PTS, DTS, wall-clock) through the `obs_source_t` API.
- **Evidence:** `docs/ARCHITECTURE.md` notes that chunks are tagged with OBS-side `uint64_t` timestamps. It is unknown whether OBS also timestamps the *receive* time of RTMP frames. If it does, clock deltas might be inferable without ONVIF.
- **Research task:** Check `obs_source_get_sync_offset`, `obs_source_get_audio_timestamp`, and `obs_output` timing APIs.

### 8. ONVIF Library / Prototype Stack (D-17 / D-18)
- **Decision:** If prototype is warranted, prefer a minimal C/C++ ONVIF client (hand-rolled HTTP+XML or gSOAP stubs) over a full stack. License must be GPL-2.0-or-later compatible.
- **Evidence:** PROJECT.md explicitly excludes Apache-2.0 with patent clauses. Many ONVIF libraries are Apache-2.0 licensed. gSOAP is GPL-compatible but generates large codebases. Hand-rolled is more code but fully controlled.

### 9. Document Structure (D-13)
- **Decision:** `docs/ONVIF-EVAL.md` must contain six mandatory sections: Methods Tested, Accuracy Measured, Integration Complexity, Verdict, Rationale, Revisit Criteria.
- **Evidence:** This structure satisfies the ROADMAP.md plan #3 and ensures the document is actionable for future maintainers.

### 10. Verdict Criteria (D-14)
- **Decision:** Three verdicts with explicit gates:
  - **Adopt:** ≤ 5 ms accuracy + feasible integration + clear benefit over GCC-PHAT alone.
  - **Defer:** Promise shown but blocked by complexity, variability, or API limits. Include unblock condition.
  - **Skip:** Accuracy > 5 ms or infeasible. Document blocker for future reference.
- **Evidence:** ROADMAP.md success criterion #3 requires that a deferred/skipped verdict "explains the specific technical blocker so the decision can be revisited later without re-doing the research."

---

## Assumptions & Confidence Levels

| # | Assumption | Confidence | Evidence |
|---|-----------|-----------|----------|
| A-01 | ONVIF `GetSystemDateAndTime` is universally supported on Profile S cameras. | Likely | ONVIF Core spec mandates Device Service for conformance. |
| A-02 | NTP client status is readable via ONVIF on most cameras. | Likely | Common feature, but some vendors hide it behind proprietary extensions. |
| A-03 | OBS does NOT expose RTMP ingest wall-clock timing to plugins. | Unclear | No evidence found in codebase; requires API research. |
| A-04 | A direct ONVIF SOAP client can run on a background pthread inside an OBS plugin without crashing OBS. | Likely | OBS plugins routinely spawn threads (Phase 6 analysis thread). Network I/O on a non-RT thread should be safe. |
| A-05 | PTP is not available on the target production cameras. | Likely | Budget IP cameras rarely support IEEE 1588 PTP. |
| A-06 | The current GCC-PHAT + EMA baseline is sufficient for v1 drift tracking without ONVIF. | Confident | Phase 6 tests show EMA tracks drift; 60-minute soak criterion is ≤ 20 ms residual. |

---

## Auto-Resolved Checklist

- [x] Evaluation Method — Literature/spec primary; hardware conditional
- [x] ONVIF Spec Focus — All three evaluated; practical accuracy prioritized
- [x] Prototype Scope — Document first; prototype gated on spec review
- [x] Target Camera Assumptions — Generic ONVIF Profile S/G
- [x] Integration Approach — Direct and external both evaluated
- [x] Accuracy Threshold — ≤ 5 ms for adopt verdict
- [x] Comparison Baseline — Phase 6 GCC-PHAT + EMA
- [x] Document Structure — Six mandatory sections in `docs/ONVIF-EVAL.md`
- [x] OBS Timing Metadata — Investigate Media Source timing APIs
- [x] ONVIF Library Decision — Minimal C/C++, GPL-compatible

---

## No Corrections Applied

All assumptions were auto-resolved without user correction (auto-decide mode).

---

## External Research Needed

1. **ONVIF Core Specification** — Device Management Service, clock synchronization primitives (NTP/PTP configuration, `GetSystemDateAndTime` behavior).
2. **OBS Plugin API** — Whether `obs_source_t` or `obs_output` exposes RTMP ingest timing metadata (PTS, receive timestamp).
3. **ONVIF Client Libraries** — License compatibility survey (GPL-2.0-or-later compatible options).

---

*Phase: 07-onvif-drift-evaluation*
*Discussion log generated: 2026-05-04*
