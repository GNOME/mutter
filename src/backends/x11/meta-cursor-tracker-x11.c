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

#include "config.h"

#include <gdk/gdkx.h>

#include <meta/errors.h>

#include "display-private.h"
#include "meta-cursor-tracker-x11.h"
#include "meta-cursor-tracker-private.h"
#include "meta-cursor-private.h"

struct _MetaCursorTrackerX11
{
  MetaCursorTracker parent;
};

struct _MetaCursorTrackerX11Class
{
  MetaCursorTrackerClass parent_class;
};

G_DEFINE_TYPE (MetaCursorTrackerX11, meta_cursor_tracker_x11, META_TYPE_CURSOR_TRACKER);

static void
meta_cursor_tracker_x11_get_pointer (MetaCursorTracker   *tracker,
                                     int                 *x,
                                     int                 *y,
                                     ClutterModifierType *mods)
{
  GdkDeviceManager *gmanager;
  GdkDevice *gdevice;
  GdkScreen *gscreen;

  /* We can't use the clutter interface when not running as a wayland
   * compositor, because we need to query the server, rather than
   * using the last cached value.
   */
  gmanager = gdk_display_get_device_manager (gdk_display_get_default ());
  gdevice = gdk_x11_device_manager_lookup (gmanager, META_VIRTUAL_CORE_POINTER_ID);

  gdk_device_get_position (gdevice, &gscreen, x, y);
  if (mods)
    gdk_device_get_state (gdevice,
                          gdk_screen_get_root_window (gscreen),
                          NULL, (GdkModifierType*)mods);
}

static void
meta_cursor_tracker_x11_sync_cursor (MetaCursorTracker *tracker)
{
  MetaDisplay *display = meta_get_display ();

  meta_error_trap_push (display);
  if (tracker->is_showing)
    XFixesShowCursor (display->xdisplay,
                      DefaultRootWindow (display->xdisplay));
  else
    XFixesHideCursor (display->xdisplay,
                      DefaultRootWindow (display->xdisplay));
  meta_error_trap_pop (display);
}

static void
meta_cursor_tracker_x11_ensure_cursor (MetaCursorTracker *tracker)
{
  MetaDisplay *display = meta_get_display ();
  XFixesCursorImage *cursor_image;
  MetaCursorReference *cursor;

  if (tracker->has_window_cursor)
    return;

  cursor_image = XFixesGetCursorImage (display->xdisplay);
  if (!cursor_image)
    return;

  cursor  = meta_cursor_reference_from_xfixes_cursor_image (cursor_image);

  _meta_cursor_tracker_set_window_cursor (tracker, TRUE, cursor);

  XFree (cursor_image);
}

static void
meta_cursor_tracker_x11_class_init (MetaCursorTrackerX11Class *klass)
{
  MetaCursorTrackerClass *cursor_tracker_class = META_CURSOR_TRACKER_CLASS (klass);

  cursor_tracker_class->get_pointer = meta_cursor_tracker_x11_get_pointer;
  cursor_tracker_class->sync_cursor = meta_cursor_tracker_x11_sync_cursor;
  cursor_tracker_class->ensure_cursor = meta_cursor_tracker_x11_ensure_cursor;
}

static void
meta_cursor_tracker_x11_init (MetaCursorTrackerX11 *self)
{
  MetaDisplay *display = meta_get_display ();

  XFixesSelectCursorInput (display->xdisplay,
                           DefaultRootWindow (display->xdisplay),
                           XFixesDisplayCursorNotifyMask);
}

gboolean
meta_cursor_tracker_x11_handle_xevent (MetaCursorTrackerX11 *tracker,
                                       XEvent               *xevent)
{
  MetaDisplay *display = meta_get_display ();
  XFixesCursorNotifyEvent *notify_event;

  if (xevent->xany.type != display->xfixes_event_base + XFixesCursorNotify)
    return FALSE;

  notify_event = (XFixesCursorNotifyEvent *)xevent;
  if (notify_event->subtype != XFixesDisplayCursorNotify)
    return FALSE;

  _meta_cursor_tracker_set_window_cursor (META_CURSOR_TRACKER (tracker), FALSE, NULL);

  return TRUE;
}
