# Phase 5 Discussion Log

**Date:** 2026-05-03
**Mode:** Auto (--auto)

## Gray Areas Auto-Decided

1. **Reference tap architecture** → Global singleton (D-01, D-02)
2. **Reference source lifecycle** → Log warning and hold last offset (D-03, D-04)
3. **Enable/disable toggle behavior** → Continue audio tap, stop analysis (D-05, D-06)
4. **Settings scope** → Per-filter properties (D-07, D-08)
5. **Settings serialization** → `reference_source_name` + `sync_enabled` (D-09, D-10, D-11)
6. **Filter properties UI** → `get_properties` + `update` hooks (D-12, D-13)
7. **OBS Frontend API** → ENABLE_FRONTEND_API=ON (D-14)
