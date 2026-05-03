/*
obs-av-sync — Automatic multi-camera AV sync for OBS Studio
Copyright (C) 2026 Sean Mahoney <sean@mahoney.xyz>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <util/bmem.h>
#include <plugin-support.h>

#include <inttypes.h>

#include "av_sync_filter.h"
#include "ring_buffer.h"
#include "reference_tap.h"

#define AV_SYNC_FILTER_ID "obs_av_sync_filter"

/* Diagnostic logging throttle: log first N callbacks in detail, then roll up every INTERVAL. */
#define AV_SYNC_DIAG_DETAILED_CALLBACKS 5
#define AV_SYNC_DIAG_LOG_INTERVAL_NS 5000000000ULL /* 5 s */

/* Ring capacity in seconds — enough to cover the longest analysis window plus headroom. */
#define AV_SYNC_RING_SECONDS 10

/* Maximum audio chunk size supported for downmix (1 second = 48000 samples at 48 kHz).
   Heap-allocated at filter create. Chunks larger than this are skipped with a warning. */
#define AV_SYNC_MAX_CHUNK_S 1

struct av_sync_filter_data {
	obs_source_t *source;

	/* Pass-through diagnostics (Phase 2a verification). */
	uint64_t callback_count;
	uint64_t total_frames;
	uint64_t first_timestamp_ns;
	uint64_t prev_timestamp_ns;
	uint64_t window_start_ns;
	uint64_t window_max_gap_ns;
	uint32_t sample_rate;

	/* Per-filter configuration (Phase 5). */
	char  *reference_source_name;   /* bstrdup'd name of the chosen reference source; NULL if none */
	bool   sync_enabled;            /* true = analysis thread may apply offsets */

	/* Ring buffer (Phase 3): per-filter mono tap, allocated at filter create. */
	av_sync_ring_t *ring;
	float          *downmix_scratch;   /* heap-allocated downmix buffer, sized sample_rate * AV_SYNC_MAX_CHUNK_S */
	size_t          downmix_capacity;  /* number of floats in downmix_scratch */
	uint64_t        oversize_skips;    /* chunks skipped because they exceeded the downmix scratch */
};

static void av_sync_filter_update(void *data_ptr, obs_data_t *settings);

static const char *av_sync_filter_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("AVSync.FilterName");
}

static void *av_sync_filter_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	struct av_sync_filter_data *data = bzalloc(sizeof(*data));
	data->source = source;

	obs_source_t *parent = obs_filter_get_parent(source);
	const char *parent_name = parent ? obs_source_get_name(parent) : "(unattached)";

	struct obs_audio_info oai;
	uint32_t sample_rate = obs_get_audio_info(&oai) ? oai.samples_per_sec : 48000;
	data->sample_rate      = sample_rate;
	data->reference_source_name = NULL;
	data->sync_enabled = true;
	data->downmix_capacity = (size_t)sample_rate * AV_SYNC_MAX_CHUNK_S;
	data->downmix_scratch  = bzalloc(data->downmix_capacity * sizeof(float));
	data->ring             = av_sync_ring_create((size_t)sample_rate * AV_SYNC_RING_SECONDS, sample_rate);
	/* Note: capacity is computed first and then used in bzalloc — this is equivalent to the
	   research doc's example and is intentional. */

	obs_log(LOG_INFO, "filter created on '%s' rate=%u", parent_name, sample_rate);

	const char *saved_ref = obs_data_get_string(settings, "reference_source_name");
	if (saved_ref && saved_ref[0] != '\0') {
		obs_source_t *ref = obs_get_source_by_name(saved_ref);
		if (ref) {
			reference_tap_set_source(saved_ref);
			obs_source_release(ref);
		} else {
			obs_log(LOG_WARNING, "saved reference source '%s' not found at startup", saved_ref);
		}
	}

	/* Apply initial settings (reference source name, enable state). */
	av_sync_filter_update(data, settings);

	return data;
}

static void av_sync_filter_destroy(void *data_ptr)
{
	struct av_sync_filter_data *data = data_ptr;
	obs_source_t *parent = data->source ? obs_filter_get_parent(data->source) : NULL;
	const char *parent_name = parent ? obs_source_get_name(parent) : "(unknown)";
	obs_log(LOG_INFO, "filter destroyed on '%s'", parent_name);
	av_sync_ring_destroy(data->ring);
	bfree(data->downmix_scratch);
	bfree(data->reference_source_name);
	bfree(data);
}

static bool add_source_to_list(void *param, obs_source_t *source)
{
	obs_property_t *p = (obs_property_t *)param;
	if (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) {
		const char *name = obs_source_get_name(source);
		if (name && name[0] != '\0') {
			obs_property_list_add_string(p, name, name);
		}
	}
	return true;
}

static obs_properties_t *av_sync_filter_get_properties(void *data_ptr)
{
	UNUSED_PARAMETER(data_ptr);
	obs_properties_t *props = obs_properties_create();

	obs_property_t *list = obs_properties_add_list(
		props,
		"reference_source_name",
		obs_module_text("ReferenceSource"),
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(list, obs_module_text("ReferenceSource.None"), "");
	obs_enum_sources(add_source_to_list, list);

	obs_properties_add_bool(
		props,
		"sync_enabled",
		obs_module_text("EnableSyncTracking"));

	return props;
}

static void av_sync_filter_update(void *data_ptr, obs_data_t *settings)
{
	struct av_sync_filter_data *data = data_ptr;

	const char *new_ref = obs_data_get_string(settings, "reference_source_name");
	bool new_enabled = obs_data_get_bool(settings, "sync_enabled");

	/* Update sync_enabled (always safe, no locking needed). */
	data->sync_enabled = new_enabled;

	/* Update reference source if it changed. */
	bool changed = false;
	if (new_ref && new_ref[0] != '\0') {
		if (!data->reference_source_name ||
		    strcmp(data->reference_source_name, new_ref) != 0) {
			bfree(data->reference_source_name);
			data->reference_source_name = bstrdup(new_ref);
			changed = true;
		}
	} else {
		if (data->reference_source_name) {
			bfree(data->reference_source_name);
			data->reference_source_name = NULL;
			changed = true;
		}
	}

	if (changed) {
		reference_tap_set_source(data->reference_source_name);
		obs_log(LOG_INFO, "reference source changed to '%s' (enabled=%s)",
		        data->reference_source_name ? data->reference_source_name : "(none)",
		        data->sync_enabled ? "true" : "false");
	}
}

static struct obs_audio_data *av_sync_filter_audio(void *data_ptr, struct obs_audio_data *audio)
{
	struct av_sync_filter_data *data = data_ptr;

	if (!audio || audio->frames == 0) {
		return audio;
	}

	const uint64_t ts = audio->timestamp;
	data->callback_count++;
	data->total_frames += audio->frames;

	if (data->callback_count == 1) {
		data->first_timestamp_ns = ts;
		data->window_start_ns    = ts;
	} else {
		if (ts > data->prev_timestamp_ns) {
			const uint64_t gap = ts - data->prev_timestamp_ns;
			if (gap > data->window_max_gap_ns) {
				data->window_max_gap_ns = gap;
			}
		}
	}
	data->prev_timestamp_ns = ts;

	/* Count active planes (each plane is one float32 channel). */
	int planes = 0;
	for (int i = 0; i < MAX_AV_PLANES && audio->data[i]; i++) {
		planes++;
	}

	if (data->ring && planes > 0) {
		if (audio->frames > data->downmix_capacity) {
			data->oversize_skips++;
		} else {
			float *scratch = data->downmix_scratch;
			const float inv_planes = 1.0f / (float)planes;
			const uint32_t frames = audio->frames;
			for (uint32_t i = 0; i < frames; i++) {
				float sum = 0.0f;
				for (int p = 0; p < planes; p++) {
					sum += ((const float *)audio->data[p])[i];
				}
				scratch[i] = sum * inv_planes;
			}
			av_sync_ring_write(data->ring, scratch, frames, ts);
		}
	}

	if (data->callback_count <= AV_SYNC_DIAG_DETAILED_CALLBACKS) {
		obs_log(LOG_INFO,
			"passthrough cb #%" PRIu64 ": frames=%u ts=%" PRIu64 " rate=%u planes=%d",
			data->callback_count, audio->frames, ts, data->sample_rate, planes);
	}

	if (ts - data->window_start_ns >= AV_SYNC_DIAG_LOG_INTERVAL_NS) {
		if (data->oversize_skips > 0) {
			obs_source_t *parent_warn = data->source ? obs_filter_get_parent(data->source) : NULL;
			const char *warn_name = parent_warn ? obs_source_get_name(parent_warn) : "(unknown)";
			obs_log(LOG_WARNING,
				"oversize chunk skips=%" PRIu64 " on '%s' (downmix capacity=%zu); data lost",
				data->oversize_skips, warn_name, data->downmix_capacity);
		}
		const uint64_t elapsed_ns = ts - data->first_timestamp_ns;
		const double elapsed_s = (double)elapsed_ns / 1.0e9;
		const double eff_rate = elapsed_s > 0.0 ? (double)data->total_frames / elapsed_s : 0.0;
		av_sync_ring_stats_t rs = {0};
		av_sync_ring_get_stats(data->ring, &rs);
		const double fill_pct = rs.capacity > 0 ? (100.0 * (double)rs.filled / (double)rs.capacity) : 0.0;
		obs_log(LOG_INFO,
			"passthrough rollup cb=%" PRIu64 " t=%.1fs frames=%" PRIu64
			" eff=%.1fHz nominal=%u max_gap=%.2fms ring=%zu/%zu (%.0f%%) written=%" PRIu64
			" skips=%" PRIu64,
			data->callback_count, elapsed_s, data->total_frames, eff_rate,
			data->sample_rate, (double)data->window_max_gap_ns / 1.0e6, rs.filled,
			rs.capacity, fill_pct, rs.total_written, data->oversize_skips);
		data->oversize_skips = 0;
		data->window_start_ns = ts;
		data->window_max_gap_ns = 0;
	}

	return audio;
}

static struct obs_source_info av_sync_filter_info = {
	.id = AV_SYNC_FILTER_ID,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = av_sync_filter_get_name,
	.create = av_sync_filter_create,
	.destroy = av_sync_filter_destroy,
	.update = av_sync_filter_update,
	.get_properties = av_sync_filter_get_properties,
	.filter_audio = av_sync_filter_audio,
};

void av_sync_register_filter(void)
{
	obs_register_source(&av_sync_filter_info);
}
