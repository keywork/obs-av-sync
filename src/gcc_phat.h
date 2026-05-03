/**
 * @file gcc_phat.h
 * @brief C-compatible interface for GCC-PHAT delay estimation.
 *
 * obs-av-sync — Automatic multi-camera AV sync for OBS Studio
 * Copyright (C) 2026 Sean Mahoney <sean@mahoney.xyz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef GCC_PHAT_H
#define GCC_PHAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result of a GCC-PHAT offset estimation. */
typedef struct {
	/** Estimated offset in nanoseconds. Positive means target lags reference. */
	float offset_ns;
	/** Confidence metric (peak-to-sidelobe ratio, higher is better). */
	float confidence;
} gcc_phat_result_t;

/**
 * @brief Estimate the time offset between two audio signals using GCC-PHAT.
 *
 * @param ref         Reference signal buffer (mono, float).
 * @param target      Target signal buffer (mono, float).
 * @param n_samples   Number of samples in both buffers.
 * @param sample_rate Sample rate in Hz.
 * @return gcc_phat_result_t containing offset_ns and confidence.
 */
gcc_phat_result_t estimate_offset(const float *ref, const float *target,
                                   size_t n_samples, uint32_t sample_rate);

#ifdef __cplusplus
}
#endif

#endif /* GCC_PHAT_H */
