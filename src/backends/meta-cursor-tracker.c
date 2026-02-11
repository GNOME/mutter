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
#include "backends/meta-cursor-xcursor.h"
#include "backends/meta-sprite.h"
#include "cogl/cogl.h"
#include "core/display-private.h"
#include "clutter/clutter-mutter.h"
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

  ClutterCursor *current_cursor;

  gboolean pointer_focus;

  int cursor_visibility_inhibitors;

  float x;
  float y;
} MetaCursorTrackerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorTracker, meta_cursor_tracker,
                            G_TYPE_OBJECT)

enum
{
  CURSOR_CHANGED,
  POSITION_INVALIDATED,
  VISIBILITY_CHANGED,
  CURSOR_PREFS_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
meta_cursor_tracker_notify_cursor_changed (MetaCursorTracker *tracker)
{
  g_signal_emit (tracker, signals[CURSOR_CHANGED], 0);
}

static void
cursor_texture_updated (ClutterCursor     *cursor,
                        MetaCursorTracker *tracker)
{
  meta_cursor_tracker_notify_cursor_changed (tracker);
}

static gboolean
update_current_cursor (MetaCursorTracker *tracker,
                       ClutterCursor     *cursor)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  if (priv->current_cursor == cursor)
    return FALSE;

  if (priv->current_cursor)
    {
      g_signal_handlers_disconnect_by_func (priv->current_cursor,
                                            cursor_texture_updated,
                                            tracker);
    }

  g_set_object (&priv->current_cursor, cursor);

  if (cursor)
    {
      clutter_cursor_invalidate (cursor);
      g_signal_connect (cursor, "texture-changed",
                        G_CALLBACK (cursor_texture_updated), tracker);
    }

  meta_cursor_tracker_notify_cursor_changed (tracker);

  return TRUE;
}

static void
set_pointer_visible (MetaCursorTracker *tracker,
                     gboolean           visible)
{
  MetaBackend *backend = meta_cursor_tracker_get_backend (tracker);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  ClutterStage *stage = NULL;
  ClutterSprite *sprite = NULL;

  if (visible)
    clutter_seat_inhibit_unfocus (seat);
  else
    clutter_seat_uninhibit_unfocus (seat);

  g_signal_emit (tracker, signals[VISIBILITY_CHANGED], 0);

  stage = CLUTTER_STAGE (meta_backend_get_stage (backend));

  if (stage)
    sprite = clutter_backend_get_pointer_sprite (clutter_backend, stage);

  if (sprite)
    {
      g_assert (META_IS_SPRITE (sprite));
      meta_sprite_sync_cursor (META_SPRITE (sprite));
    }
}

static ClutterCursor *
meta_cursor_tracker_real_get_sprite (MetaCursorTracker *tracker)
{
  MetaBackend *backend = meta_cursor_tracker_get_backend (tracker);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  MetaCursorRenderer *cursor_renderer;

  if (clutter_seat_is_unfocus_inhibited (seat))
    return NULL;

  cursor_renderer = meta_backend_get_cursor_renderer (backend);

  if (!cursor_renderer)
    return NULL;

  return meta_cursor_renderer_get_cursor (cursor_renderer);
}

static void
on_prefs_changed (MetaPreference pref,
                  gpointer       user_data)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (user_data);

  if (pref == META_PREF_CURSOR_SIZE ||
      pref == META_PREF_CURSOR_THEME)
    g_signal_emit (tracker, signals[CURSOR_PREFS_CHANGED], 0);
}

void
meta_cursor_tracker_destroy (MetaCursorTracker *tracker)
{
  meta_cursor_tracker_set_current_cursor (tracker, NULL);
  g_object_unref (tracker);
}

static void
meta_cursor_tracker_init (MetaCursorTracker *tracker)
{
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
meta_cursor_tracker_finalize (GObject *object)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (object);
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  g_clear_object (&priv->current_cursor);
  meta_prefs_remove_listener (on_prefs_changed, tracker);

  G_OBJECT_CLASS (meta_cursor_tracker_parent_class)->finalize (object);
}

static void
meta_cursor_tracker_constructed (GObject *object)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (object);
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  priv->x = -1.0;
  priv->y = -1.0;

  meta_prefs_add_listener (on_prefs_changed, tracker);

  set_pointer_visible (tracker, TRUE);

  G_OBJECT_CLASS (meta_cursor_tracker_parent_class)->constructed (object);
}

static void
meta_cursor_tracker_class_init (MetaCursorTrackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_cursor_tracker_get_property;
  object_class->set_property = meta_cursor_tracker_set_property;
  object_class->finalize = meta_cursor_tracker_finalize;
  object_class->constructed = meta_cursor_tracker_constructed;

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

  signals[CURSOR_PREFS_CHANGED] = g_signal_new ("cursor-prefs-changed",
                                                G_TYPE_FROM_CLASS (klass),
                                                G_SIGNAL_RUN_LAST,
                                                0, NULL, NULL, NULL,
                                                G_TYPE_NONE, 0);
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
  ClutterCursor *cursor;

  cursor = META_CURSOR_TRACKER_GET_CLASS (tracker)->get_sprite (tracker);

  if (!cursor)
    return NULL;

  clutter_cursor_realize_texture (cursor);
  return clutter_cursor_get_texture (cursor, NULL, NULL);
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
  ClutterCursor *cursor;

  cursor = META_CURSOR_TRACKER_GET_CLASS (tracker)->get_sprite (tracker);

  if (!cursor)
    return 1.0;

  return clutter_cursor_get_texture_scale (cursor);
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
  ClutterCursor *cursor;

  g_return_if_fail (META_IS_CURSOR_TRACKER (tracker));

  cursor = META_CURSOR_TRACKER_GET_CLASS (tracker)->get_sprite (tracker);

  if (cursor)
    {
      G_GNUC_UNUSED CoglTexture *texture = NULL;
      clutter_cursor_get_texture (cursor, x, y);
    }
  else
    {
      if (x)
        *x = 0;
      if (y)
        *y = 0;
    }
}

void
meta_cursor_tracker_set_current_cursor (MetaCursorTracker *tracker,
                                        ClutterCursor     *cursor)
{
  update_current_cursor (tracker, cursor);
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
  MetaBackend *backend = meta_cursor_tracker_get_backend (tracker);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);

  clutter_seat_query_state (seat, NULL, coords, mods);
}

gboolean
meta_cursor_tracker_get_pointer_visible (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  return priv->cursor_visibility_inhibitors <= 0;
}

void
meta_cursor_tracker_inhibit_cursor_visibility (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  priv->cursor_visibility_inhibitors++;

  if (priv->cursor_visibility_inhibitors == 1)
    set_pointer_visible (tracker, FALSE);
}

void
meta_cursor_tracker_uninhibit_cursor_visibility (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  g_return_if_fail (priv->cursor_visibility_inhibitors > 0);

  priv->cursor_visibility_inhibitors--;

  if (priv->cursor_visibility_inhibitors == 0)
    set_pointer_visible (tracker, TRUE);
}

MetaBackend *
meta_cursor_tracker_get_backend (MetaCursorTracker *tracker)
{
  MetaCursorTrackerPrivate *priv =
    meta_cursor_tracker_get_instance_private (tracker);

  return priv->backend;
}
