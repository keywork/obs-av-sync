# Plan 07-01 Summary: ONVIF Clock-Sync Feasibility Research

**Date:** 2026-05-04
**Plan:** 07-01
**Requirement:** DRIFT-02

## What was investigated

1. ONVIF Device Management service clock primitives (GetSystemDateAndTime, SetSystemDateAndTime, NTP config, PTP)
2. NTP accuracy on IP cameras (RFC 5905, NIST measurements, industry sources)
3. PTP accuracy and prevalence (industrial camera docs, hardware requirements)
4. SOAP round-trip timing analysis (RTT, asymmetry, jitter)
5. ONVIF client library survey (gSOAP, libcurl+expat, libonvif, rapidonvif)
6. Integration path feasibility (direct from plugin vs. external helper)

## Key findings

- `GetSystemDateAndTime` is the only mandatory clock primitive; it returns camera OS time with no accuracy guarantees
- NTP on generic cameras: 1–10 ms typical, straddles the 5 ms adoption threshold
- PTP: Infeasible for generic Profile S cameras (rare, requires hardware timestamping + PTP-aware switches)
- SOAP RTT method: 2–8 ms typical, marginal due to asymmetry and camera firmware processing delays
- Client libraries: gSOAP (GPL v2), hand-rolled libcurl+expat (MIT), libonvif (LGPL-2.1) are all GPL-compatible
- Direct integration from plugin is preferred over external helper (lower latency, simpler deployment)

## Verdict

**Defer** ONVIF clock sync for v1. No mechanism reliably achieves ≤ 5 ms on generic cameras. GCC-PHAT already measures the composite (clock + network) offset.

## Acceptance criteria check

| Criterion                                                            | Status |
| -------------------------------------------------------------------- | ------ |
| `07-RESEARCH.md` contains `## ONVIF Device Management Clock Primitives` | ✅ Section 1 |
| ≥ 4 clock-relevant operations documented                             | ✅ GetSystemDateAndTime, SetSystemDateAndTime, GetNTP/SetNTP, PTP |
| `07-RESEARCH.md` contains `## NTP Accuracy Evaluation`                 | ✅ Section 2 |
| Table with ≥ 3 accuracy scenarios                                    | ✅ 4 scenarios |
| `07-RESEARCH.md` contains `## PTP Accuracy Evaluation`                 | ✅ Section 3 |
| `07-RESEARCH.md` contains `## GetSystemDateAndTime Round-Trip Analysis`| ✅ Section 4 |
| `07-RESEARCH.md` contains `## ONVIF Client Library Survey`             | ✅ Section 5 |
| Table with ≥ 3 libraries and license compatibility                   | ✅ 5 libraries |
| `07-RESEARCH.md` contains `## Integration Path Feasibility`            | ✅ Section 6 |
| Recommended path stated with rationale                               | ✅ Direct integration preferred |
