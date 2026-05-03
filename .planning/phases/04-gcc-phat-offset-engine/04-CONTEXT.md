# Phase 4 Context — GCC-PHAT Offset Engine

## Decisions

### D-01: PFFFT Vendoring — FetchContent from GitHub
- **Approach:** CMake `FetchContent_Declare` + `FetchContent_MakeAvailable` on the `hayguen/pffft` repository.
- **Version pin:** Use a specific Git tag or commit hash (to be researched in plan-phase).
- **Rationale:** No git submodules to manage; no vendored source files in repo; version pin is explicit in CMakeLists.txt; works with CI caching.
- **License:** PFFFT is BSD-like (no GPL pollution). Compatible with GPL-2.0-or-later.
- **Build integration:** PFFFT provides a CMakeLists.txt with `add_library(pffft)`. We link `pffft` target to our plugin target via `target_link_libraries`.
- **Fallback:** If FetchContent fails (offline build), document that `FETCHCONTENT_SOURCE_DIR_PFFFT` can be set to a local clone.

### D-02: Analysis Parameters — 48 kHz, 2-second window, 131072-point FFT
- **Sample rate:** 48 kHz (matches OBS source rate; no resampler needed in Phase 4).
- **Window size:** 2 seconds = 96000 samples.
- **FFT size:** Next power of 2 ≥ 96000 = 131072 (128k).
- **Unambiguous range:** ±65536 samples = ±1365 ms, exceeding the ±500 ms requirement.
- **Frequency resolution:** 48000 / 131072 ≈ 0.366 Hz per bin.
- **Time resolution per sample:** 1/48000 ≈ 0.021 ms.
- **Sub-sample interpolation:** Parabolic interpolation on the correlation peak gives fractional-sample accuracy.
- **Rationale:** 48 kHz avoids introducing a resampler dependency. 2 s window gives sufficient averaging for 10 dB SNR. 128k FFT is computationally reasonable for a ~2 Hz analysis rate.

### D-03: Test Signal Generation — Hand-rolled sin + Box-Muller Gaussian
- **Method:** `sinf(2.0f * M_PI * freq * t)` for deterministic sine waves, plus Gaussian noise via Box-Muller transform on `rand()` / `drand48()`.
- **Delay injection:** Shift the delayed signal by a known integer sample count (±500 ms = ±24000 samples at 48 kHz).
- **SNR control:** Compute signal power and noise power, scale noise amplitude to achieve target SNR (10–40 dB).
- **Rationale:** Zero dependencies, deterministic, cross-platform, works in both C and C++ test files. Matches `float` precision of ring buffer.

### D-04: Confidence Metric — Peak-to-Sidelobe Ratio (PSR) primary
- **Primary metric:** PSR = peak_magnitude / mean_sidelobe_magnitude.
  - Sidelobe = all correlation bins excluding peak ±3 bins (to avoid including the peak lobe).
- **Secondary metric:** Normalized peak = peak_magnitude / RMS(correlation).
- **Return value:** `estimate_offset` returns both `offset_ns` and `confidence` (a float, where higher = more confident).
- **Rationale:** PSR is standard for GCC-PHAT. It degrades predictably with SNR. The secondary metric provides a cross-check for the smoother (Phase 6).

### D-05: Language — C++ for gcc_phat, C preserved for existing files
- **New files:** `src/gcc_phat.cpp`, `src/gcc_phat.h`.
- **Existing files:** `plugin-main.c`, `av_sync_filter.c`, `ring_buffer.c` remain C.
- **CMake:** Add `src/gcc_phat.cpp` to `target_sources`. No `CMAKE_CXX_STANDARD` set explicitly — rely on default (C++17 on modern compilers, which is fine).
- **Rationale:** `std::complex<float>` makes the cross-spectrum and PHAT weighting code much cleaner and less error-prone than manual complex arithmetic in C. PFFFT is a C API callable from C++ without extern "C" wrappers.
- **Header compatibility:** `gcc_phat.h` must be C-compatible (use `extern "C"`) so `av_sync_filter.c` can include it and call `estimate_offset`.

### D-06: GCC-PHAT Pipeline
1. **Hann window** both `ref` and `target` arrays (in-place or scratch buffer).
2. **PFFFT forward** on both windowed signals → complex spectra.
3. **Cross-spectrum:** `X = F_ref * conj(F_target)` (element-wise complex multiply).
4. **PHAT whitening:** `X_ph = X / |X|` (normalize magnitude to 1, keep phase).
5. **PFFFT inverse** on `X_ph` → generalized cross-correlation (GCC) vector.
6. **Peak pick:** Find the maximum magnitude bin in the GCC vector.
7. **Parabolic interpolation:** Fit a parabola to the peak and its two neighbors; compute fractional peak offset.
8. **Convert to time:** `offset_samples = (peak_bin > N/2) ? (peak_bin - N) : peak_bin`; `offset_ns = offset_samples * 1e9 / sample_rate`.
9. **Confidence:** Compute PSR on the GCC vector.

### D-07: Test Coverage
- **Synthetic delay tests:** Delays of -500 ms, -250 ms, -100 ms, -10 ms, 0 ms, +10 ms, +100 ms, +250 ms, +500 ms.
- **SNR sweep:** 10 dB, 20 dB, 30 dB, 40 dB at each delay.
- **Signal content:** 1 kHz sine wave (simplest), plus band-limited pink noise (more realistic).
- **Success threshold:** |measured_offset - true_offset| < 1 ms for ALL tests at SNR ≥ 10 dB.
- **Confidence check:** At each SNR level, assert that average confidence decreases as SNR decreases.

### D-08: FFT Library Interface
- **PFFFT setup:** Create a `PFFFT_Setup *` once per window size via `pffft_new_setup(FFT_SIZE, PFFFT_COMPLEX)`.
- **Work buffer:** PFFFT requires an aligned work buffer (`pffft_simd_size()` bytes aligned). Allocate once per setup.
- **Input/output:** PFFFT operates on `float *` arrays of size `2 * FFT_SIZE * sizeof(float)` for complex transforms.
- **Cleanup:** Call `pffft_destroy_setup` when done.
- **Thread safety:** One `PFFFT_Setup` per analysis thread (no sharing across threads).

## Canonical References
- PFFFT repository: https://github.com/hayguen/pffft
- Knapp & Carter 1976: "The Generalized Correlation Method for Estimation of Time Delay"
- CMake FetchContent docs: https://cmake.org/cmake/help/latest/module/FetchContent.html
- OBS plugin API: https://docs.obsproject.com/

## Prior Decisions Carried Forward
- C11 atomics for thread safety (Phase 3, D-01/D-02)
- `_Atomic size_t total_written` with acquire/release ordering (Phase 3)
- Heap allocation in filter create, never in audio callback (Phase 3, D-06/D-07)
- No Win32-only APIs in core (PROJECT.md constraints)
- GPL-2.0-or-later licensing (PROJECT.md constraints)

## Deferred Ideas (Not in Phase 4)
- Resampling to 16 kHz for analysis — deferred to Phase 6 if FFT cost becomes a problem
- Multiple confidence metrics (coherence, normalized correlation) — deferred to smoother design in Phase 6
- Real-time performance benchmarking — deferred to Phase 6 soak tests
- Frequency-domain pre-filtering (bandpass) — deferred; may help with noise but not needed for ≥10 dB SNR

## Implementation Order
1. Vendor PFFFT (FetchContent)
2. Implement `gcc_phat.cpp` / `gcc_phat.h` with the full pipeline
3. Wire CMake for C++ compilation
4. Write `test_gcc_phat.cpp` with synthetic signals
5. Run tests, iterate on accuracy
