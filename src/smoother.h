/*
obs-av-sync — Automatic multi-camera AV sync for OBS Studio
Copyright (C) 2026 Sean Mahoney <sean@mahoney.xyz>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct av_sync_smoother av_sync_smoother_t;

struct av_sync_smoother {
	float alpha;              /* default 0.3f */
	float smoothed_offset_ms; /* current smoothed value */
	float slew_rate_cap_ms;   /* default 20.0f */
	float confidence_threshold; /* default 2.0f */
	uint32_t valid_count;     /* number of accepted measurements */
	bool has_valid_offset;    /* true once at least one measurement accepted */
	float last_raw_confidence; /* last confidence seen, even on rejection */
};

/* Zero the struct and set default constants. */
void smoother_init(av_sync_smoother_t *s);

/* Process one raw measurement. Returns true if accepted, false if rejected. */
bool smoother_process(av_sync_smoother_t *s, float raw_offset_ms, float confidence);

/* Return the current smoothed offset in milliseconds. */
float smoother_get_offset_ms(const av_sync_smoother_t *s);

/* Return status: 0 = Measuring, 1 = Synced, 2 = Out of Range. */
int smoother_get_status(const av_sync_smoother_t *s);

#ifdef __cplusplus
}
#endif
