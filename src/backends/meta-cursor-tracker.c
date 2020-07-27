/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013 Red Hat, Inc.
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
 * Author: Giovanni Campagna <gcampagn@redhat.com>
 */

/**
 * SECTION:cursor-tracker
 * @title: MetaCursorTracker
 * @short_description: Mutter cursor tracking helper. Originally only
 *                     tracking the cursor image, now more of a "core
 *                     pointer abstraction"
 */

#include "config.h"

#include "backends/meta-cursor-tracker-private.h"

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <string.h>

#include "backends/meta-backend-private.h"
#include "backends/x11/cm/meta-cursor-sprite-xfixes.h"
#include "cogl/cogl.h"
#include "clutter/clutter.h"
#include "meta-marshal.h"
#include "meta/main.h"
#include "meta/meta-x11-errors.h"
#include "meta/util.h"
#include "x11/meta-x11-display-private.h"

struct _MetaCursorTracker
{
  GObject parent;
};

typedef struct _MetaCursorTrackerPrivate
{
  gboolean is_showing;

  MetaCursorSprite *effective_cursor; /* May be NULL when hidden */
  MetaCursorSprite *displayed_cursor;

  /* Wayland clients can set a NULL buffer as their cursor
   * explicitly, which means that we shouldn't display anything.
   * So, we can't simply store a NULL in window_cursor to
   * determine an unset window cursor; we need an extra boolean.
   */
  gboolean has_window_cursor;
  MetaCursorSprite *window_cursor;

  MetaCursorSprite *root_cursor;

  /* The cursor from the X11 server. */
  MetaCursorSpriteXfixes *xfixes_cursor;
} MetaCursorTrackerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorTracker, meta_cursor_tracker,
                            G_TYPE_OBJECT)

enum
{
  CURSOR_CHANGED,
  CURSOR_MOVED,
  VISIBILITY_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
cursor_texture_updated (MetaCursorSprite  *cursor,
                        MetaCursorTracker *tracker)
{
  g_signal_emit (tracker, signals[CURSOR_CHANGED], 0);
}

static gboolean
update_displayed_cursor (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);
  MetaDisplay *display = meta_get_display ();
  MetaCursorSprite *cursor = NULL;

  if (display && meta_display_windows_are_interactable (display) &&
      priv->has_window_cursor)
    cursor = priv->window_cursor;
  else
    cursor = priv->root_cursor;

  if (priv->displayed_cursor == cursor)
    return FALSE;

  if (priv->displayed_cursor)
    {
      g_signal_handlers_disconnect_by_func (priv->displayed_cursor,
                                            cursor_texture_updated,
                                            tracker);
    }

  g_set_object (&priv->displayed_cursor, cursor);

  if (cursor)
    {
      g_signal_connect (cursor, "texture-changed",
                        G_CALLBACK (cursor_texture_updated), tracker);
    }

  return TRUE;
}

static gboolean
update_effective_cursor (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);
  MetaCursorSprite *cursor = NULL;

  if (priv->is_showing)
    cursor = priv->displayed_cursor;

  return g_set_object (&priv->effective_cursor, cursor);
}

static void
change_cursor_renderer (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);
  MetaBackend *backend = meta_get_backend ();
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);

  meta_cursor_renderer_set_cursor (cursor_renderer, priv->effective_cursor);
}

static void
sync_cursor (MetaCursorTracker *tracker)
{
  gboolean cursor_changed = FALSE;

  cursor_changed = update_displayed_cursor (tracker);

  if (update_effective_cursor (tracker))
    change_cursor_renderer (tracker);

  if (cursor_changed)
    g_signal_emit (tracker, signals[CURSOR_CHANGED], 0);
}

static void
meta_cursor_tracker_init (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  priv->is_showing = TRUE;
}

static void
meta_cursor_tracker_finalize (GObject *object)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (object);
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  g_clear_object (&priv->effective_cursor);
  g_clear_object (&priv->displayed_cursor);
  g_clear_object (&priv->root_cursor);

  G_OBJECT_CLASS (meta_cursor_tracker_parent_class)->finalize (object);
}

static void
meta_cursor_tracker_class_init (MetaCursorTrackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_cursor_tracker_finalize;

  signals[CURSOR_CHANGED] = g_signal_new ("cursor-changed",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 0);

  /**
   * MetaCursorTracker::cursor-moved:
   * @cursor: The #MetaCursorTracker
   * @x: The new X coordinate of the cursor
   * @y: The new Y coordinate of the cursor
   *
   * Notifies when the cursor has moved to a new location.
   */
  signals[CURSOR_MOVED] = g_signal_new ("cursor-moved",
                                        G_TYPE_FROM_CLASS (klass),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL, NULL,
                                        meta_marshal_VOID__FLOAT_FLOAT,
                                        G_TYPE_NONE, 2,
                                        G_TYPE_FLOAT,
                                        G_TYPE_FLOAT);
  g_signal_set_va_marshaller (signals[CURSOR_MOVED],
                              G_TYPE_FROM_CLASS (klass),
                              meta_marshal_VOID__FLOAT_FLOATv);

  signals[VISIBILITY_CHANGED] = g_signal_new ("visibility-changed",
                                              G_TYPE_FROM_CLASS (klass),
                                              G_SIGNAL_RUN_LAST,
                                              0, NULL, NULL, NULL,
                                              G_TYPE_NONE, 0);
}

/**
 * meta_cursor_tracker_get_for_display:
 * @display: the #MetaDisplay
 *
 * Retrieves the cursor tracker object for @display.
 *
 * Returns: (transfer none):
 */
MetaCursorTracker *
meta_cursor_tracker_get_for_display (MetaDisplay *display)
{
  MetaBackend *backend = meta_get_backend ();
  MetaCursorTracker *tracker = meta_backend_get_cursor_tracker (backend);

  g_assert (tracker);

  return tracker;
}

static void
set_window_cursor (MetaCursorTracker *tracker,
                   gboolean           has_cursor,
                   MetaCursorSprite  *cursor_sprite)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  g_clear_object (&priv->window_cursor);
  if (cursor_sprite)
    priv->window_cursor = g_object_ref (cursor_sprite);
  priv->has_window_cursor = has_cursor;
  sync_cursor (tracker);
}

gboolean
meta_cursor_tracker_handle_xevent (MetaCursorTracker *tracker,
                                   XEvent            *xevent)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);
  MetaX11Display *x11_display = meta_get_display ()->x11_display;
  XFixesCursorNotifyEvent *notify_event;

  if (meta_is_wayland_compositor ())
    return FALSE;

  if (xevent->xany.type != x11_display->xfixes_event_base + XFixesCursorNotify)
    return FALSE;

  notify_event = (XFixesCursorNotifyEvent *)xevent;
  if (notify_event->subtype != XFixesDisplayCursorNotify)
    return FALSE;

  g_clear_object (&priv->xfixes_cursor);
  g_signal_emit (tracker, signals[CURSOR_CHANGED], 0);

  return TRUE;
}

static void
ensure_xfixes_cursor (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);
  MetaDisplay *display = meta_get_display ();
  g_autoptr (GError) error = NULL;

  if (priv->xfixes_cursor)
    return;

  priv->xfixes_cursor = meta_cursor_sprite_xfixes_new (display, &error);
  if (!priv->xfixes_cursor)
    g_warning ("Failed to create XFIXES cursor: %s", error->message);
}

/**
 * meta_cursor_tracker_get_sprite:
 *
 * Returns: (transfer none):
 */
CoglTexture *
meta_cursor_tracker_get_sprite (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);
  MetaCursorSprite *cursor_sprite;

  g_return_val_if_fail (META_IS_CURSOR_TRACKER (tracker), NULL);

  if (meta_is_wayland_compositor ())
    {
      cursor_sprite = priv->displayed_cursor;
    }
  else
    {
      ensure_xfixes_cursor (tracker);
      cursor_sprite = META_CURSOR_SPRITE (priv->xfixes_cursor);
    }

  if (cursor_sprite)
    {
      meta_cursor_sprite_realize_texture (cursor_sprite);
      return meta_cursor_sprite_get_cogl_texture (cursor_sprite);
    }
  else
    {
      return NULL;
    }
}

/**
 * meta_cursor_tracker_get_hot:
 * @tracker:
 * @x: (out):
 * @y: (out):
 *
 */
void
meta_cursor_tracker_get_hot (MetaCursorTracker *tracker,
                             int               *x,
                             int               *y)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);
  MetaCursorSprite *cursor_sprite;

  g_return_if_fail (META_IS_CURSOR_TRACKER (tracker));

  if (meta_is_wayland_compositor ())
    {
      cursor_sprite = priv->displayed_cursor;
    }
  else
    {
      ensure_xfixes_cursor (tracker);
      cursor_sprite = META_CURSOR_SPRITE (priv->xfixes_cursor);
    }

  if (cursor_sprite)
    meta_cursor_sprite_get_hotspot (cursor_sprite, x, y);
  else
    {
      if (x)
        *x = 0;
      if (y)
        *y = 0;
    }
}

void
meta_cursor_tracker_set_window_cursor (MetaCursorTracker *tracker,
                                       MetaCursorSprite  *cursor_sprite)
{
  set_window_cursor (tracker, TRUE, cursor_sprite);
}

void
meta_cursor_tracker_unset_window_cursor (MetaCursorTracker *tracker)
{
  set_window_cursor (tracker, FALSE, NULL);
}

/**
 * meta_cursor_tracker_set_root_cursor:
 * @tracker: a #MetaCursorTracker object.
 * @cursor_sprite: (transfer none): the new root cursor
 *
 * Sets the root cursor (the cursor that is shown if not modified by a window).
 * The #MetaCursorTracker will take a strong reference to the sprite.
 */
void
meta_cursor_tracker_set_root_cursor (MetaCursorTracker *tracker,
                                     MetaCursorSprite  *cursor_sprite)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  g_clear_object (&priv->root_cursor);
  if (cursor_sprite)
    priv->root_cursor = g_object_ref (cursor_sprite);

  sync_cursor (tracker);
}

void
meta_cursor_tracker_update_position (MetaCursorTracker *tracker,
                                     float              new_x,
                                     float              new_y)
{
  MetaBackend *backend = meta_get_backend ();
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);

  g_assert (meta_is_wayland_compositor ());

  meta_cursor_renderer_set_position (cursor_renderer, new_x, new_y);

  g_signal_emit (tracker, signals[CURSOR_MOVED], 0, new_x, new_y);
}

static void
get_pointer_position_gdk (int         *x,
                          int         *y,
                          int         *mods)
{
  GdkSeat *gseat;
  GdkDevice *gdevice;
  GdkScreen *gscreen;

  gseat = gdk_display_get_default_seat (gdk_display_get_default ());
  gdevice = gdk_seat_get_pointer (gseat);

  gdk_device_get_position (gdevice, &gscreen, x, y);
  if (mods)
    gdk_device_get_state (gdevice,
                          gdk_screen_get_root_window (gscreen),
                          NULL, (GdkModifierType*)mods);
}

static void
get_pointer_position_clutter (int         *x,
                              int         *y,
                              int         *mods)
{
  ClutterSeat *seat;
  ClutterInputDevice *cdevice;
  graphene_point_t point;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  cdevice = clutter_seat_get_pointer (seat);

  clutter_input_device_get_coords (cdevice, NULL, &point);
  if (x)
    *x = point.x;
  if (y)
    *y = point.y;
  if (mods)
    *mods = clutter_input_device_get_modifier_state (cdevice);
}

void
meta_cursor_tracker_get_pointer (MetaCursorTracker   *tracker,
                                 int                 *x,
                                 int                 *y,
                                 ClutterModifierType *mods)
{
  /* We can't use the clutter interface when not running as a wayland compositor,
     because we need to query the server, rather than using the last cached value.
     OTOH, on wayland we can't use GDK, because that only sees the events
     we forward to xwayland.
  */
  if (meta_is_wayland_compositor ())
    get_pointer_position_clutter (x, y, (int*)mods);
  else
    get_pointer_position_gdk (x, y, (int*)mods);
}

gboolean
meta_cursor_tracker_get_pointer_visible (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  return priv->is_showing;
}

void
meta_cursor_tracker_set_pointer_visible (MetaCursorTracker *tracker,
                                         gboolean           visible)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  if (visible == priv->is_showing)
    return;
  priv->is_showing = visible;

  sync_cursor (tracker);

  g_signal_emit (tracker, signals[VISIBILITY_CHANGED], 0);
}

MetaCursorSprite *
meta_cursor_tracker_get_displayed_cursor (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  return priv->displayed_cursor;
}
