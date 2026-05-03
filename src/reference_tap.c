/*
obs-av-sync — Automatic multi-camera AV sync for OBS Studio
Copyright (C) 2026 Sean Mahoney <sean@mahoney.xyz>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <plugin-support.h>
#include <string.h>
#include <pthread.h>

#include "ring_buffer.h"
#include "reference_tap.h"

static pthread_mutex_t ref_mutex;
static char            ref_name[256];
static obs_source_t   *ref_source;
static av_sync_ring_t *ref_ring;
static uint32_t        ref_sample_rate;
static float          *ref_downmix_scratch;
static size_t          ref_downmix_capacity;

static void reference_audio_callback(void *param, obs_source_t *source,
                                      const struct audio_data *audio, bool muted)
{
	UNUSED_PARAMETER(param);
	UNUSED_PARAMETER(source);

	if (muted || !audio || audio->frames == 0) {
		return;
	}

	if (audio->frames > ref_downmix_capacity) {
		return;
	}

	int planes = 0;
	for (int i = 0; i < MAX_AV_PLANES && audio->data[i]; i++) {
		planes++;
	}

	if (planes == 0) {
		return;
	}

	const float inv_planes = 1.0f / (float)planes;
	for (uint32_t i = 0; i < audio->frames; i++) {
		float sum = 0.0f;
		for (int p = 0; p < planes; p++) {
			sum += ((const float *)audio->data[p])[i];
		}
		ref_downmix_scratch[i] = sum * inv_planes;
	}

	av_sync_ring_write(ref_ring, ref_downmix_scratch, audio->frames, audio->timestamp);
}

bool reference_tap_init(void)
{
	pthread_mutex_init(&ref_mutex, NULL);

	struct obs_audio_info oai;
	ref_sample_rate = obs_get_audio_info(&oai) ? oai.samples_per_sec : 48000;

	ref_ring = av_sync_ring_create((size_t)ref_sample_rate * 10, ref_sample_rate);
	ref_downmix_capacity = (size_t)ref_sample_rate;
	ref_downmix_scratch  = bzalloc(ref_downmix_capacity * sizeof(float));

	ref_name[0] = '\0';
	ref_source  = NULL;

	return true;
}

void reference_tap_shutdown(void)
{
	pthread_mutex_lock(&ref_mutex);

	if (ref_source) {
		obs_source_remove_audio_capture_callback(ref_source, reference_audio_callback, NULL);
		obs_source_release(ref_source);
		ref_source = NULL;
	}

	av_sync_ring_destroy(ref_ring);
	ref_ring = NULL;

	bfree(ref_downmix_scratch);
	ref_downmix_scratch = NULL;

	pthread_mutex_unlock(&ref_mutex);
	pthread_mutex_destroy(&ref_mutex);
}

void reference_tap_set_source(const char *name)
{
	pthread_mutex_lock(&ref_mutex);

	if (!name || name[0] == '\0') {
		if (ref_source) {
			obs_source_remove_audio_capture_callback(ref_source, reference_audio_callback, NULL);
			obs_source_release(ref_source);
			ref_source = NULL;
		}
		ref_name[0] = '\0';
	} else if (ref_source && strcmp(ref_name, name) == 0) {
		/* Same source — no-op. */
	} else {
		/* Detach existing source if any. */
		if (ref_source) {
			obs_source_remove_audio_capture_callback(ref_source, reference_audio_callback, NULL);
			obs_source_release(ref_source);
			ref_source = NULL;
		}

		strncpy(ref_name, name, sizeof(ref_name) - 1);
		ref_name[sizeof(ref_name) - 1] = '\0';

		obs_source_t *new_source = obs_get_source_by_name(name);
		if (!new_source) {
			obs_log(LOG_WARNING, "reference source '%s' not found", name);
		} else if (!(obs_source_get_output_flags(new_source) & OBS_SOURCE_AUDIO)) {
			obs_log(LOG_WARNING, "reference source '%s' has no audio output", name);
			obs_source_release(new_source);
		} else {
			obs_source_add_audio_capture_callback(new_source, reference_audio_callback, NULL);
			ref_source = new_source;
		}
	}

	pthread_mutex_unlock(&ref_mutex);
}

av_sync_ring_t *reference_tap_get_ring(void)
{
	pthread_mutex_lock(&ref_mutex);
	av_sync_ring_t *ring = ref_ring;
	pthread_mutex_unlock(&ref_mutex);
	return ring;
}
