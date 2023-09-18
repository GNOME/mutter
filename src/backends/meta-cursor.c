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

#include "config.h"

#include "backends/meta-cursor.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-tracker-private.h"
#include "cogl/cogl.h"
#include "meta/common.h"

enum
{
  PROP_0,

  PROP_CURSOR_TRACKER,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  TEXTURE_CHANGED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

typedef struct _MetaCursorSpritePrivate
{
  GObject parent;

  CoglTexture2D *texture;
  float texture_scale;
  MetaMonitorTransform texture_transform;
  int hot_x, hot_y;

  MetaCursorPrepareFunc prepare_func;
  gpointer prepare_func_data;

  MetaCursorTracker *cursor_tracker;
} MetaCursorSpritePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaCursorSprite,
                                     meta_cursor_sprite,
                                     G_TYPE_OBJECT)

gboolean
meta_cursor_sprite_is_animated (MetaCursorSprite *sprite)
{
  MetaCursorSpriteClass *klass = META_CURSOR_SPRITE_GET_CLASS (sprite);

  if (klass->is_animated)
    return klass->is_animated (sprite);
  else
    return FALSE;
}

void
meta_cursor_sprite_tick_frame (MetaCursorSprite *sprite)
{
  return META_CURSOR_SPRITE_GET_CLASS (sprite)->tick_frame (sprite);
}

unsigned int
meta_cursor_sprite_get_current_frame_time (MetaCursorSprite *sprite)
{
  return META_CURSOR_SPRITE_GET_CLASS (sprite)->get_current_frame_time (sprite);
}

void
meta_cursor_sprite_clear_texture (MetaCursorSprite *sprite)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  g_clear_object (&priv->texture);
  meta_cursor_sprite_invalidate (sprite);
}

void
meta_cursor_sprite_set_texture (MetaCursorSprite *sprite,
                                CoglTexture      *texture,
                                int               hot_x,
                                int               hot_y)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  g_clear_object (&priv->texture);
  if (texture)
    priv->texture = g_object_ref (COGL_TEXTURE_2D (texture));
  priv->hot_x = hot_x;
  priv->hot_y = hot_y;

  meta_cursor_sprite_invalidate (sprite);

  g_signal_emit (sprite, signals[TEXTURE_CHANGED], 0);
}

void
meta_cursor_sprite_set_texture_scale (MetaCursorSprite *sprite,
                                      float             scale)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  if (priv->texture_scale != scale)
    meta_cursor_sprite_invalidate (sprite);

  priv->texture_scale = scale;
}

void
meta_cursor_sprite_set_texture_transform (MetaCursorSprite     *sprite,
                                          MetaMonitorTransform  transform)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  if (priv->texture_transform != transform)
    meta_cursor_sprite_invalidate (sprite);

  priv->texture_transform = transform;
}

CoglTexture *
meta_cursor_sprite_get_cogl_texture (MetaCursorSprite *sprite)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  return COGL_TEXTURE (priv->texture);
}

void
meta_cursor_sprite_get_hotspot (MetaCursorSprite *sprite,
                                int              *hot_x,
                                int              *hot_y)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  *hot_x = priv->hot_x;
  *hot_y = priv->hot_y;
}

int
meta_cursor_sprite_get_width (MetaCursorSprite *sprite)
{
  CoglTexture *texture;

  texture = meta_cursor_sprite_get_cogl_texture (sprite);
  return cogl_texture_get_width (texture);
}

int
meta_cursor_sprite_get_height (MetaCursorSprite *sprite)
{
  CoglTexture *texture;

  texture = meta_cursor_sprite_get_cogl_texture (sprite);
  return cogl_texture_get_height (texture);
}

float
meta_cursor_sprite_get_texture_scale (MetaCursorSprite *sprite)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  return priv->texture_scale;
}

MetaMonitorTransform
meta_cursor_sprite_get_texture_transform (MetaCursorSprite *sprite)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  return priv->texture_transform;
}

void
meta_cursor_sprite_set_prepare_func (MetaCursorSprite      *sprite,
                                     MetaCursorPrepareFunc  func,
                                     gpointer               user_data)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  priv->prepare_func = func;
  priv->prepare_func_data = user_data;
}

void
meta_cursor_sprite_prepare_at (MetaCursorSprite   *sprite,
                               float               best_scale,
                               int                 x,
                               int                 y)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  if (priv->prepare_func)
    priv->prepare_func (sprite, best_scale, x, y, priv->prepare_func_data);
}

gboolean
meta_cursor_sprite_realize_texture (MetaCursorSprite *sprite)
{
  return META_CURSOR_SPRITE_GET_CLASS (sprite)->realize_texture (sprite);
}

void
meta_cursor_sprite_invalidate (MetaCursorSprite *sprite)
{
  MetaCursorSpriteClass *sprite_class = META_CURSOR_SPRITE_GET_CLASS (sprite);

  if (sprite_class->invalidate)
    sprite_class->invalidate (sprite);
}

static void
meta_cursor_sprite_init (MetaCursorSprite *sprite)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  priv->texture_scale = 1.0f;
  priv->texture_transform = META_MONITOR_TRANSFORM_NORMAL;
}

static void
meta_cursor_sprite_constructed (GObject *object)
{
  MetaCursorSprite *sprite = META_CURSOR_SPRITE (object);
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  g_assert (priv->cursor_tracker);

  meta_cursor_tracker_register_cursor_sprite (priv->cursor_tracker, sprite);

  g_clear_object (&priv->texture);

  G_OBJECT_CLASS (meta_cursor_sprite_parent_class)->constructed (object);
}

static void
meta_cursor_sprite_finalize (GObject *object)
{
  MetaCursorSprite *sprite = META_CURSOR_SPRITE (object);
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  g_clear_object (&priv->texture);

  meta_cursor_tracker_unregister_cursor_sprite (priv->cursor_tracker, sprite);
  g_clear_object (&priv->cursor_tracker);

  G_OBJECT_CLASS (meta_cursor_sprite_parent_class)->finalize (object);
}

static void
meta_cursor_tracker_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MetaCursorSprite *sprite = META_CURSOR_SPRITE (object);
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  switch (prop_id)
    {
    case PROP_CURSOR_TRACKER:
      g_set_object (&priv->cursor_tracker, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_cursor_sprite_class_init (MetaCursorSpriteClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_cursor_sprite_constructed;
  object_class->finalize = meta_cursor_sprite_finalize;
  object_class->set_property = meta_cursor_tracker_set_property;

  obj_props[PROP_CURSOR_TRACKER] =
    g_param_spec_object ("cursor-tracker", NULL, NULL,
                         META_TYPE_CURSOR_TRACKER,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[TEXTURE_CHANGED] = g_signal_new ("texture-changed",
                                           G_TYPE_FROM_CLASS (object_class),
                                           G_SIGNAL_RUN_LAST,
                                           0,
                                           NULL, NULL, NULL,
                                           G_TYPE_NONE, 0);
}
