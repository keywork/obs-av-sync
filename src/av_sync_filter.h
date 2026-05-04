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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct av_sync_filter_data;

typedef void (*av_sync_instance_cb)(struct av_sync_filter_data *inst, void *userdata);

void av_sync_filter_enum_instances(av_sync_instance_cb cb, void *userdata);

const char *av_sync_filter_get_parent_name(struct av_sync_filter_data *data);
int         av_sync_filter_get_status(struct av_sync_filter_data *data);
bool        av_sync_filter_get_sync_enabled(struct av_sync_filter_data *data);
float       av_sync_filter_get_smoothed_offset_ms(struct av_sync_filter_data *data);
float       av_sync_filter_get_last_confidence(struct av_sync_filter_data *data);
const char *av_sync_filter_get_reference_name(struct av_sync_filter_data *data);

void av_sync_register_filter(void);

#ifdef __cplusplus
}
#endif
