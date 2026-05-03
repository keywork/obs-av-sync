# Phase 4 Research — GCC-PHAT Offset Engine

## 1. PFFFT Integration

### Canonical Repository
- **Primary fork:** `https://github.com/hayguen/pffft` (actively maintained, CMake support)
- **Original:** `https://bitbucket.org/jpommier/pffft` (Julius O. Smith / Hayo Baan)
- **License:** BSD-like (very permissive, no GPL pollution). Compatible with GPL-2.0-or-later.
- **CMake:** The hayguen fork provides `CMakeLists.txt` with `add_library(pffft)` target.
- **Version pin:** Use commit hash or tag. Latest stable tag as of 2026 is `v1.0.1` or commit `c95035e`.

### Key API Functions
```c
PFFFT_Setup *pffft_new_setup(int N, pffft_transform_t transform);
void pffft_destroy_setup(PFFFT_Setup *);

/* Real FFT: input N floats, output N floats (special packed format) */
void pffft_transform(PFFFT_Setup *, const float *input, float *output, float *work, pffft_direction_t dir);
void pffft_transform_ordered(PFFFT_Setup *, const float *input, float *output, float *work, pffft_direction_t dir);

/* Complex FFT: input/output are 2*N floats (interleaved real/imag) */
void pffft_zreorder(PFFFT_Setup *, const float *in, float *out, pffft_direction_t dir);
```

### Real FFT Output Format
- PFFFT's real FFT does NOT produce standard interleaved complex output.
- The output is a "packed" format: for N real input samples, the output is N floats in a special order.
- To work with complex spectra (needed for cross-spectrum), use the **complex FFT mode** (`PFFFT_COMPLEX`) even for real signals: pad real input to 2*N floats (interleaved with zeros), then call `pffft_transform` with `PFFFT_COMPLEX`.
- Alternative: Use `pffft_transform_ordered` which produces a more conventional ordering, but the exact layout requires care.
- **Recommendation for Phase 4:** Use `PFFFT_COMPLEX` mode with 2*N interleaved floats (real, imag). This gives standard complex FFT output that is easy to manipulate for cross-spectrum and PHAT. Slightly more memory but much simpler code.

## 2. GCC-PHAT Pipeline

### Complete Sequence (Complex FFT approach)
Given `ref[n]` and `target[n]` (n = 0..N-1), sample rate = `fs`:

1. **Copy + zero-pad to FFT size:**
   ```
   float ref_complex[2*FFT_SIZE] = {ref[0], 0, ref[1], 0, ..., ref[N-1], 0, 0, 0, ...};
   float target_complex[2*FFT_SIZE] = {target[0], 0, target[1], 0, ..., target[N-1], 0, 0, 0, ...};
   ```

2. **Forward FFT:**
   ```
   pffft_transform(setup, ref_complex, ref_spectrum, work, PFFFT_FORWARD);
   pffft_transform(setup, target_complex, target_spectrum, work, PFFFT_FORWARD);
   ```

3. **Cross-spectrum:** For each frequency bin k:
   ```
   X_ref = ref_spectrum[2*k] + i*ref_spectrum[2*k+1];
   X_tgt = target_spectrum[2*k] + i*target_spectrum[2*k+1];
   X_cross = X_ref * conj(X_tgt);
   ```

4. **PHAT whitening:**
   ```
   mag = |X_cross| = sqrt(real^2 + imag^2);
   if (mag > epsilon) X_ph = X_cross / mag;
   else X_ph = 0;
   ```

5. **Inverse FFT:**
   ```
   pffft_transform(setup, cross_ph, gcc, work, PFFFT_BACKWARD);
   ```
   Note: PFFFT inverse does NOT normalize by 1/N. We must divide the output by FFT_SIZE.

6. **Peak pick:** Find index `peak_bin` where `gcc[k]` is maximum (magnitude).

7. **Convert to delay:**
   ```
   if (peak_bin > FFT_SIZE/2) delay_samples = peak_bin - FFT_SIZE;
   else delay_samples = peak_bin;
   offset_ns = delay_samples * 1e9 / fs;
   ```

8. **Parabolic interpolation** (for sub-sample accuracy):
   Let `y_m1 = gcc[(peak_bin - 1 + FFT_SIZE) % FFT_SIZE]`;
   Let `y_0  = gcc[peak_bin]`;
   Let `y_p1 = gcc[(peak_bin + 1) % FFT_SIZE]`;
   ```
   p = (y_m1 - y_p1) / (2 * (y_m1 - 2*y_0 + y_p1));
   delay_samples += p;  // fractional correction
   ```
   Guard: if denominator is near zero, skip interpolation.

### Why Complex FFT for Real Signals?
- PFFFT's real FFT packed format is confusing and error-prone.
- Complex FFT with zero-imag input is standard, well-documented, and the 2x memory overhead is acceptable (2 * 128k * 4 bytes = 1 MB per buffer).
- The cross-spectrum code is clean `std::complex<float>` arithmetic.

## 3. Test Signal Generation

### Box-Muller Gaussian Noise
```cpp
#include <cmath>

static float box_muller(float *u1, float *u2) {
    // Call twice per pair of uniform randoms
    float mag = sqrtf(-2.0f * logf(*u1));
    return mag * cosf(2.0f * M_PI * (*u2));
}
```
Alternative: C++11 `<random>` with `std::normal_distribution<float>` is cleaner and thread-safe.

### Delay Injection
Given signal `s[n]` of length N, delayed signal `d[n]` with delay of `D` samples:
- For positive delay (target lags reference): `d[n] = (n >= D) ? s[n - D] : 0`
- For negative delay (target leads reference): `d[n] = (n + D < N) ? s[n + D] : 0`
- Zero-pad the trailing/leading edge.

### SNR Control
1. Generate signal `s[n]` (e.g., sine wave).
2. Compute signal power: `P_s = sum(s[n]^2) / N`.
3. Generate noise `w[n]` (Gaussian, zero mean).
4. Compute noise power: `P_w = sum(w[n]^2) / N`.
5. Scale noise: `w_scaled[n] = w[n] * sqrt(P_s / P_w) * 10^(-SNR_dB/20)`.
6. Noisy signal: `x[n] = s[n] + w_scaled[n]`.

### Signal Types
- **Pure tone:** `sinf(2π * 1000 * n / fs)` — simplest, good for unit tests.
- **Band-limited noise:** Filter white noise through a lowpass at ~4 kHz — more realistic.
- **Chirp:** `sinf(2π * (f0 + (f1-f0)*n/N) * n / fs)` — wideband, good for correlation.

## 4. CMake C++ Integration

### Adding a .cpp file to a C target
```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/av_sync_filter.c src/ring_buffer.c src/gcc_phat.cpp)
```
No special `LINKER_LANGUAGE` needed. CMake automatically enables C++ compilation for `.cpp` files.

### C++ Standard
- Default C++ standard is usually C++17 on modern compilers (VS 2022+, GCC 11+, Clang 14+).
- No need to set `CXX_STANDARD` explicitly unless we need a specific version.
- PFFFT is C code; linking C++ to C is fine (the C++ compiler handles it).

### C11 Atomics unaffected
- Adding C++ files does NOT affect the C compiler settings for `.c` files.
- The existing `C_STANDARD 11` and `/experimental:c11atomics` remain in effect for C sources.

### FetchContent for PFFFT
```cmake
include(FetchContent)
FetchContent_Declare(
  pffft
  GIT_REPOSITORY https://github.com/hayguen/pffft.git
  GIT_TAG        c95035e  # or v1.0.1 tag
)
FetchContent_MakeAvailable(pffft)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE pffft)
```

## 5. PFFFT Alignment and Memory

### Alignment Requirements
- PFFFT requires **16-byte aligned** input/output/work buffers for SSE.
- For AVX-256, **32-byte alignment** is needed.
- `pffft_aligned_malloc` and `pffft_aligned_free` are provided by PFFFT for this purpose.
- **Recommendation:** Use `pffft_aligned_malloc` / `pffft_aligned_free` for all PFFFT buffers. Do NOT use `bzalloc`/`bfree` for PFFFT buffers — OBS's allocator may not provide the required alignment.

### Buffer Sizes
- Complex FFT input/output: `2 * FFT_SIZE * sizeof(float)` (interleaved real/imag).
- Work buffer: `2 * FFT_SIZE * sizeof(float)` (PFFFT needs a work area of the same size).

## 6. Confidence Metric

### Peak-to-Sidelobe Ratio (PSR)
```
peak = max(|gcc[k]|)
peak_idx = argmax(|gcc[k]|)
excluded = {peak_idx-2, peak_idx-1, peak_idx, peak_idx+1, peak_idx+2} (modulo FFT_SIZE)
sidelobe_power = mean(|gcc[k]|^2 for k not in excluded)
PSR = peak / sqrt(sidelobe_power)
```
- **Why exclude ±2:** The peak lobe of the sinc function spans ~3 bins; excluding ±2 avoids biasing the sidelobe estimate.
- **Expected values:**
  - 40 dB SNR: PSR ~50–100
  - 20 dB SNR: PSR ~10–30
  - 10 dB SNR: PSR ~3–8
  - <5 dB SNR: PSR ~1–2 (unreliable)

### Normalized Peak (secondary)
```
norm_peak = peak / sqrt(mean(|gcc[k]|^2 for all k))
```
- This is similar to PSR but includes the peak lobe in the denominator. Always ≤ PSR.

## Validation Map

| Research Section | Success Criterion | Validation |
|---|---|---|
| §1 PFFFT API | PFFFT builds on all 3 platforms | CI build matrix |
| §2 Pipeline | Offset within 1 ms at ≥10 dB SNR | Unit tests |
| §2 Pipeline | Correct handling of ±500 ms | Unit tests with ±24000 sample delays |
| §3 Test signals | Deterministic, reproducible tests | Fixed random seed |
| §4 CMake | Mixed C/C++ compiles cleanly | Local + CI build |
| §5 Alignment | No segfaults or misalignment | Valgrind / ASan on Linux |
| §6 Confidence | Score decreases with SNR | Unit test assertions |

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| PFFFT complex FFT slower than real FFT | Low | Medium | Benchmark in Phase 6; fallback to real FFT if needed |
| Parabolic interpolation unstable at low SNR | Medium | Low | Guard against near-zero denominator; fallback to integer peak |
| PFFFT FetchContent fails offline | Low | High | Document `FETCHCONTENT_SOURCE_DIR_PFFFT` override |
| C++ compilation breaks C11 atomics on MSVC | Low | High | CI build matrix catches it; separate C and CXX flags |
| GCC-PHAT accuracy < 1 ms at 10 dB SNR | Low | Critical | Increase FFT size or window; use chirp test signals |
