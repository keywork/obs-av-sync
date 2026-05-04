# Phase 6 Discussion Log

**Date:** 2026-05-03
**Mode:** Auto (--auto)

## Gray Areas Auto-Decided

1. **Analysis thread architecture** → Per-filter pthread (D-01, D-02, D-03)
2. **Analysis cadence and window size** → 2 Hz, 4-second window (D-04, D-05, D-06)
3. **Smoother algorithm** → EMA with confidence gating + slew-rate cap (D-07, D-08, D-09)
4. **Confidence threshold and status states** → Threshold < 2.0; 3 states (D-10, D-11)
5. **Offset application** → Analysis thread directly calls obs_source_set_sync_offset (D-12, D-13, D-14)
6. **Drift tracking** → EMA accumulation, no periodic reset (D-15, D-16)
7. **Thread safety** → SPSC ring reads, no locks needed (D-17, D-18, D-19)
8. **Graceful degradation** → Hold last value on source removal/mute (D-20, D-21, D-22)
