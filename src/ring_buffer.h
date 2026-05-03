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

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct av_sync_ring av_sync_ring_t;

av_sync_ring_t *av_sync_ring_create(size_t capacity_samples, uint32_t sample_rate);
void av_sync_ring_destroy(av_sync_ring_t *ring);

/* Append n mono float samples tagged with the OBS timestamp (ns) of the FIRST sample.
   Single-producer only. Oldest samples are overwritten when the ring is full. */
void av_sync_ring_write(av_sync_ring_t *ring, const float *samples, size_t n, uint64_t timestamp_ns);

typedef struct {
	size_t capacity;
	size_t filled;
	uint64_t total_written;
	uint64_t oldest_timestamp_ns;
	uint64_t newest_timestamp_ns;
	uint32_t sample_rate;
} av_sync_ring_stats_t;

void av_sync_ring_get_stats(const av_sync_ring_t *ring, av_sync_ring_stats_t *out);

#ifdef __cplusplus
}
#endif
