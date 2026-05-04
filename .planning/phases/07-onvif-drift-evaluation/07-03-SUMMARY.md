# Plan 07-03 Summary: Write ONVIF Clock-Sync Evaluation Recommendation

**Date:** 2026-05-04
**Plan:** 07-03
**Requirement:** DRIFT-02
**Depends on:** 07-01, 07-02

## What was produced

A structured recommendation document: `docs/ONVIF-EVAL.md` synthesizing all Phase 7 research into an actionable, future-maintainer-ready evaluation.

## Document structure

| Section | Status | Notes |
|---------|--------|-------|
| Executive Summary | ✅ | Verdict (**Defer**) stated in bold within first 3 paragraphs; ~200 words |
| Methods Evaluated | ✅ | NTP, PTP, `GetSystemDateAndTime`, OBS Timing Metadata; 5-column comparison table |
| Accuracy Analysis | ✅ | Best/typical/worst scenarios for all three ONVIF methods; ≤ 5 ms assessment; cumulative error with GCC-PHAT; camera caveats |
| Integration Complexity | ✅ | Direct vs. external paths; 4-dimension comparison table; engineering effort estimates |
| Comparison with GCC-PHAT | ✅ | Baseline parameters (2 Hz, 4 s window, EMA α=0.3); value proposition (4 scenarios); decision matrix (5 scenarios); concludes redundant/marginal |
| Verdict & Rationale | ✅ | **Defer** verdict; 3-paragraph rationale; unblock conditions listed |
| Revisit Criteria | ✅ | 4 falsifiable criteria with check instructions; recommended review trigger (Milestone 2 planning) |

## Key findings restated

- `GetSystemDateAndTime` is the only mandatory ONVIF clock primitive; returns camera OS time with no accuracy guarantees.
- NTP on generic cameras: 1–10 ms typical (straddles 5 ms threshold).
- PTP: Infeasible for generic Profile S cameras (rare, requires hardware + PTP-aware switches).
- SOAP RTT method: 2–8 ms typical, marginal due to asymmetry and camera firmware processing.
- OBS provides NO camera clock data; `audio->timestamp` is an OBS-internal mixing cursor.
- GCC-PHAT already measures composite (clock + network) offset with < 1 ms synthetic accuracy.
- **Verdict: DEFER** — no ONVIF mechanism reliably achieves ≤ 5 ms on generic cameras.

## Downstream impact

- Phase 7 is complete. No ONVIF code is integrated into the plugin.
- Phase 8 (Dock UI) can proceed without ONVIF dependencies.
- Milestone 2 planning should revisit this decision using the Revisit Criteria.

## Acceptance criteria check

| Criterion | Status |
|-----------|--------|
| `docs/ONVIF-EVAL.md` exists in `docs/` | ✅ |
| Contains all 6 mandatory sections plus Revisit Criteria | ✅ (7 sections + TOC) |
| Verdict is one of Adopt / Defer / Skip and is in bold | ✅ **Defer** |
| Each section has ≥ 2 paragraphs or a table | ✅ |
| No placeholder text (TODO, FIXME, TBD, XXX) | ✅ |
| Document is ≥ 1500 words | ✅ (~3700 words) |
| References `07-RESEARCH.md` at least once | ✅ (5 references) |
| Revisit Criteria has ≥ 2 falsifiable bullet points | ✅ (4 criteria) |
