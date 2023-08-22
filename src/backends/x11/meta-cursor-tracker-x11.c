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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "backends/x11/meta-cursor-tracker-x11.h"

#include "backends/x11/cm/meta-cursor-sprite-xfixes.h"
#include "clutter/clutter-private.h"
#include "x11/meta-x11-display-private.h"

#define UPDATE_POSITION_TIMEOUT_MS (ms (100))

struct _MetaCursorTrackerX11
{
  MetaCursorTracker parent;

  gboolean is_force_track_position_enabled;
  guint update_position_timeout_id;

  MetaCursorSpriteXfixes *xfixes_cursor;
};

G_DEFINE_TYPE (MetaCursorTrackerX11, meta_cursor_tracker_x11,
               META_TYPE_CURSOR_TRACKER)

static gboolean
ensure_xfixes_cursor (MetaCursorTrackerX11 *tracker_x11);

gboolean
meta_cursor_tracker_x11_handle_xevent (MetaCursorTrackerX11 *tracker_x11,
                                       XEvent               *xevent)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (tracker_x11);
  MetaBackend *backend = meta_cursor_tracker_get_backend (tracker);
  MetaContext *context = meta_backend_get_context (backend);
  MetaDisplay *display = meta_context_get_display (context);
  MetaX11Display *x11_display = meta_display_get_x11_display (display);
  XFixesCursorNotifyEvent *notify_event;

  if (xevent->xany.type != x11_display->xfixes_event_base + XFixesCursorNotify)
    return FALSE;

  notify_event = (XFixesCursorNotifyEvent *)xevent;
  if (notify_event->subtype != XFixesDisplayCursorNotify)
    return FALSE;

  g_clear_object (&tracker_x11->xfixes_cursor);
  meta_cursor_tracker_notify_cursor_changed (META_CURSOR_TRACKER (tracker_x11));

  return TRUE;
}

static void
update_position (MetaCursorTrackerX11 *tracker_x11)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (tracker_x11);

  meta_cursor_tracker_invalidate_position (tracker);
}

static gboolean
ensure_xfixes_cursor (MetaCursorTrackerX11 *tracker_x11)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (tracker_x11);
  MetaBackend *backend = meta_cursor_tracker_get_backend (tracker);
  MetaContext *context = meta_backend_get_context (backend);
  MetaDisplay *display = meta_context_get_display (context);
  MetaCursorTracker *cursor_tracker;
  g_autoptr (GError) error = NULL;

  if (tracker_x11->xfixes_cursor)
    return FALSE;

  cursor_tracker = META_CURSOR_TRACKER (tracker_x11);
  tracker_x11->xfixes_cursor = meta_cursor_sprite_xfixes_new (display,
                                                              cursor_tracker,
                                                              &error);
  if (!tracker_x11->xfixes_cursor)
    g_warning ("Failed to create XFIXES cursor: %s", error->message);

  return TRUE;
}

static gboolean
update_cursor_timeout (gpointer user_data)
{
  MetaCursorTrackerX11 *tracker_x11 = user_data;
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (tracker_x11);
  MetaBackend *backend = meta_cursor_tracker_get_backend (tracker);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  gboolean cursor_changed;
  MetaCursorSprite *cursor_sprite;

  update_position (tracker_x11);

  cursor_changed = ensure_xfixes_cursor (tracker_x11);

  if (tracker_x11->xfixes_cursor)
    cursor_sprite = META_CURSOR_SPRITE (tracker_x11->xfixes_cursor);
  else
    cursor_sprite = NULL;

  meta_cursor_renderer_update_stage_overlay (cursor_renderer, cursor_sprite);

  if (cursor_changed)
    meta_cursor_tracker_notify_cursor_changed (tracker);

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
                       update_cursor_timeout,
                       tracker_x11);
      update_position (tracker_x11);
    }
  else
    {
      g_clear_handle_id (&tracker_x11->update_position_timeout_id,
                         g_source_remove);
    }
}

static MetaCursorSprite *
meta_cursor_tracker_x11_get_sprite (MetaCursorTracker *tracker)
{
  MetaCursorTrackerX11 *tracker_x11 = META_CURSOR_TRACKER_X11 (tracker);

  ensure_xfixes_cursor (META_CURSOR_TRACKER_X11 (tracker));
  if (tracker_x11->xfixes_cursor)
    return META_CURSOR_SPRITE (tracker_x11->xfixes_cursor);
  else
    return NULL;
}

static void
meta_cursor_tracker_x11_dispose (GObject *object)
{
  MetaCursorTrackerX11 *tracker_x11 = META_CURSOR_TRACKER_X11 (object);

  g_clear_handle_id (&tracker_x11->update_position_timeout_id, g_source_remove);
  g_clear_object (&tracker_x11->xfixes_cursor);

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
  tracker_class->get_sprite =
    meta_cursor_tracker_x11_get_sprite;
}
