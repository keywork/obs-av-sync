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

typedef struct av_sync_ring av_sync_ring_t;

/* One-time init / shutdown — call from obs_module_load / obs_module_unload. */
bool reference_tap_init(void);
void reference_tap_shutdown(void);

/* Change the designated reference source. Pass NULL or "" to detach.
   Thread-safe; may be called from the UI thread.
   @param requester Optional name of the filter/source requesting the change,
          logged when the global reference is overridden. */
void reference_tap_set_source(const char *name, const char *requester);

/* Return the shared reference ring, or NULL if no source is attached.
   The pointer is valid for the lifetime of the plugin module. Callers
   must NOT hold it across reference_tap_shutdown() and must NOT free it. */
const av_sync_ring_t *reference_tap_get_ring(void);

/* Return the number of oversized audio chunks dropped by the reference
   callback since init. */
uint64_t reference_tap_get_oversize_skips(void);

#ifdef __cplusplus
}
#endif
