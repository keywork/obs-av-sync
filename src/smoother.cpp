/*
obs-av-sync — Automatic multi-camera AV sync for OBS Studio
Copyright (C) 2026 Sean Mahoney <sean@mahoney.xyz>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "smoother.h"
#include <math.h>

void smoother_init(av_sync_smoother_t *s)
{
	if (!s) {
		return;
	}
	s->alpha = 0.3f;
	s->smoothed_offset_ms = 0.0f;
	s->slew_rate_cap_ms = 20.0f;
	s->confidence_threshold = 2.0f;
	s->valid_count = 0;
	s->has_valid_offset = false;
	s->last_raw_confidence = 0.0f;
}

bool smoother_process(av_sync_smoother_t *s, float raw_offset_ms, float confidence)
{
	if (!s) {
		return false;
	}
	if (!isfinite(raw_offset_ms) || !isfinite(confidence)) {
		return false;
	}
	s->last_raw_confidence = confidence;
	if (confidence < s->confidence_threshold) {
		return false;
	}

	float desired = s->alpha * raw_offset_ms +
	                (1.0f - s->alpha) * s->smoothed_offset_ms;
	float delta = desired - s->smoothed_offset_ms;

	if (delta > s->slew_rate_cap_ms) {
		delta = s->slew_rate_cap_ms;
	} else if (delta < -s->slew_rate_cap_ms) {
		delta = -s->slew_rate_cap_ms;
	}

	s->smoothed_offset_ms += delta;
	s->valid_count++;
	s->has_valid_offset = true;
	return true;
}

float smoother_get_offset_ms(const av_sync_smoother_t *s)
{
	return s ? s->smoothed_offset_ms : 0.0f;
}

int smoother_get_status(const av_sync_smoother_t *s)
{
	if (!s || !s->has_valid_offset) {
		return 0;
	}
	if (s->valid_count < 3) {
		return 0;
	}
	if (!isfinite(s->smoothed_offset_ms) || !isfinite(s->last_raw_confidence) ||
	    s->last_raw_confidence < s->confidence_threshold || fabsf(s->smoothed_offset_ms) > 500.0f) {
		return 2;
	}
	return 1;
}
