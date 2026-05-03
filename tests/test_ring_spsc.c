/*
 * tests/test_ring_spsc.c — SPSC round-trip test for av_sync_ring.
 *
 * Spawns a writer thread and reads concurrently from the main thread.
 * Under ThreadSanitizer, any data race on ring->samples[] will be reported.
 *
 * obs-av-sync — Copyright (C) 2026 Sean Mahoney <sean@mahoney.xyz>
 * GPL-2.0-or-later
 */

#include "obs_shim.h"
#include "../src/ring_buffer.h"

#include <assert.h>
#include <stdio.h>
#include <inttypes.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <process.h>
typedef HANDLE thread_t;
static thread_t thread_create(unsigned(__stdcall *fn)(void *), void *arg)
{
	return (HANDLE)_beginthreadex(NULL, 0, fn, arg, 0, NULL);
}
static void thread_join(thread_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
#  define THREAD_FUNC_RETURN unsigned __stdcall
#else
#  include <pthread.h>
#  include <sched.h>
typedef pthread_t thread_t;
static thread_t thread_create(void *(*fn)(void *), void *arg)
{
	pthread_t t;
	pthread_create(&t, NULL, fn, arg);
	return t;
}
static void thread_join(thread_t t) { pthread_join(t, NULL); }
#  define THREAD_FUNC_RETURN void *
#endif

#define CAPACITY    4800
#define SAMPLE_RATE 48000
#define CHUNKS      1000
#define CHUNK_SIZE  480

static av_sync_ring_t *g_ring;

static THREAD_FUNC_RETURN writer_thread(void *arg)
{
	(void)arg;
	float buf[CHUNK_SIZE];
	for (int i = 0; i < CHUNKS; i++) {
		for (int j = 0; j < CHUNK_SIZE; j++) {
			buf[j] = (float)(i * CHUNK_SIZE + j);
		}
		av_sync_ring_write(g_ring, buf, CHUNK_SIZE, (uint64_t)i * 10000000ULL);
	}
#if defined(_WIN32)
	return 0;
#else
	return NULL;
#endif
}

int main(void)
{
	g_ring = av_sync_ring_create(CAPACITY, SAMPLE_RATE);
	assert(g_ring != NULL);

	av_sync_ring_cursor_t cursor;
	av_sync_ring_cursor_init(g_ring, &cursor);

	thread_t writer = thread_create(writer_thread, NULL);

	float out[CAPACITY];
	size_t total_read = 0;
	const size_t expected = (size_t)CHUNKS * CHUNK_SIZE;
	size_t verify_pos = cursor.pos;

	while (total_read < expected) {
		size_t got = av_sync_ring_read(g_ring, &cursor, out, 960);
		for (size_t i = 0; i < got; i++) {
			assert(out[i] == (float)(verify_pos + i));
		}
		verify_pos += got;
		total_read += got;
		if (got == 0) {
#ifdef _WIN32
			Sleep(0);
#else
			sched_yield();
#endif
		}
	}

	thread_join(writer);

	av_sync_ring_stats_t stats;
	av_sync_ring_get_stats(g_ring, &stats);
	assert(cursor.pos == expected);
	assert(stats.total_written == (uint64_t)expected);

	av_sync_ring_destroy(g_ring);

	printf("PASS: spsc_round_trip (cursor.pos=%zu, %zu samples read, %" PRIu64 " written)\n",
	       cursor.pos, total_read, stats.total_written);
	return 0;
}
