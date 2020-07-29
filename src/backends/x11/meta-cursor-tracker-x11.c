/*
 * Copyright (C) 2020 Red Hat
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "backends/x11/meta-cursor-tracker-x11.h"

#include "clutter/clutter-private.h"

#define UPDATE_POSITION_TIMEOUT_MS (ms (100))

struct _MetaCursorTrackerX11
{
  MetaCursorTracker parent;

  gboolean is_force_track_position_enabled;
  guint update_position_timeout_id;
};

G_DEFINE_TYPE (MetaCursorTrackerX11, meta_cursor_tracker_x11,
               META_TYPE_CURSOR_TRACKER)

static void
update_position (MetaCursorTrackerX11 *tracker_x11)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (tracker_x11);
  int x, y;

  meta_cursor_tracker_get_pointer (tracker, &x, &y, NULL);
  meta_cursor_tracker_update_position (tracker, x, y);
}

static gboolean
update_position_timeout (gpointer user_data)
{
  MetaCursorTrackerX11 *tracker_x11 = user_data;

  update_position (tracker_x11);

  return G_SOURCE_CONTINUE;
}

static void
meta_cursor_tracker_x11_set_force_track_position (MetaCursorTracker *tracker,
                                                  gboolean           is_enabled)
{
  MetaCursorTrackerX11 *tracker_x11 = META_CURSOR_TRACKER_X11 (tracker);

  if (tracker_x11->is_force_track_position_enabled == is_enabled)
    return;

  tracker_x11->is_force_track_position_enabled = is_enabled;

  if (is_enabled)
    {
      tracker_x11->update_position_timeout_id =
        g_timeout_add (UPDATE_POSITION_TIMEOUT_MS,
                       update_position_timeout,
                       tracker_x11);
      update_position (tracker_x11);
    }
  else
    {
      g_clear_handle_id (&tracker_x11->update_position_timeout_id,
                         g_source_remove);
    }
}

static void
meta_cursor_tracker_x11_dispose (GObject *object)
{
  MetaCursorTrackerX11 *tracker_x11 = META_CURSOR_TRACKER_X11 (object);

  g_clear_handle_id (&tracker_x11->update_position_timeout_id, g_source_remove);

  G_OBJECT_CLASS (meta_cursor_tracker_x11_parent_class)->dispose (object);
}

static void
meta_cursor_tracker_x11_init (MetaCursorTrackerX11 *tracker_x11)
{
}

static void
meta_cursor_tracker_x11_class_init (MetaCursorTrackerX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaCursorTrackerClass *tracker_class = META_CURSOR_TRACKER_CLASS (klass);

  object_class->dispose = meta_cursor_tracker_x11_dispose;

  tracker_class->set_force_track_position =
    meta_cursor_tracker_x11_set_force_track_position;
}
