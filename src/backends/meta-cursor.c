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
  PROP_COLOR_STATE,

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
  MtkMonitorTransform texture_transform;
  gboolean has_viewport_src_rect;
  graphene_rect_t viewport_src_rect;
  gboolean has_viewport_dst_size;
  int viewport_dst_width;
  int viewport_dst_height;
  int hot_x, hot_y;

  ClutterColorState *color_state;

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

  if (G_APPROX_VALUE (priv->texture_scale, scale, FLT_EPSILON))
    return;

  priv->texture_scale = scale;
  meta_cursor_sprite_invalidate (sprite);
}

void
meta_cursor_sprite_set_texture_transform (MetaCursorSprite    *sprite,
                                          MtkMonitorTransform  transform)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  if (priv->texture_transform == transform)
    return;

  priv->texture_transform = transform;
  meta_cursor_sprite_invalidate (sprite);
}

void
meta_cursor_sprite_set_viewport_src_rect (MetaCursorSprite      *sprite,
                                          const graphene_rect_t *src_rect)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  if (priv->has_viewport_src_rect &&
      G_APPROX_VALUE (priv->viewport_src_rect.origin.x,
                      src_rect->origin.x, FLT_EPSILON) &&
      G_APPROX_VALUE (priv->viewport_src_rect.origin.y,
                      src_rect->origin.y, FLT_EPSILON) &&
      G_APPROX_VALUE (priv->viewport_src_rect.size.width,
                      src_rect->size.width, FLT_EPSILON) &&
      G_APPROX_VALUE (priv->viewport_src_rect.size.height,
                      src_rect->size.height, FLT_EPSILON))
    return;

  priv->has_viewport_src_rect = TRUE;
  priv->viewport_src_rect = *src_rect;
  meta_cursor_sprite_invalidate (sprite);
}

void
meta_cursor_sprite_reset_viewport_src_rect (MetaCursorSprite *sprite)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  if (!priv->has_viewport_src_rect)
    return;

  priv->has_viewport_src_rect = FALSE;
  meta_cursor_sprite_invalidate (sprite);
}

void
meta_cursor_sprite_set_viewport_dst_size (MetaCursorSprite *sprite,
                                          int               dst_width,
                                          int               dst_height)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  if (priv->has_viewport_dst_size &&
      priv->viewport_dst_width == dst_width &&
      priv->viewport_dst_height == dst_height)
    return;

  priv->has_viewport_dst_size = TRUE;
  priv->viewport_dst_width = dst_width;
  priv->viewport_dst_height = dst_height;
  meta_cursor_sprite_invalidate (sprite);
}

void
meta_cursor_sprite_reset_viewport_dst_size (MetaCursorSprite *sprite)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  if (!priv->has_viewport_dst_size)
    return;

  priv->has_viewport_dst_size = FALSE;
  meta_cursor_sprite_invalidate (sprite);
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

MtkMonitorTransform
meta_cursor_sprite_get_texture_transform (MetaCursorSprite *sprite)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  return priv->texture_transform;
}

const graphene_rect_t *
meta_cursor_sprite_get_viewport_src_rect (MetaCursorSprite *sprite)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  if (!priv->has_viewport_src_rect)
    return NULL;

  return &priv->viewport_src_rect;
}

gboolean
meta_cursor_sprite_get_viewport_dst_size (MetaCursorSprite *sprite,
                                          int              *dst_width,
                                          int              *dst_height)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  if (!priv->has_viewport_dst_size)
    return FALSE;

  *dst_width = priv->viewport_dst_width;
  *dst_height = priv->viewport_dst_height;
  return TRUE;
}

void
meta_cursor_sprite_prepare_at (MetaCursorSprite   *sprite,
                               float               best_scale,
                               int                 x,
                               int                 y)
{
  MetaCursorSpriteClass *sprite_class = META_CURSOR_SPRITE_GET_CLASS (sprite);

  if (sprite_class->prepare_at)
    sprite_class->prepare_at (sprite, best_scale, x, y);
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
  priv->texture_transform = MTK_MONITOR_TRANSFORM_NORMAL;
}

static void
meta_cursor_sprite_constructed (GObject *object)
{
  MetaCursorSprite *sprite = META_CURSOR_SPRITE (object);
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  g_assert (priv->cursor_tracker);

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
  g_clear_object (&priv->color_state);

  g_clear_object (&priv->cursor_tracker);

  G_OBJECT_CLASS (meta_cursor_sprite_parent_class)->finalize (object);
}

static void
meta_cursor_sprite_set_property (GObject      *object,
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
    case PROP_COLOR_STATE:
      g_set_object (&priv->color_state, g_value_get_object (value));
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
  object_class->set_property = meta_cursor_sprite_set_property;

  obj_props[PROP_CURSOR_TRACKER] =
    g_param_spec_object ("cursor-tracker", NULL, NULL,
                         META_TYPE_CURSOR_TRACKER,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_COLOR_STATE] =
    g_param_spec_object ("color-state", NULL, NULL,
                         CLUTTER_TYPE_COLOR_STATE,
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

ClutterColorState *
meta_cursor_sprite_get_color_state (MetaCursorSprite *sprite)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  return priv->color_state;
}

MetaCursorTracker *
meta_cursor_sprite_get_cursor_tracker (MetaCursorSprite *sprite)
{
  MetaCursorSpritePrivate *priv =
    meta_cursor_sprite_get_instance_private (sprite);

  return priv->cursor_tracker;
}
