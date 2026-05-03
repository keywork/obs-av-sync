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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct av_sync_ring av_sync_ring_t;

/* One-time init / shutdown — call from obs_module_load / obs_module_unload. */
bool reference_tap_init(void);
void reference_tap_shutdown(void);

/* Change the designated reference source. Pass NULL or "" to detach.
   Thread-safe; may be called from the UI thread. */
void reference_tap_set_source(const char *name);

/* Return the shared reference ring, or NULL if no source is attached.
   The caller (analysis thread) must NOT free the ring. */
av_sync_ring_t *reference_tap_get_ring(void);

#ifdef __cplusplus
}
#endif
