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
 * MetaCursorTracker:
 *
 * Mutter cursor tracking helper. Originally only tracking
 * the cursor image, now more of a "core pointer abstraction"
 */

#include "config.h"

#include "backends/meta-cursor-tracker-private.h"

#include <string.h>

#include "backends/meta-backend-private.h"
#include "cogl/cogl.h"
#include "core/display-private.h"
#include "clutter/clutter.h"
#include "meta/main.h"
#include "meta/util.h"

enum
{
  PROP_0,

  PROP_BACKEND,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaCursorTrackerPrivate
{
  MetaBackend *backend;

  gboolean is_showing;
  gboolean pointer_focus;

  int track_position_count;

  float x;
  float y;

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

  GList *cursor_sprites;
} MetaCursorTrackerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorTracker, meta_cursor_tracker,
                            G_TYPE_OBJECT)

enum
{
  CURSOR_CHANGED,
  POSITION_INVALIDATED,
  VISIBILITY_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

void
meta_cursor_tracker_notify_cursor_changed (MetaCursorTracker *tracker)
{
  g_signal_emit (tracker, signals[CURSOR_CHANGED], 0);
}

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
  MetaContext *context = meta_backend_get_context (priv->backend);
  MetaDisplay *display = meta_context_get_display (context);
  MetaCursorSprite *cursor = NULL;

  if (display && !meta_display_is_grabbed (display) && priv->has_window_cursor)
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
      meta_cursor_sprite_invalidate (cursor);
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
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (priv->backend);

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
meta_cursor_tracker_real_set_force_track_position (MetaCursorTracker *tracker,
                                                   gboolean           is_enabled)
{
}

static MetaCursorSprite *
meta_cursor_tracker_real_get_sprite (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  return priv->displayed_cursor;
}

void
meta_cursor_tracker_destroy (MetaCursorTracker *tracker)
{
  g_object_run_dispose (G_OBJECT (tracker));
  g_object_unref (tracker);
}

static void
meta_cursor_tracker_init (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  priv->is_showing = FALSE;
  priv->x = -1.0;
  priv->y = -1.0;
}

static void
meta_cursor_tracker_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (object);
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_cursor_tracker_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (object);
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_cursor_tracker_dispose (GObject *object)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (object);
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  g_clear_object (&priv->effective_cursor);
  g_clear_object (&priv->displayed_cursor);
  g_clear_object (&priv->window_cursor);
  g_clear_object (&priv->root_cursor);

  G_OBJECT_CLASS (meta_cursor_tracker_parent_class)->dispose (object);
}

static void
meta_cursor_tracker_class_init (MetaCursorTrackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_cursor_tracker_get_property;
  object_class->set_property = meta_cursor_tracker_set_property;
  object_class->dispose = meta_cursor_tracker_dispose;

  klass->set_force_track_position =
    meta_cursor_tracker_real_set_force_track_position;
  klass->get_sprite =
    meta_cursor_tracker_real_get_sprite;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[CURSOR_CHANGED] = g_signal_new ("cursor-changed",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 0);

  signals[POSITION_INVALIDATED] = g_signal_new ("position-invalidated",
                                                G_TYPE_FROM_CLASS (klass),
                                                G_SIGNAL_RUN_LAST,
                                                0,
                                                NULL, NULL, NULL,
                                                G_TYPE_NONE, 0);

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
 * Returns: (transfer none): the cursor tracker object for @display.
 */
MetaCursorTracker *
meta_cursor_tracker_get_for_display (MetaDisplay *display)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
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

/**
 * meta_cursor_tracker_get_sprite:
 * @tracker: a #MetaCursorTracker
 *
 * Get the #CoglTexture of the cursor sprite
 *
 * Returns: (transfer none) (nullable): the #CoglTexture of the cursor sprite
 */
CoglTexture *
meta_cursor_tracker_get_sprite (MetaCursorTracker *tracker)
{
  MetaCursorSprite *cursor_sprite;

  cursor_sprite = META_CURSOR_TRACKER_GET_CLASS (tracker)->get_sprite (tracker);

  if (!cursor_sprite)
    return NULL;

  meta_cursor_sprite_realize_texture (cursor_sprite);
  return meta_cursor_sprite_get_cogl_texture (cursor_sprite);
}

/**
 * meta_cursor_tracker_get_scale:
 * @tracker: a #MetaCursorTracker
 *
 * Get the scale factor of the cursor sprite
 *
 * Returns: The scale factor of the cursor sprite
 */
float
meta_cursor_tracker_get_scale (MetaCursorTracker *tracker)
{
  MetaCursorSprite *cursor_sprite;

  cursor_sprite = META_CURSOR_TRACKER_GET_CLASS (tracker)->get_sprite (tracker);

  if (!cursor_sprite)
    return 1.0;

  return meta_cursor_sprite_get_texture_scale (cursor_sprite);
}

/**
 * meta_cursor_tracker_get_hot:
 * @tracker: a #MetaCursorTracker
 * @x: (out): the x coordinate of the cursor hotspot
 * @y: (out): the y coordinate of the cursor hotspot
 *
 * Get the hotspot of the current cursor sprite.
 */
void
meta_cursor_tracker_get_hot (MetaCursorTracker *tracker,
                             int               *x,
                             int               *y)
{
  MetaCursorSprite *cursor_sprite;

  g_return_if_fail (META_IS_CURSOR_TRACKER (tracker));

  cursor_sprite = META_CURSOR_TRACKER_GET_CLASS (tracker)->get_sprite (tracker);

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
 * @cursor_sprite: (transfer none) (nullable): the new root cursor
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
meta_cursor_tracker_invalidate_position (MetaCursorTracker *tracker)
{
  g_signal_emit (tracker, signals[POSITION_INVALIDATED], 0);
}

/**
 * meta_cursor_tracker_get_pointer:
 * @tracker: a #MetaCursorTracker object
 * @coords: (out caller-allocates) (optional): the coordinates of the pointer
 * @mods: (out) (optional): the current #ClutterModifierType of the pointer
 *
 * Get the current pointer position and state.
 */
void
meta_cursor_tracker_get_pointer (MetaCursorTracker   *tracker,
                                 graphene_point_t    *coords,
                                 ClutterModifierType *mods)
{
  ClutterSeat *seat;
  ClutterInputDevice *cdevice;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  cdevice = clutter_seat_get_pointer (seat);

  clutter_seat_query_state (seat, cdevice, NULL, coords, mods);
}

void
meta_cursor_tracker_track_position (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  priv->track_position_count++;
  if (priv->track_position_count == 1)
    {
      MetaCursorTrackerClass *klass =
        META_CURSOR_TRACKER_GET_CLASS (tracker);

      klass->set_force_track_position (tracker, TRUE);
    }
}

void
meta_cursor_tracker_untrack_position (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  g_return_if_fail (priv->track_position_count > 0);

  priv->track_position_count--;
  if (priv->track_position_count == 0)
    {
      MetaCursorTrackerClass *klass =
        META_CURSOR_TRACKER_GET_CLASS (tracker);

      klass->set_force_track_position (tracker, FALSE);
    }
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
  ClutterSeat *seat;

  if (visible == priv->is_showing)
    return;
  priv->is_showing = visible;

  sync_cursor (tracker);

  g_signal_emit (tracker, signals[VISIBILITY_CHANGED], 0);

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());

  if (priv->is_showing)
    clutter_seat_inhibit_unfocus (seat);
  else
    clutter_seat_uninhibit_unfocus (seat);
}

MetaBackend *
meta_cursor_tracker_get_backend (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  return priv->backend;
}

void
meta_cursor_tracker_register_cursor_sprite (MetaCursorTracker *tracker,
                                            MetaCursorSprite  *sprite)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  priv->cursor_sprites = g_list_prepend (priv->cursor_sprites, sprite);
}

void
meta_cursor_tracker_unregister_cursor_sprite (MetaCursorTracker *tracker,
                                              MetaCursorSprite  *sprite)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  priv->cursor_sprites = g_list_remove (priv->cursor_sprites, sprite);
}

