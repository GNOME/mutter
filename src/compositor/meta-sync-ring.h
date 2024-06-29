#pragma once

#include <glib.h>
#include <X11/Xlib.h>

#include "cogl/cogl.h"

gboolean meta_sync_ring_init (CoglContext *ctx,
                              Display     *dpy);
void meta_sync_ring_destroy (void);
gboolean meta_sync_ring_after_frame (CoglContext *ctx);
gboolean meta_sync_ring_insert_wait (CoglContext *ctx);
void meta_sync_ring_handle_event (XEvent *event);
