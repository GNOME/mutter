/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "backends/meta-cursor-renderer.h"

#include <math.h>

#include "backends/meta-stage-private.h"
#include "clutter/clutter.h"
#include "clutter/clutter-mutter.h"
#include "cogl/cogl.h"
#include "core/boxes-private.h"
#include "meta/meta-backend.h"
#include "meta/util.h"

G_DEFINE_INTERFACE (MetaHwCursorInhibitor, meta_hw_cursor_inhibitor,
                    G_TYPE_OBJECT)

enum
{
  PROP_0,

  PROP_BACKEND,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _MetaCursorRendererPrivate
{
  MetaBackend *backend;

  float current_x;
  float current_y;

  MetaCursorSprite *displayed_cursor;
  MetaCursorSprite *overlay_cursor;

  MetaOverlay *stage_overlay;
  gboolean handled_by_backend;
  gulong after_paint_handler_id;

  GList *hw_cursor_inhibitors;
};
typedef struct _MetaCursorRendererPrivate MetaCursorRendererPrivate;

enum
{
  CURSOR_PAINTED,
  LAST_SIGNAL
};
static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorRenderer, meta_cursor_renderer, G_TYPE_OBJECT);

static gboolean
meta_hw_cursor_inhibitor_is_cursor_sprite_inhibited (MetaHwCursorInhibitor *inhibitor,
                                                     MetaCursorSprite      *cursor_sprite)
{
  MetaHwCursorInhibitorInterface *iface =
    META_HW_CURSOR_INHIBITOR_GET_IFACE (inhibitor);

  return iface->is_cursor_sprite_inhibited (inhibitor, cursor_sprite);
}

static void
meta_hw_cursor_inhibitor_default_init (MetaHwCursorInhibitorInterface *iface)
{
}

void
meta_cursor_renderer_emit_painted (MetaCursorRenderer *renderer,
                                   MetaCursorSprite   *cursor_sprite)
{
  g_signal_emit (renderer, signals[CURSOR_PAINTED], 0, cursor_sprite);
}

static void
align_cursor_position (MetaCursorRenderer *renderer,
                       graphene_rect_t    *rect)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  ClutterActor *stage = meta_backend_get_stage (priv->backend);
  ClutterStageView *view;
  cairo_rectangle_int_t view_layout;
  float view_scale;

  view = clutter_stage_get_view_at (CLUTTER_STAGE (stage),
                                    priv->current_x,
                                    priv->current_y);
  if (!view)
    return;

  clutter_stage_view_get_layout (view, &view_layout);
  view_scale = clutter_stage_view_get_scale (view);

  graphene_rect_offset (rect, -view_layout.x, -view_layout.y);
  rect->origin.x = floorf (rect->origin.x * view_scale) / view_scale;
  rect->origin.y = floorf (rect->origin.y * view_scale) / view_scale;
  graphene_rect_offset (rect, view_layout.x, view_layout.y);
}

void
meta_cursor_renderer_update_stage_overlay (MetaCursorRenderer *renderer,
                                           MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);
  ClutterActor *stage = meta_backend_get_stage (priv->backend);
  CoglTexture *texture;
  graphene_rect_t rect = GRAPHENE_RECT_INIT_ZERO;

  g_set_object (&priv->overlay_cursor, cursor_sprite);

  if (cursor_sprite)
    {
      rect = meta_cursor_renderer_calculate_rect (renderer, cursor_sprite);
      align_cursor_position (renderer, &rect);
    }

  if (!priv->stage_overlay)
    priv->stage_overlay = meta_stage_create_cursor_overlay (META_STAGE (stage));

  if (cursor_sprite)
    texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
  else
    texture = NULL;

  meta_overlay_set_visible (priv->stage_overlay, !priv->handled_by_backend);
  meta_stage_update_cursor_overlay (META_STAGE (stage), priv->stage_overlay,
                                    texture, &rect);
}

static void
meta_cursor_renderer_after_paint (ClutterStage       *stage,
                                  ClutterStageView   *stage_view,
                                  MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  if (priv->displayed_cursor && !priv->handled_by_backend)
    {
      graphene_rect_t rect;
      MetaRectangle view_layout;
      graphene_rect_t view_rect;

      rect = meta_cursor_renderer_calculate_rect (renderer,
                                                  priv->displayed_cursor);
      clutter_stage_view_get_layout (stage_view, &view_layout);
      view_rect = meta_rectangle_to_graphene_rect (&view_layout);
      if (graphene_rect_intersection (&rect, &view_rect, NULL))
        meta_cursor_renderer_emit_painted (renderer, priv->displayed_cursor);
    }
}

static gboolean
meta_cursor_renderer_real_update_cursor (MetaCursorRenderer *renderer,
                                         MetaCursorSprite   *cursor_sprite)
{
  if (cursor_sprite)
    meta_cursor_sprite_realize_texture (cursor_sprite);

  return FALSE;
}

static void
meta_cursor_renderer_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (object);
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

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
meta_cursor_renderer_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (object);
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

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
meta_cursor_renderer_finalize (GObject *object)
{
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (object);
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);
  ClutterActor *stage = meta_backend_get_stage (priv->backend);

  if (priv->stage_overlay)
    meta_stage_remove_cursor_overlay (META_STAGE (stage), priv->stage_overlay);

  g_clear_signal_handler (&priv->after_paint_handler_id, stage);

  g_clear_object (&priv->displayed_cursor);
  g_clear_object (&priv->overlay_cursor);

  G_OBJECT_CLASS (meta_cursor_renderer_parent_class)->finalize (object);
}

static void
meta_cursor_renderer_constructed (GObject *object)
{
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (object);
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  ClutterActor *stage;

  stage = meta_backend_get_stage (priv->backend);
  priv->after_paint_handler_id =
    g_signal_connect (stage, "after-paint",
                      G_CALLBACK (meta_cursor_renderer_after_paint),
                      renderer);

  G_OBJECT_CLASS (meta_cursor_renderer_parent_class)->constructed (object);
}

static void
meta_cursor_renderer_class_init (MetaCursorRendererClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_cursor_renderer_get_property;
  object_class->set_property = meta_cursor_renderer_set_property;
  object_class->finalize = meta_cursor_renderer_finalize;
  object_class->constructed = meta_cursor_renderer_constructed;
  klass->update_cursor = meta_cursor_renderer_real_update_cursor;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend",
                         "backend",
                         "MetaBackend",
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[CURSOR_PAINTED] = g_signal_new ("cursor-painted",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 1,
                                          G_TYPE_POINTER);
}

static void
meta_cursor_renderer_init (MetaCursorRenderer *renderer)
{
}

graphene_rect_t
meta_cursor_renderer_calculate_rect (MetaCursorRenderer *renderer,
                                     MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  CoglTexture *texture;
  int hot_x, hot_y;
  int width, height;
  float texture_scale;

  texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
  if (!texture)
    return (graphene_rect_t) GRAPHENE_RECT_INIT_ZERO;

  meta_cursor_sprite_get_hotspot (cursor_sprite, &hot_x, &hot_y);
  texture_scale = meta_cursor_sprite_get_texture_scale (cursor_sprite);
  width = cogl_texture_get_width (texture);
  height = cogl_texture_get_height (texture);

  return (graphene_rect_t) {
    .origin = {
      .x = priv->current_x - (hot_x * texture_scale),
      .y = priv->current_y - (hot_y * texture_scale)
    },
    .size = {
      .width = width * texture_scale,
      .height = height * texture_scale
    }
  };
}

static void
meta_cursor_renderer_update_cursor (MetaCursorRenderer *renderer,
                                    MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);
  gboolean handled_by_backend;

  if (cursor_sprite)
    meta_cursor_sprite_prepare_at (cursor_sprite,
                                   (int) priv->current_x,
                                   (int) priv->current_y);

  handled_by_backend =
    META_CURSOR_RENDERER_GET_CLASS (renderer)->update_cursor (renderer,
                                                              cursor_sprite);
  if (handled_by_backend != priv->handled_by_backend)
    priv->handled_by_backend = handled_by_backend;

  meta_cursor_renderer_update_stage_overlay (renderer, cursor_sprite);
}

MetaCursorRenderer *
meta_cursor_renderer_new (MetaBackend *backend)
{
  return g_object_new (META_TYPE_CURSOR_RENDERER,
                       "backend", backend,
                       NULL);
}

void
meta_cursor_renderer_set_cursor (MetaCursorRenderer *renderer,
                                 MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  if (priv->displayed_cursor == cursor_sprite)
    return;
  g_set_object (&priv->displayed_cursor, cursor_sprite);

  meta_cursor_renderer_update_cursor (renderer, cursor_sprite);
}

void
meta_cursor_renderer_force_update (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  meta_cursor_renderer_update_cursor (renderer, priv->displayed_cursor);
}

void
meta_cursor_renderer_set_position (MetaCursorRenderer *renderer,
                                   float               x,
                                   float               y)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  priv->current_x = x;
  priv->current_y = y;

  meta_cursor_renderer_update_cursor (renderer, priv->displayed_cursor);
}

graphene_point_t
meta_cursor_renderer_get_position (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  return (graphene_point_t) {
    .x = priv->current_x,
    .y = priv->current_y
  };
}

MetaCursorSprite *
meta_cursor_renderer_get_cursor (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  return priv->overlay_cursor;
}

gboolean
meta_cursor_renderer_is_overlay_visible (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  if (!priv->stage_overlay)
    return FALSE;

  return meta_overlay_is_visible (priv->stage_overlay);
}

void
meta_cursor_renderer_add_hw_cursor_inhibitor (MetaCursorRenderer    *renderer,
                                              MetaHwCursorInhibitor *inhibitor)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  priv->hw_cursor_inhibitors = g_list_prepend (priv->hw_cursor_inhibitors,
                                               inhibitor);
}

void
meta_cursor_renderer_remove_hw_cursor_inhibitor (MetaCursorRenderer    *renderer,
                                                 MetaHwCursorInhibitor *inhibitor)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  priv->hw_cursor_inhibitors = g_list_remove (priv->hw_cursor_inhibitors,
                                              inhibitor);
}

gboolean
meta_cursor_renderer_is_hw_cursors_inhibited (MetaCursorRenderer *renderer,
                                              MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  GList *l;

  for (l = priv->hw_cursor_inhibitors; l; l = l->next)
    {
      MetaHwCursorInhibitor *inhibitor = l->data;

      if (meta_hw_cursor_inhibitor_is_cursor_sprite_inhibited (inhibitor,
                                                               cursor_sprite))
        return TRUE;
    }

  return FALSE;
}
