#ifndef _META_SYNC_RING_H_
#define _META_SYNC_RING_H_

#include <glib.h>

#include <X11/Xlib.h>
#include <X11/extensions/sync.h>

#include <meta/display.h>

gboolean meta_sync_ring_init (MetaDisplay *dpy);
void meta_sync_ring_destroy (void);
void meta_sync_ring_after_frame (void);
void meta_sync_ring_insert_wait (void);
void meta_sync_ring_handle_event (XSyncAlarmNotifyEvent *event);

#endif  /* _META_SYNC_RING_H_ */
