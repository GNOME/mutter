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

#include "clutter-color-state.h"
#include "clutter-cursor-private.h"

#include "cogl/cogl.h"

enum
{
  PROP_0,
  PROP_COLOR_STATE,
  N_PROPS
};

static GParamSpec *obj_props[N_PROPS] = { 0, };

enum
{
  TEXTURE_CHANGED,
  N_SIGNALS,
};

static guint signals[N_SIGNALS] = { 0, };

typedef struct _ClutterCursorPrivate
{
  GObject parent;

  float texture_scale;
  MtkMonitorTransform texture_transform;
  gboolean has_viewport_src_rect;
  graphene_rect_t viewport_src_rect;
  gboolean has_viewport_dst_size;
  int viewport_dst_width;
  int viewport_dst_height;
  int hot_x, hot_y;

  ClutterColorState *color_state;
} ClutterCursorPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterCursor,
                                     clutter_cursor,
                                     G_TYPE_OBJECT)

gboolean
clutter_cursor_is_animated (ClutterCursor *cursor)
{
  ClutterCursorClass *klass = CLUTTER_CURSOR_GET_CLASS (cursor);

  if (klass->is_animated)
    return klass->is_animated (cursor);
  else
    return FALSE;
}

void
clutter_cursor_tick_frame (ClutterCursor *cursor)
{
  return CLUTTER_CURSOR_GET_CLASS (cursor)->tick_frame (cursor);
}

unsigned int
clutter_cursor_get_current_frame_time (ClutterCursor *cursor)
{
  return CLUTTER_CURSOR_GET_CLASS (cursor)->get_current_frame_time (cursor);
}

void
clutter_cursor_set_texture_scale (ClutterCursor *cursor,
                                  float          scale)
{
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  if (G_APPROX_VALUE (priv->texture_scale, scale, FLT_EPSILON))
    return;

  priv->texture_scale = scale;
  clutter_cursor_invalidate (cursor);
}

void
clutter_cursor_set_texture_transform (ClutterCursor       *cursor,
                                      MtkMonitorTransform  transform)
{
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  if (priv->texture_transform == transform)
    return;

  priv->texture_transform = transform;
  clutter_cursor_invalidate (cursor);
}

void
clutter_cursor_set_viewport_src_rect (ClutterCursor         *cursor,
                                      const graphene_rect_t *src_rect)
{
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

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
  clutter_cursor_invalidate (cursor);
}

void
clutter_cursor_reset_viewport_src_rect (ClutterCursor *cursor)
{
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  if (!priv->has_viewport_src_rect)
    return;

  priv->has_viewport_src_rect = FALSE;
  clutter_cursor_invalidate (cursor);
}

void
clutter_cursor_set_viewport_dst_size (ClutterCursor *cursor,
                                      int            dst_width,
                                      int            dst_height)
{
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  if (priv->has_viewport_dst_size &&
      priv->viewport_dst_width == dst_width &&
      priv->viewport_dst_height == dst_height)
    return;

  priv->has_viewport_dst_size = TRUE;
  priv->viewport_dst_width = dst_width;
  priv->viewport_dst_height = dst_height;
  clutter_cursor_invalidate (cursor);
}

void
clutter_cursor_reset_viewport_dst_size (ClutterCursor *cursor)
{
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  if (!priv->has_viewport_dst_size)
    return;

  priv->has_viewport_dst_size = FALSE;
  clutter_cursor_invalidate (cursor);
}

CoglTexture *
clutter_cursor_get_texture (ClutterCursor *cursor,
                            int           *hot_x,
                            int           *hot_y)
{
  return CLUTTER_CURSOR_GET_CLASS (cursor)->get_texture (cursor,
                                                         hot_x,
                                                         hot_y);
}

float
clutter_cursor_get_texture_scale (ClutterCursor *cursor)
{
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  return priv->texture_scale;
}

MtkMonitorTransform
clutter_cursor_get_texture_transform (ClutterCursor *cursor)
{
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  return priv->texture_transform;
}

const graphene_rect_t *
clutter_cursor_get_viewport_src_rect (ClutterCursor *cursor)
{
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  if (!priv->has_viewport_src_rect)
    return NULL;

  return &priv->viewport_src_rect;
}

gboolean
clutter_cursor_get_viewport_dst_size (ClutterCursor *cursor,
                                      int           *dst_width,
                                      int           *dst_height)
{
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  if (!priv->has_viewport_dst_size)
    return FALSE;

  *dst_width = priv->viewport_dst_width;
  *dst_height = priv->viewport_dst_height;
  return TRUE;
}

void
clutter_cursor_prepare_at (ClutterCursor   *cursor,
                           float            best_scale,
                           int              x,
                           int              y)
{
  ClutterCursorClass *cursor_class = CLUTTER_CURSOR_GET_CLASS (cursor);

  if (cursor_class->prepare_at)
    cursor_class->prepare_at (cursor, best_scale, x, y);
}

gboolean
clutter_cursor_realize_texture (ClutterCursor *cursor)
{
  return CLUTTER_CURSOR_GET_CLASS (cursor)->realize_texture (cursor);
}

void
clutter_cursor_invalidate (ClutterCursor *cursor)
{
  ClutterCursorClass *cursor_class = CLUTTER_CURSOR_GET_CLASS (cursor);

  if (cursor_class->invalidate)
    cursor_class->invalidate (cursor);
}

static void
clutter_cursor_init (ClutterCursor *cursor)
{
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  priv->texture_scale = 1.0f;
  priv->texture_transform = MTK_MONITOR_TRANSFORM_NORMAL;
}

static void
clutter_cursor_finalize (GObject *object)
{
  ClutterCursor *cursor = CLUTTER_CURSOR (object);
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  g_clear_object (&priv->color_state);

  G_OBJECT_CLASS (clutter_cursor_parent_class)->finalize (object);
}

static void
clutter_cursor_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  ClutterCursor *cursor = CLUTTER_CURSOR (object);
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  switch (prop_id)
    {
    case PROP_COLOR_STATE:
      g_set_object (&priv->color_state, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_cursor_class_init (ClutterCursorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = clutter_cursor_finalize;
  object_class->set_property = clutter_cursor_set_property;

  obj_props[PROP_COLOR_STATE] =
    g_param_spec_object ("color-state", NULL, NULL,
                         CLUTTER_TYPE_COLOR_STATE,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[TEXTURE_CHANGED] =
    g_signal_new ("texture-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

ClutterColorState *
clutter_cursor_get_color_state (ClutterCursor *cursor)
{
  ClutterCursorPrivate *priv =
    clutter_cursor_get_instance_private (cursor);

  return priv->color_state;
}

void
clutter_cursor_emit_texture_changed (ClutterCursor *cursor)
{
  g_signal_emit (cursor, signals[TEXTURE_CHANGED], 0);
}
