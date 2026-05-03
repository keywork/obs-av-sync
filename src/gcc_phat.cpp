/**
 * @file gcc_phat.cpp
 * @brief GCC-PHAT (Generalized Cross-Correlation with Phase Transform)
 *        delay estimation implementation.
 *
 * obs-av-sync — Automatic multi-camera AV sync for OBS Studio
 * Copyright (C) 2026 Sean Mahoney <sean@mahoney.xyz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>
#include <complex>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "pffft.h"
#include "gcc_phat.h"

gcc_phat_result_t estimate_offset(const float *ref, const float *target,
                                   size_t n_samples, uint32_t sample_rate)
{
	gcc_phat_result_t result = {0.0f, 0.0f};

	if (!ref || !target || n_samples == 0 || sample_rate == 0) {
		return result;
	}

	const int fft_size = pffft_next_power_of_two(static_cast<int>(n_samples));
	const size_t complex_count = static_cast<size_t>(fft_size);
	const size_t buf_bytes = 2 * complex_count * sizeof(float);

	float *ref_complex = static_cast<float *>(pffft_aligned_malloc(buf_bytes));
	float *tgt_complex = static_cast<float *>(pffft_aligned_malloc(buf_bytes));
	float *ref_spec = static_cast<float *>(pffft_aligned_malloc(buf_bytes));
	float *tgt_spec = static_cast<float *>(pffft_aligned_malloc(buf_bytes));
	float *cross_ph = static_cast<float *>(pffft_aligned_malloc(buf_bytes));
	float *gcc = static_cast<float *>(pffft_aligned_malloc(buf_bytes));
	float *work = static_cast<float *>(pffft_aligned_malloc(buf_bytes));

	if (!ref_complex || !tgt_complex || !ref_spec || !tgt_spec || !cross_ph ||
	    !gcc || !work) {
		pffft_aligned_free(ref_complex);
		pffft_aligned_free(tgt_complex);
		pffft_aligned_free(ref_spec);
		pffft_aligned_free(tgt_spec);
		pffft_aligned_free(cross_ph);
		pffft_aligned_free(gcc);
		pffft_aligned_free(work);
		return result;
	}

	PFFFT_Setup *setup = pffft_new_setup(fft_size, PFFFT_COMPLEX);
	if (!setup) {
		pffft_aligned_free(ref_complex);
		pffft_aligned_free(tgt_complex);
		pffft_aligned_free(ref_spec);
		pffft_aligned_free(tgt_spec);
		pffft_aligned_free(cross_ph);
		pffft_aligned_free(gcc);
		pffft_aligned_free(work);
		return result;
	}

	for (size_t i = 0; i < n_samples; ++i) {
		ref_complex[2 * i] = ref[i];
		ref_complex[2 * i + 1] = 0.0f;
		tgt_complex[2 * i] = target[i];
		tgt_complex[2 * i + 1] = 0.0f;
	}
	for (size_t i = n_samples; i < complex_count; ++i) {
		ref_complex[2 * i] = 0.0f;
		ref_complex[2 * i + 1] = 0.0f;
		tgt_complex[2 * i] = 0.0f;
		tgt_complex[2 * i + 1] = 0.0f;
	}

	pffft_transform(setup, ref_complex, ref_spec, work, PFFFT_FORWARD);
	pffft_transform(setup, tgt_complex, tgt_spec, work, PFFFT_FORWARD);

	for (int k = 0; k < fft_size; ++k) {
		const std::complex<float> x_ref(ref_spec[2 * k],
		                                 ref_spec[2 * k + 1]);
		const std::complex<float> x_tgt(tgt_spec[2 * k],
		                                 tgt_spec[2 * k + 1]);
		const std::complex<float> x_cross = x_ref * std::conj(x_tgt);

		const float mag = std::abs(x_cross);
		std::complex<float> x_ph(0.0f, 0.0f);
		if (mag > 1e-12f) {
			x_ph = x_cross / mag;
		}

		cross_ph[2 * k] = x_ph.real();
		cross_ph[2 * k + 1] = x_ph.imag();
	}

	pffft_transform(setup, cross_ph, gcc, work, PFFFT_BACKWARD);

	const float norm = 1.0f / static_cast<float>(fft_size);
	for (size_t i = 0; i < 2 * complex_count; ++i) {
		gcc[i] *= norm;
	}

	float max_mag = 0.0f;
	int peak_bin = 0;
	for (int k = 0; k < fft_size; ++k) {
		const float mag = std::hypot(gcc[2 * k], gcc[2 * k + 1]);
		if (mag > max_mag) {
			max_mag = mag;
			peak_bin = k;
		}
	}

	const int prev_bin = (peak_bin - 1 + fft_size) % fft_size;
	const int next_bin = (peak_bin + 1) % fft_size;
	const float ym1 = gcc[2 * prev_bin];
	const float y0 = gcc[2 * peak_bin];
	const float yp1 = gcc[2 * next_bin];
	const float denom = 2.0f * (ym1 - 2.0f * y0 + yp1);
	float p = 0.0f;
	if (std::fabs(denom) > 1e-12f) {
		p = (ym1 - yp1) / denom;
	}

	float delay_samples = (peak_bin > fft_size / 2)
		                      ? static_cast<float>(peak_bin - fft_size)
		                      : static_cast<float>(peak_bin);
	delay_samples += p;
	result.offset_ns = delay_samples * 1e9f / static_cast<float>(sample_rate);

	const float peak = std::hypot(gcc[2 * peak_bin], gcc[2 * peak_bin + 1]);

	float sidelobe_sum_sq = 0.0f;
	int sidelobe_count = 0;
	for (int k = 0; k < fft_size; ++k) {
		int dist = k - peak_bin;
		if (dist < 0)
			dist = -dist;
		int wrap_dist = dist;
		if (fft_size - dist < wrap_dist)
			wrap_dist = fft_size - dist;

		if (wrap_dist > 2) {
			const float mag = std::hypot(gcc[2 * k], gcc[2 * k + 1]);
			sidelobe_sum_sq += mag * mag;
			++sidelobe_count;
		}
	}

	if (sidelobe_count > 0) {
		const float sidelobe_rms = std::sqrt(
			sidelobe_sum_sq / static_cast<float>(sidelobe_count));
		if (sidelobe_rms > 1e-12f) {
			result.confidence = peak / sidelobe_rms;
		}
	}

	pffft_destroy_setup(setup);
	pffft_aligned_free(ref_complex);
	pffft_aligned_free(tgt_complex);
	pffft_aligned_free(ref_spec);
	pffft_aligned_free(tgt_spec);
	pffft_aligned_free(cross_ph);
	pffft_aligned_free(gcc);
	pffft_aligned_free(work);

	return result;
}
