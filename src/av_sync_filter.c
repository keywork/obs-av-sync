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

#include "av_sync_filter.h"

#define AV_SYNC_FILTER_ID "obs_av_sync_filter"

struct av_sync_filter_data {
	obs_source_t *source;
};

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
	obs_log(LOG_INFO, "filter created on '%s'", parent_name);

	return data;
}

static void av_sync_filter_destroy(void *data_ptr)
{
	struct av_sync_filter_data *data = data_ptr;
	obs_source_t *parent = data->source ? obs_filter_get_parent(data->source) : NULL;
	const char *parent_name = parent ? obs_source_get_name(parent) : "(unknown)";
	obs_log(LOG_INFO, "filter destroyed on '%s'", parent_name);
	bfree(data);
}

static struct obs_audio_data *av_sync_filter_audio(void *data_ptr, struct obs_audio_data *audio)
{
	UNUSED_PARAMETER(data_ptr);
	/* Phase 2a: pass-through. Phase 2b will tap samples into a ring buffer here. */
	return audio;
}

static struct obs_source_info av_sync_filter_info = {
	.id = AV_SYNC_FILTER_ID,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = av_sync_filter_get_name,
	.create = av_sync_filter_create,
	.destroy = av_sync_filter_destroy,
	.filter_audio = av_sync_filter_audio,
};

void av_sync_register_filter(void)
{
	obs_register_source(&av_sync_filter_info);
}
