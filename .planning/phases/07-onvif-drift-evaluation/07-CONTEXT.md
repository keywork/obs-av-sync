# Phase 7: ONVIF Drift Evaluation - Context

**Gathered:** 2026-05-04
**Status:** Ready for planning

<domain>
## Phase Boundary

Document whether ONVIF clock sync is a viable complement to GCC-PHAT for tracking slow drift, with a concrete recommendation.

**In scope:**
- Research ONVIF Device Management spec (NTP/PTP/GetSystemDateAndTime) for clock-sync feasibility
- Assess what OBS exposes about RTMP/Media Source timing metadata
- Evaluate whether clock deltas are accessible from the plugin layer without a direct ONVIF connection
- Attempt a minimal proof-of-concept or document why it is not feasible
- Measure achievable accuracy vs. the existing Phase 6 GCC-PHAT + EMA smoother
- Write `docs/ONVIF-EVAL.md` with method, accuracy, complexity, verdict, and rationale

**Out of scope:**
- Full ONVIF client implementation integrated into the plugin (only prototype if warranted)
- Camera auto-discovery via ONVIF (v2 requirement, deferred)
- Changes to the GCC-PHAT engine or smoother (those are locked in Phase 6)
- UI for ONVIF configuration (Phase 8 dock UI is separate)

**Requirements:** DRIFT-02
</domain>

<decisions>
## Implementation Decisions

### Evaluation Method
- **D-01:** Start with **literature and specification review** as the primary evaluation method. Review the ONVIF Device Management Service spec (Core Specification, Device Service WSDL), focusing on clock synchronization primitives.
- **D-02:** **Hardware testing is secondary and conditional** — only proceed to testing against physical cameras if the spec review indicates feasibility AND cameras are accessible in the target production environment. Do not block the phase on hardware access.

### ONVIF Spec Focus
- **D-03:** Evaluate **all three clock-sync mechanisms** — NTP (simple, widely supported), PTP (precision, less common on budget cameras), and ONVIF `GetSystemDateAndTime` / `SetSystemDateAndTime` — then focus on the one(s) that offer the best practical accuracy on generic ONVIF cameras.
- **D-04:** Document the **accuracy floor** for each mechanism: NTP typically ~1–10 ms over LAN; PTP typically < 1 ms (sub-millisecond) but requires hardware support; `GetSystemDateAndTime` is a SOAP round-trip subject to network jitter.

### Prototype Scope
- **D-05:** **Document evaluation first.** Produce the structured analysis in `docs/ONVIF-EVAL.md` before writing any code.
- **D-06:** A **code prototype is only built if the spec review shows clear promise**: specifically, if a mechanism can theoretically achieve ≤ 5 ms clock-delta accuracy AND a feasible integration path exists. If the review reveals blockers (no timing metadata exposed, blocking network calls required inside OBS, license incompatibility), skip the prototype and document the blockers.

### Target Camera Assumptions
- **D-07:** Assume **generic ONVIF Profile S/G cameras** — the evaluation must apply to the broad class of IP cameras that advertise ONVIF support, not just the specific models in the current production setup.
- **D-08:** Document **camera-specific limitations** as caveats (e.g., "Some cameras expose NTP client status only via vendor-specific ONVIF extensions"). The verdict must hold for a generic profile, with notes where model-specific behavior would change the outcome.

### Integration with Existing Plugin
- **D-09:** Evaluate **two integration paths** in parallel:
  1. **Direct ONVIF connection from the OBS plugin** — assess whether the plugin can make non-blocking SOAP/HTTP requests without interfering with OBS's real-time audio or graphics threads.
  2. **External tool / helper process** — a standalone utility or script that queries cameras and writes timing data to a file, socket, or shared memory that the plugin reads.
- **D-10:** Document the **specific technical blockers** for each path. For the direct path, the critical question is: can an OBS plugin safely open TCP connections and parse XML/SOAP on a background thread without violating OBS's threading model? For the external tool path, the critical question is: how does the plugin receive clock-delta updates (file polling, named pipe, localhost socket)?

### Accuracy Threshold & Comparison Baseline
- **D-11:** The **adoption threshold is ≤ 5 ms clock-delta accuracy** (from ROADMAP.md success criteria). Anything worse than 5 ms is not a viable complement to GCC-PHAT, which already achieves < 1 ms residual offset in synthetic tests and ≤ 20 ms in live production.
- **D-12:** Compare ONVIF approaches against the **Phase 6 baseline**: GCC-PHAT (2 Hz, 4 s window, < 1 ms synthetic accuracy) + EMA smoother (α = 0.3, slew-rate cap ±20 ms). ONVIF is only valuable if it improves drift tracking, reduces CPU load, or provides a backup when audio correlation fails.

### Document Structure
- **D-13:** `docs/ONVIF-EVAL.md` must contain these mandatory sections:
  1. **Methods Tested** — which ONVIF mechanisms were evaluated and how
  2. **Accuracy Measured** — expected or measured accuracy for each mechanism
  3. **Integration Complexity** — effort to integrate into the plugin (direct vs. external)
  4. **Verdict** — one of: **Adopt** / **Defer** / **Skip**
  5. **Rationale** — data and reasoning behind the verdict
  6. **Revisit Criteria** — specific conditions under which the decision should be re-evaluated (e.g., "if OBS adds Media Source timing metadata API")

### Verdict Criteria
- **D-14:** Define clear gates for each verdict:
  - **Adopt:** ≤ 5 ms accuracy AND feasible integration path AND clear benefit over GCC-PHAT alone (e.g., reduced CPU, handles silent audio).
  - **Defer:** Shows promise (≤ 5 ms theoretically possible) but blocked by complexity, camera variability, OBS API limitations, or insufficient benefit/cost ratio. Include a concrete unblock condition.
  - **Skip:** Accuracy > 5 ms or technically infeasible from both plugin and external contexts. Document the blocker so it does not need to be re-researched.

### OBS Timing Metadata Investigation
- **D-15:** Investigate whether OBS **Media Sources** (RTMP ingest) expose any timing metadata (PTS, DTS, wall-clock, or stream-time) through the `obs_source_t` API. If OBS already timestamps incoming frames with receiver-side wall clock, clock deltas may be inferable without ONVIF.
- **D-16:** Check `obs_source_get_sync_offset`, `obs_source_get_audio_timestamp`, and any `obs_output` timing APIs for clues about stream-level timing. Document findings in `docs/ONVIF-EVAL.md` §OBS Timing Exposure.

### ONVIF Library / Prototype Stack (Conditional)
- **D-17:** If a prototype is warranted, prefer a **minimal C/C++ ONVIF client** over a full stack. Options to evaluate:
  - Hand-rolled HTTP POST + XML parser (lightest, full control, more code)
  - gSOAP-generated stubs from ONVIF WSDL (standard, but large generated code and potential license concerns)
  - Existing lightweight ONVIF C libraries (evaluate license compatibility with GPL-2.0-or-lateer)
- **D-18:** **License constraint:** Any library used must be compatible with GPL-2.0-or-later. Apache-2.0 with patent clauses is explicitly excluded per PROJECT.md.

### Claude's Discretion
- Exact depth of ONVIF spec review (paragraph summary vs. section-by-section analysis) — recommend section-by-section for the Device Service clock primitives, high-level for other services.
- Formatting and tone of `docs/ONVIF-EVAL.md` — follow the existing `docs/ARCHITECTURE.md` style (technical, concise, numbered sections).
- Whether to include a brief comparison table (NTP vs. PTP vs. GetSystemDateAndTime) in the doc — recommended for readability.
</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements & Roadmap
- `.planning/REQUIREMENTS.md` §DRIFT-02 — ONVIF evaluation requirement
- `.planning/ROADMAP.md` §Phase 7 — Phase goal, plans, and success criteria
- `.planning/PROJECT.md` — Project vision, constraints, and key decisions

### Architecture & Existing Docs
- `docs/ARCHITECTURE.md` — Plugin data flow and OBS API touchpoints
- `docs/ROADMAP.md` — Public-facing roadmap (synchronized with `.planning/ROADMAP.md`)

### Existing Code (read before planning any prototype)
- `src/av_sync_filter.c` — Current filter with Phase 6 continuous sync engine
- `src/gcc_phat.h` / `src/gcc_phat.cpp` — GCC-PHAT estimator (baseline for comparison)
- `src/reference_tap.h` / `src/reference_tap.c` — Reference tap architecture

### External Specifications
- ONVIF Core Specification (latest) — Device Management Service, `GetSystemDateAndTime`, NTP/PTP configuration
- ONVIF Device Service WSDL — SOAP operations for clock synchronization
- OBS Plugin API documentation: https://docs.obsproject.com/ — check for Media Source timing metadata APIs

### Prior Phase Decisions (relevant constraints)
- Phase 6 CONTEXT.md — GCC-PHAT + EMA smoother baseline (D-04 through D-16)
- Phase 5 CONTEXT.md — Reference tap and per-filter configuration (D-01, D-14)
- Phase 3 CONTEXT.md — Threading constraints: no blocking or heap allocation on OBS audio callbacks
</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/reference_tap.c` — If external tool approach is chosen, the reference tap pattern (background data collection + shared ring) can be adapted for clock-delta ingestion.
- `obs_log(LOG_INFO/LOG_WARNING, ...)` — Established logging pattern for diagnostic output.

### Established Patterns
- **No blocking on real-time threads** (Phase 3, D-01/D-02). Any ONVIF network I/O must run on a dedicated background thread, never the audio callback or OBS graphics thread.
- **C for OBS entry point, C++ for DSP/state** (PROJECT.md). If a prototype is built, it should follow this split: C-compatible header, C++ implementation if complex XML/SOAP parsing is needed.
- **Per-filter pthread lifecycle** (Phase 6, D-01/D-02). A background ONVIF polling thread would follow the same create/join pattern as the analysis thread.

### Integration Points
- `av_sync_filter.c` — If ONVIF is adopted, clock-delta input would feed into the smoother alongside GCC-PHAT estimates.
- `obs_module_load` / `obs_module_unload` — Global ONVIF client init/cleanup would hook here (if direct integration).
- CMakeLists.txt — Any new ONVIF dependency (library or generated stubs) would be added here; must preserve cross-platform builds.
</code_context>

<specifics>
## Specific Ideas

- The target production setup uses 2–4 IP cameras connected via RTMP proxy (Media Sources in OBS). ONVIF clock sync may help with slow drift, but the primary sync mechanism (GCC-PHAT + EMA) is already built and working. This phase is about documenting whether ONVIF adds value, not replacing the audio path.
- "Both a fixed per-camera offset and ongoing drift during long shows are observed" (PROJECT.md). ONVIF could theoretically address the drift component by keeping camera clocks synchronized to a common reference, but only if the RTMP ingest path preserves that timing.
- The user prefers "research and do best" — decisions are locked based on technical merit, not aesthetic preference.
</specifics>

<deferred>
## Deferred Ideas

- **ONVIF-based auto-discovery of camera sources** — This is a v2 requirement (deferred per REQUIREMENTS.md). If ONVIF is adopted in this phase, auto-discovery becomes more feasible but remains out of scope.
- **Direct ONVIF PTZ or metadata integration** — Out of scope; this phase is strictly about clock sync for drift evaluation.
- **Replacing GCC-PHAT with ONVIF** — Not the goal. ONVIF is evaluated as a *complement*, not a replacement.
- **Per-camera NTP server configuration from the plugin** — Even if ONVIF is adopted, actually reconfiguring camera NTP settings is likely out of scope for v1; reading clock deltas is the evaluation boundary.

### Reviewed Todos (not folded)
- None — no pending todos were matched to Phase 7 scope.
</deferred>

---

*Phase: 07-onvif-drift-evaluation*
*Context gathered: 2026-05-04*
