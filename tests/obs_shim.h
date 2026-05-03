/*
 * tests/obs_shim.h — Minimal OBS API stubs for standalone unit tests.
 * Include this before any OBS or plugin headers.
 *
 * obs-av-sync — Copyright (C) 2026 Sean Mahoney <sean@mahoney.xyz>
 * GPL-2.0-or-later
 */
#pragma once

#include <stdlib.h>
#include <string.h>

/* Map OBS heap allocators to standard C equivalents. */
#define bzalloc(n)  calloc(1, (n))
#define bfree(p)    free(p)

/* Stub obs_log — ring_buffer.c currently does not call obs_log; this stub prevents link errors if future changes add logging. */
#define obs_log(level, ...) ((void)0)
