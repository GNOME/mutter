/*
 * Copyright 2014 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef META_CURSOR_TRACKER_X11_H
#define META_CURSOR_TRACKER_X11_H

#include <glib-object.h>
#include <meta/meta-cursor-tracker.h>

#define META_TYPE_CURSOR_TRACKER_X11            (meta_cursor_tracker_x11_get_type ())
#define META_CURSOR_TRACKER_X11(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_CURSOR_TRACKER_X11, MetaCursorTrackerX11))
#define META_CURSOR_TRACKER_X11_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_CURSOR_TRACKER_X11, MetaCursorTrackerX11Class))
#define META_IS_CURSOR_TRACKER_X11(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_CURSOR_TRACKER_X11))
#define META_IS_CURSOR_TRACKER_X11_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_CURSOR_TRACKER_X11))
#define META_CURSOR_TRACKER_X11_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_CURSOR_TRACKER_X11, MetaCursorTrackerX11Class))

typedef struct _MetaCursorTrackerX11        MetaCursorTrackerX11;
typedef struct _MetaCursorTrackerX11Class   MetaCursorTrackerX11Class;

GType meta_cursor_tracker_x11_get_type (void);

gboolean
meta_cursor_tracker_x11_handle_xevent (MetaCursorTrackerX11 *tracker,
                                       XEvent               *xevent);

#endif /* META_CURSOR_TRACKER_X11_H */
