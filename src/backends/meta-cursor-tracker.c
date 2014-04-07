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

#include <config.h>
#include <string.h>
#include <meta/main.h>
#include <meta/util.h>
#include <meta/errors.h>

#include "meta-cursor-private.h"
#include "meta-cursor-tracker-private.h"
#include "backends/native/meta-cursor-tracker-native.h"
#include "backends/x11/meta-cursor-tracker-x11.h"
#include "screen-private.h"

G_DEFINE_TYPE (MetaCursorTracker, meta_cursor_tracker, G_TYPE_OBJECT);

enum {
    CURSOR_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
meta_cursor_tracker_init (MetaCursorTracker *self)
{
  /* (JS) Best (?) that can be assumed since XFixes doesn't provide a way of
     detecting if the system mouse cursor is showing or not.

     On wayland we start with the cursor showing
  */
  self->is_showing = TRUE;
}

static void
meta_cursor_tracker_finalize (GObject *object)
{
  MetaCursorTracker *self = META_CURSOR_TRACKER (object);
  int i;

  if (self->displayed_cursor)
    meta_cursor_reference_unref (self->displayed_cursor);
  if (self->root_cursor)
    meta_cursor_reference_unref (self->root_cursor);

  for (i = 0; i < META_CURSOR_LAST; i++)
    if (self->theme_cursors[i])
      meta_cursor_reference_unref (self->theme_cursors[i]);

  G_OBJECT_CLASS (meta_cursor_tracker_parent_class)->finalize (object);
}

static void
default_do_nothing (MetaCursorTracker *tracker)
{
}

static void
default_load_cursor_pixels (MetaCursorTracker   *tracker,
                            MetaCursorReference *cursor,
                            uint8_t             *pixels,
                            int                  width,
                            int                  height,
                            int                  rowstride,
                            uint32_t             format)
{
}

static void
default_load_cursor_buffer (MetaCursorTracker   *tracker,
                            MetaCursorReference *cursor,
                            struct wl_resource  *buffer)
{
}

static void
meta_cursor_tracker_class_init (MetaCursorTrackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_cursor_tracker_finalize;

  klass->sync_cursor = default_do_nothing;
  klass->ensure_cursor = default_do_nothing;
  klass->load_cursor_pixels = default_load_cursor_pixels;
  klass->load_cursor_buffer = default_load_cursor_buffer;

  signals[CURSOR_CHANGED] = g_signal_new ("cursor-changed",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 0);
}

/**
 * meta_cursor_tracker_get_for_screen:
 * @screen: the #MetaScreen
 *
 * Retrieves the cursor tracker object for @screen.
 *
 * Returns: (transfer none):
 */
MetaCursorTracker *
meta_cursor_tracker_get_for_screen (MetaScreen *screen)
{
  MetaCursorTracker *self;

  if (screen->cursor_tracker)
    return screen->cursor_tracker;

  if (meta_is_wayland_compositor ())
    self = g_object_new (META_TYPE_CURSOR_TRACKER_NATIVE, NULL);
  else
    self = g_object_new (META_TYPE_CURSOR_TRACKER_X11, NULL);

  screen->cursor_tracker = self;
  return self;
}

/**
 * meta_cursor_tracker_get_sprite:
 *
 * Returns: (transfer none):
 */
CoglTexture *
meta_cursor_tracker_get_sprite (MetaCursorTracker *tracker)
{
  g_return_val_if_fail (META_IS_CURSOR_TRACKER (tracker), NULL);

  META_CURSOR_TRACKER_GET_CLASS (tracker)->ensure_cursor (tracker);

  if (tracker->current_cursor)
    return meta_cursor_reference_get_cogl_texture (tracker->current_cursor, NULL, NULL);
  else
    return NULL;
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
  g_return_if_fail (META_IS_CURSOR_TRACKER (tracker));

  META_CURSOR_TRACKER_GET_CLASS (tracker)->ensure_cursor (tracker);

  if (tracker->current_cursor)
    meta_cursor_reference_get_cogl_texture (tracker->current_cursor, x, y);
  else
    {
      if (x)
        *x = 0;
      if (y)
        *y = 0;
    }
}

void
_meta_cursor_tracker_set_window_cursor (MetaCursorTracker   *tracker,
                                        gboolean             has_cursor,
                                        MetaCursorReference *cursor)
{
  g_clear_pointer (&tracker->window_cursor, meta_cursor_reference_unref);
  if (cursor)
    tracker->window_cursor = meta_cursor_reference_ref (cursor);
  tracker->has_window_cursor = has_cursor;

  _meta_cursor_tracker_sync_cursor (tracker);
}

void
meta_cursor_tracker_set_window_cursor (MetaCursorTracker   *tracker,
                                       MetaCursorReference *cursor)
{
  _meta_cursor_tracker_set_window_cursor (tracker, TRUE, cursor);
}

void
meta_cursor_tracker_unset_window_cursor (MetaCursorTracker *tracker)
{
  _meta_cursor_tracker_set_window_cursor (tracker, FALSE, NULL);
}

void
meta_cursor_tracker_set_grab_cursor (MetaCursorTracker   *tracker,
                                     MetaCursorReference *cursor)
{
  g_clear_pointer (&tracker->grab_cursor, meta_cursor_reference_unref);
  if (cursor)
    tracker->grab_cursor = meta_cursor_reference_ref (cursor);

  _meta_cursor_tracker_sync_cursor (tracker);
}

void
meta_cursor_tracker_set_root_cursor (MetaCursorTracker   *tracker,
                                     MetaCursorReference *cursor)
{
  g_clear_pointer (&tracker->root_cursor, meta_cursor_reference_unref);
  if (cursor)
    tracker->root_cursor = meta_cursor_reference_ref (cursor);

  _meta_cursor_tracker_sync_cursor (tracker);
}

static MetaCursorReference *
get_current_cursor (MetaCursorTracker *tracker)
{
  if (tracker->grab_cursor)
    return tracker->grab_cursor;

  if (tracker->has_window_cursor)
    return tracker->window_cursor;

  return tracker->root_cursor;
}

static MetaCursorReference *
get_displayed_cursor (MetaCursorTracker *tracker)
{
  if (!tracker->is_showing)
    return NULL;

  return get_current_cursor (tracker);
}

void
_meta_cursor_tracker_sync_cursor (MetaCursorTracker *tracker)
{
  MetaCursorReference *current_cursor = get_current_cursor (tracker);
  MetaCursorReference *displayed_cursor = get_displayed_cursor (tracker);

  if (tracker->displayed_cursor != displayed_cursor)
    {
      g_clear_pointer (&tracker->displayed_cursor, meta_cursor_reference_unref);
      if (displayed_cursor)
        tracker->displayed_cursor = meta_cursor_reference_ref (displayed_cursor);

      META_CURSOR_TRACKER_GET_CLASS (tracker)->sync_cursor (tracker);
    }

  if (tracker->current_cursor != current_cursor)
    {
      g_clear_pointer (&tracker->current_cursor, meta_cursor_reference_unref);
      if (current_cursor)
        tracker->current_cursor = meta_cursor_reference_ref (current_cursor);

      g_signal_emit (tracker, signals[CURSOR_CHANGED], 0);
    }
}

void
meta_cursor_tracker_get_pointer (MetaCursorTracker   *tracker,
                                 int                 *x,
                                 int                 *y,
                                 ClutterModifierType *mods)
{
  META_CURSOR_TRACKER_GET_CLASS (tracker)->get_pointer (tracker, x, y, mods);
}

void
meta_cursor_tracker_set_pointer_visible (MetaCursorTracker *tracker,
                                         gboolean           visible)
{
  if (visible == tracker->is_showing)
    return;
  tracker->is_showing = visible;

  _meta_cursor_tracker_sync_cursor (tracker);
}

MetaCursorReference *
meta_cursor_tracker_get_cursor_from_theme (MetaCursorTracker   *tracker,
                                           MetaCursor           meta_cursor)
{
  MetaCursorReference *cursor;
  XcursorImage *xc_image;

  if (tracker->theme_cursors[meta_cursor])
    return meta_cursor_reference_ref (tracker->theme_cursors[meta_cursor]);

  xc_image = meta_display_load_x_cursor (meta_get_display (), meta_cursor);
  if (!xc_image)
    return NULL;

  cursor = meta_cursor_reference_from_xcursor_image (xc_image);

  META_CURSOR_TRACKER_GET_CLASS (tracker)->load_cursor_pixels (tracker,
                                                               cursor,
                                                               (uint8_t *) xc_image->pixels,
                                                               xc_image->width,
                                                               xc_image->height,
                                                               xc_image->width * 4,
                                                               GBM_FORMAT_ARGB8888);
  XcursorImageDestroy (xc_image);

  tracker->theme_cursors[meta_cursor] = cursor;

  return meta_cursor_reference_ref (cursor);
}

MetaCursorReference *
meta_cursor_tracker_get_cursor_from_buffer (MetaCursorTracker  *tracker,
                                            struct wl_resource *buffer,
                                            int                 hot_x,
                                            int                 hot_y)
{
  MetaCursorReference *cursor;

  cursor = meta_cursor_reference_from_buffer (buffer, hot_x, hot_y);

  META_CURSOR_TRACKER_GET_CLASS (tracker)->load_cursor_buffer (tracker,
                                                               cursor,
                                                               buffer);
  return cursor;
}
