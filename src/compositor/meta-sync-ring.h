#pragma once

#include <glib.h>
#include <X11/Xlib.h>

gboolean meta_sync_ring_init (Display *dpy);
void meta_sync_ring_destroy (void);
gboolean meta_sync_ring_after_frame (void);
gboolean meta_sync_ring_insert_wait (void);
void meta_sync_ring_handle_event (XEvent *event);
