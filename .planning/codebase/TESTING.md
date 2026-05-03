# Testing

## Current State

There is **no `tests/` directory** and no test harness of any kind in the repository at this time. The codebase is in early phases (Phase 2 of 7 complete). All current verification is manual/observational: run OBS, add the filter to a source, and check the OBS log for diagnostic output.

---

## Test Infrastructure

### Existing
- None. No `tests/` directory, no test CMake targets, no test runner configured.

### Template Scaffolding
- The project derives from `obs-plugintemplate`. The template does not include a test harness; it provides only build and format-check CI. Any test harness must be added from scratch under `tests/`.

### Expected Future Setup (per ROADMAP / CLAUDE.md)
- A `tests/` directory should be added when the DSP layer lands (Phase 3)
- Unit tests are expected to be standalone C/C++ executables — no OBS runtime required for pure DSP functions
- CMake integration: add a `tests/CMakeLists.txt` and call `add_subdirectory(tests)` from the root, with a `CTest` or lightweight custom runner

---

## CI Test Checks

The GitHub Actions workflows under `.github/workflows/` cover:

| Workflow | File | What it checks |
|---|---|---|
| **Build** | `build-project.yaml` | Full CMake build on Windows, macOS, Linux (via `push.yaml` / `dispatch.yaml`) |
| **Format (clang-format)** | `check-format.yaml` | All C/C++ source files comply with `.clang-format` (clang-format ≥ 16, run on ubuntu-24.04) — fails CI on any diff |
| **Format (gersemi)** | `check-format.yaml` | All `CMakeLists.txt` / `.cmake` files comply with `.gersemirc` (120-col, 2-space indent) — fails CI on any diff |
| **PR automation** | `pr-pull.yaml` | Template PR workflow (not test-related) |

**There are no CI test jobs.** The `check-format` workflow runs two format checkers (`clang-format` and `gersemi`) but zero functional tests.

Format checks can be run locally:
```sh
build-aux/run-clang-format
build-aux/run-gersemi
```

---

## Planned Testing

### From ROADMAP.md — Phase 3 (GCC-PHAT unit tests)
> "Unit-test with synthetic signals: known delay + noise + known SNR."
>
> **Exit criteria:** synthetic-signal tests pass across ±500 ms delays with < 1 ms error at SNR ≥ 10 dB.

This is the first concrete test milestone. The synthetic signal approach:
- Generate a reference tone / noise burst
- Produce a copy delayed by a known integer/fractional number of samples (e.g., 0 ms, ±10 ms, ±100 ms, ±500 ms)
- Mix in Gaussian noise at specified SNR levels (≥ 10 dB required; test at lower SNRs too for characterization)
- Run GCC-PHAT; assert that peak-bin + parabolic interpolation recovers the delay within 1 ms

### From CLAUDE.md
> "Add [test harness] under `tests/` when the DSP layer lands (unit-test GCC-PHAT with synthetic delayed signals; see roadmap)."

Confirms tests live in `tests/` and are gated on Phase 3 DSP code landing.

### Phase 4 (Continuous Engine)
No explicit test plan stated, but the exit criterion ("< 20 ms residual for one hour") implies a long-running integration/soak test, not a fast unit test.

---

## Testing Gaps

| Component | Status | Gap |
|---|---|---|
| `av_sync_ring_create` / `av_sync_ring_destroy` | Untested | No tests for capacity=0, sample_rate=0 guard clauses; no leak checks |
| `av_sync_ring_write` | Untested | Wrap-around correctness, overwrite-oldest semantics, oversized-chunk handling not verified by any test |
| `av_sync_ring_get_stats` | Untested | Timestamp arithmetic (`oldest_timestamp_ns` derivation) unverified |
| `av_sync_filter_audio` downmix | Untested | Multi-channel averaging (`inv_planes`) logic has no unit test |
| `av_sync_filter_audio` diagnostics | Manual only | Rollup logging verified by reading OBS log; no automated assertion |
| GCC-PHAT (not yet written) | Future | Phase 3 is the first phase that adds testable DSP code |
| Continuous sync engine (not yet written) | Future | Phase 4 engine will need integration + soak testing |
| OBS API integration | Runtime only | Filter register/create/destroy exercised only by loading OBS; not mockable without OBS runtime |

### Critical Untested Paths
1. **Ring buffer wrap-around**: `av_sync_ring_write` has two-memcpy split logic that is easy to get wrong at buffer boundaries — currently verified only by inspection
2. **Timestamp accuracy**: `newest_timestamp_ns` and `oldest_timestamp_ns` calculations depend on `(n-1) * 1e9 / sample_rate` arithmetic — off-by-one errors undetectable without tests
3. **Lazy ring allocation failure**: if `obs_get_audio_info()` returns false on first callback, `sample_rate` stays 0 and ring is never created — subsequent callbacks silently skip ring writes with no logged warning

---

## Recommended Approach

### Unit Test Framework
Use a minimal header-only C test framework (e.g., [munit](https://nemequ.github.io/munit/) or a simple hand-rolled assert harness). The DSP layer is pure C with no OBS dependency — test executables can link `ring_buffer.c` and the future `gcc_phat.c` directly without an OBS runtime.

### Synthetic Signal Strategy (Phase 3)
```
tests/
  test_ring_buffer.c   — ring write/wrap/stats unit tests
  test_gcc_phat.c      — synthetic delay recovery tests
  signal_gen.h         — helper: generate sine, noise, shifted copy at known delay
  CMakeLists.txt       — add_executable + CTest registration
```

Key test cases for GCC-PHAT:
- Zero delay (same signal both channels) → offset ≈ 0 samples
- Known integer delay (e.g., 480 samples = 10 ms at 48 kHz) → recovered within ±1 sample
- Sub-sample delay via sinc interpolation of reference → parabolic interpolation accuracy
- ±500 ms delays (± 24000 samples at 48 kHz) — boundary of expected operating range
- SNR sweep: 30 dB, 20 dB, 10 dB (pass), 5 dB (characterize, may fail)
- Edge: silence input → should not crash, should return low-confidence result

### Ring Buffer Tests
- Write 0 samples (guard clause)
- Write exactly `capacity` samples (full overwrite)
- Write `capacity + 1` samples (oversized-chunk path)
- Sequential writes that wrap around multiple times — verify `total_written` and `filled`
- Stats after 0 writes: `filled == 0`, timestamps == 0

### CMake Integration
```cmake
# tests/CMakeLists.txt
add_executable(test_ring_buffer test_ring_buffer.c ../src/ring_buffer.c)
target_include_directories(test_ring_buffer PRIVATE ../src)
add_test(NAME ring_buffer COMMAND test_ring_buffer)
```
Enable in root `CMakeLists.txt` behind a `BUILD_TESTING` option so CI can opt in without requiring OBS headers for the test binary.
