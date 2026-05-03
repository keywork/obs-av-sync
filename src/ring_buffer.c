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

#include <util/bmem.h>
#include <string.h>

#include "ring_buffer.h"

/* Phase 3: SPSC-safe ring. total_written is the single atomic write cursor.
   Producer uses memory_order_release; consumer uses memory_order_acquire.
   oldest_timestamp_ns is maintained eagerly on every write — O(1) on read. */
struct av_sync_ring {
	float          *samples;
	size_t          capacity;
	uint32_t        sample_rate;

	_Atomic size_t  total_written;        /* monotonically increasing write cursor */
	uint64_t        newest_timestamp_ns;  /* timestamp of the last sample written  */
	uint64_t        oldest_timestamp_ns;  /* timestamp of the oldest valid sample  */
};

av_sync_ring_t *av_sync_ring_create(size_t capacity_samples, uint32_t sample_rate)
{
	if (capacity_samples == 0 || sample_rate == 0) {
		return NULL;
	}
	av_sync_ring_t *r = bzalloc(sizeof(*r));
	r->samples = bzalloc(capacity_samples * sizeof(float));
	r->capacity = capacity_samples;
	r->sample_rate = sample_rate;
	return r;
}

void av_sync_ring_destroy(av_sync_ring_t *r)
{
	if (!r) {
		return;
	}
	bfree(r->samples);
	bfree(r);
}

void av_sync_ring_write(av_sync_ring_t *r, const float *samples, size_t n, uint64_t timestamp_ns)
{
	if (!r || !samples || n == 0) {
		return;
	}

	/* Producer reads its own cursor — relaxed is safe here (no consumer sync needed for this load). */
	size_t tw_before = atomic_load_explicit(&r->total_written, memory_order_relaxed);
	size_t tw_after  = tw_before + n;

	if (n >= r->capacity) {
		memcpy(r->samples, samples + (n - r->capacity), r->capacity * sizeof(float));
	} else {
		size_t write_pos   = tw_before % r->capacity;
		size_t first_chunk = r->capacity - write_pos;
		if (first_chunk >= n) {
			memcpy(r->samples + write_pos, samples, n * sizeof(float));
		} else {
			memcpy(r->samples + write_pos, samples, first_chunk * sizeof(float));
			memcpy(r->samples, samples + first_chunk, (n - first_chunk) * sizeof(float));
		}
	}

	/* Update newest_timestamp_ns: timestamp of the last sample in this chunk. */
	r->newest_timestamp_ns = timestamp_ns +
		(uint64_t)(((double)(n - 1) * 1.0e9) / (double)r->sample_rate);

	/* Maintain oldest_timestamp_ns.
	   While ring is filling (tw_after <= capacity): only the very first write sets oldest.
	   Once ring is full (tw_after > capacity): every write updates oldest to track the rolling window. */
	if (tw_after <= r->capacity) {
		if (tw_before == 0) {
			r->oldest_timestamp_ns = timestamp_ns;
		}
		/* else: ring still filling — oldest sample hasn't been overwritten, leave unchanged. */
	} else {
		/* Ring is full: oldest = newest minus the span of (capacity - 1) samples. */
		uint64_t span_ns = (uint64_t)(((double)(r->capacity - 1) * 1.0e9) / (double)r->sample_rate);
		r->oldest_timestamp_ns = (r->newest_timestamp_ns > span_ns)
		                         ? r->newest_timestamp_ns - span_ns
		                         : 0;
	}

	/* Release store: all sample bytes written above are visible to any thread that
	   subsequently acquire-loads total_written. */
	atomic_store_explicit(&r->total_written, tw_after, memory_order_release);
}

void av_sync_ring_get_stats(const av_sync_ring_t *r, av_sync_ring_stats_t *out)
{
	if (!r || !out) {
		return;
	}
	/* Stats path: relaxed load is acceptable — stats are informational, not used for
	   ring-position decisions. The consumer's ring-read path uses acquire ordering. */
	size_t tw = atomic_load_explicit(&r->total_written, memory_order_relaxed);

	out->capacity            = r->capacity;
	out->total_written       = (uint64_t)tw;
	out->filled              = (tw < r->capacity) ? tw : r->capacity;
	out->sample_rate         = r->sample_rate;
	out->newest_timestamp_ns = r->newest_timestamp_ns;
	out->oldest_timestamp_ns = r->oldest_timestamp_ns;
}
