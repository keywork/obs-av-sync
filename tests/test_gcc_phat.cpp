/**
 * @file test_gcc_phat.cpp
 * @brief Synthetic signal unit tests for GCC-PHAT delay estimation.
 *
 * obs-av-sync — Automatic multi-camera AV sync for OBS Studio
 * Copyright (C) 2026 Sean Mahoney <sean@mahoney.xyz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

#include "gcc_phat.h"

static void generate_noise(float *buf, size_t n)
{
	/* Band-limited pink noise for robust GCC-PHAT peaks.
	   Pink noise has energy at all frequencies, giving a clean
	   delta-like correlation peak. */
	std::mt19937 gen(123u);
	std::normal_distribution<float> dist(0.0f, 1.0f);
	for (size_t i = 0; i < n; ++i)
		buf[i] = dist(gen);
}

static void add_gaussian_noise(float *buf, size_t n, float snr_db)
{
	double sig_pwr = 0.0;
	for (size_t i = 0; i < n; ++i)
		sig_pwr += static_cast<double>(buf[i]) * buf[i];
	sig_pwr /= static_cast<double>(n);

	std::mt19937 gen(42u);
	std::normal_distribution<float> dist(0.0f, 1.0f);
	std::vector<float> noise(n);
	double noise_pwr = 0.0;
	for (size_t i = 0; i < n; ++i) {
		noise[i] = dist(gen);
		noise_pwr += static_cast<double>(noise[i]) * noise[i];
	}
	noise_pwr /= static_cast<double>(n);

	double scale = std::sqrt(sig_pwr / noise_pwr) *
	               std::pow(10.0, -snr_db / 20.0);
	for (size_t i = 0; i < n; ++i)
		buf[i] += static_cast<float>(noise[i] * scale);
}

static void apply_delay(const float *in, float *out, size_t n,
                        int delay_samples)
{
	for (size_t i = 0; i < n; ++i)
		out[i] = 0.0f;

	if (delay_samples >= 0) {
		for (size_t i = delay_samples; i < n; ++i)
			out[i] = in[i - delay_samples];
	} else {
		int lead = -delay_samples;
		for (size_t i = 0; i + lead < n; ++i)
			out[i] = in[i + lead];
	}
}

int main(void)
{
	const int delays[] = {-24000, -12000, -4800, -480, 0,
	                      480,    4800,   12000, 24000};
	const float snrs[] = {10.0f, 20.0f, 30.0f, 40.0f};
	const size_t delay_count = sizeof(delays) / sizeof(delays[0]);
	const size_t snr_count = sizeof(snrs) / sizeof(snrs[0]);

	double confidence_sums[4] = {0.0, 0.0, 0.0, 0.0};
	size_t confidence_counts[4] = {0, 0, 0, 0};

	int total_pass = 0;
	int total_fail = 0;

	for (size_t s = 0; s < snr_count; ++s) {
		for (size_t d = 0; d < delay_count; ++d) {
			int delay_samples = delays[d];
			float snr_db = snrs[s];

			std::vector<float> ref(96000);
			std::vector<float> tgt(96000);
			std::vector<float> delayed(96000);

			generate_noise(ref.data(), ref.size());
			std::memcpy(tgt.data(), ref.data(),
			            tgt.size() * sizeof(float));
			add_gaussian_noise(tgt.data(), tgt.size(), snr_db);
			apply_delay(tgt.data(), delayed.data(), tgt.size(),
			            delay_samples);

			gcc_phat_result_t result =
				estimate_offset(ref.data(), delayed.data(), ref.size(),
				               48000);

			float expected_ns =
				delay_samples * 1e9f / 48000.0f;

			bool offset_ok =
				std::fabs(result.offset_ns - expected_ns) <
				1000000.0f; // < 1 ms
			bool confidence_ok = result.confidence > 1.0f;

			confidence_sums[s] +=
				static_cast<double>(result.confidence);
			++confidence_counts[s];

			if (offset_ok && confidence_ok) {
				++total_pass;
				std::printf(
					"PASS  delay=%+6d  snr=%.0f dB  "
					"offset=%.3f ms  conf=%.2f\n",
					delay_samples, snr_db,
					result.offset_ns / 1e6f,
					result.confidence);
			} else {
				++total_fail;
				std::printf(
					"FAIL  delay=%+6d  snr=%.0f dB  "
					"offset=%.3f ms (expected %.3f ms)  "
					"conf=%.2f (expected >1.0)\n",
					delay_samples, snr_db,
					result.offset_ns / 1e6f,
					expected_ns / 1e6f,
					result.confidence);
			}

			assert(offset_ok);
			assert(confidence_ok);
		}
	}

	double mean_confidence_10db =
		confidence_sums[0] / static_cast<double>(confidence_counts[0]);
	double mean_confidence_40db =
		confidence_sums[3] / static_cast<double>(confidence_counts[3]);

	std::printf("\n");
	std::printf("Mean confidence @ 10 dB: %.2f\n", mean_confidence_10db);
	std::printf("Mean confidence @ 40 dB: %.2f\n", mean_confidence_40db);
	assert(mean_confidence_10db < mean_confidence_40db);

	std::printf("\n");
	std::printf("Summary: %d passed, %d failed out of %zu\n", total_pass,
	            total_fail, delay_count * snr_count);

	return total_fail > 0 ? 1 : 0;
}
