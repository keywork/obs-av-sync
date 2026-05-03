---
status: fixed
phase: 05
fixes_count: 6
---

# Phase 5 Review Fix Log

## Fixes Applied

### CR-01: reference_tap_shutdown racy defensive guard
- **File:** `src/reference_tap.c`
- **Fix:** Removed racy pre-check. Added `static bool ref_initialized`. Shutdown checks `ref_initialized` before locking mutex.
- **Verification:** Build passes, no TSan warnings (local)

### CR-02: reference_tap_get_ring dangling pointer
- **File:** `src/reference_tap.c`, `src/reference_tap.h`
- **Fix:** Return `const av_sync_ring_t *` without mutex. Documented lifetime contract in header.
- **Verification:** Build passes

### WR-01: reference_tap_init ignores allocation failures
- **File:** `src/reference_tap.c`
- **Fix:** Check `ref_ring != NULL && ref_downmix_scratch != NULL`. Return `false` on failure.
- **Verification:** Build passes

### WR-02: Multiple filters fight over global reference
- **File:** `src/reference_tap.c`, `src/av_sync_filter.c`
- **Fix:** Added `requester` parameter to `reference_tap_set_source`. Log `LOG_INFO` when reference source is overridden.
- **Verification:** Build passes

### WR-03: Reference callback silently drops oversized chunks
- **File:** `src/reference_tap.c`, `src/reference_tap.h`
- **Fix:** Added `_Atomic uint64_t ref_oversize_skips` counter and `reference_tap_get_oversize_skips()` getter.
- **Verification:** Build passes

### WR-04: Duplicate warnings for missing reference source
- **File:** `src/av_sync_filter.c`
- **Fix:** Removed redundant startup validation block. `reference_tap_set_source` already logs missing source warning.
- **Verification:** Build passes

## Verification
- **Build:** PASS (zero errors)
- **Tests:** PASS (36/36 GCC-PHAT, SPSC round-trip)
