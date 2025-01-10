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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "backends/meta-cursor-renderer.h"

#include <math.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-stage-private.h"
#include "clutter/clutter.h"
#include "clutter/clutter-mutter.h"
#include "cogl/cogl.h"
#include "core/boxes-private.h"
#include "meta/meta-backend.h"
#include "meta/util.h"
#include "mtk/mtk.h"

G_DEFINE_INTERFACE (MetaHwCursorInhibitor, meta_hw_cursor_inhibitor,
                    G_TYPE_OBJECT)

enum
{
  PROP_0,

  PROP_BACKEND,
  PROP_DEVICE,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _MetaCursorRendererPrivate
{
  MetaBackend *backend;

  float current_x;
  float current_y;

  ClutterInputDevice *device;
  MetaCursorSprite *displayed_cursor;
  MetaCursorSprite *overlay_cursor;

  MetaOverlay *stage_overlay;
  gboolean needs_overlay;
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

gboolean
meta_hw_cursor_inhibitor_is_cursor_inhibited (MetaHwCursorInhibitor *inhibitor)
{
  MetaHwCursorInhibitorInterface *iface =
    META_HW_CURSOR_INHIBITOR_GET_IFACE (inhibitor);

  return iface->is_cursor_inhibited (inhibitor);
}

static void
meta_hw_cursor_inhibitor_default_init (MetaHwCursorInhibitorInterface *iface)
{
}

void
meta_cursor_renderer_emit_painted (MetaCursorRenderer *renderer,
                                   MetaCursorSprite   *cursor_sprite,
                                   ClutterStageView   *stage_view)
{
  g_signal_emit (renderer, signals[CURSOR_PAINTED], 0, cursor_sprite, stage_view);
}

static void
align_cursor_position (MetaCursorRenderer *renderer,
                       graphene_rect_t    *rect)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  ClutterActor *stage = meta_backend_get_stage (priv->backend);
  ClutterStageView *view;
  MtkRectangle view_layout;
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
  CoglTexture *texture = NULL;
  graphene_rect_t rect = GRAPHENE_RECT_INIT_ZERO;
  MtkMonitorTransform buffer_transform = MTK_MONITOR_TRANSFORM_NORMAL;

  g_set_object (&priv->overlay_cursor, cursor_sprite);

  if (!priv->stage_overlay)
    priv->stage_overlay = meta_stage_create_cursor_overlay (META_STAGE (stage));

  if (cursor_sprite)
    {
      rect = meta_cursor_renderer_calculate_rect (renderer, cursor_sprite);
      align_cursor_position (renderer, &rect);

      texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
      buffer_transform =
        meta_cursor_sprite_get_texture_transform (cursor_sprite);
    }

  meta_overlay_set_visible (priv->stage_overlay, priv->needs_overlay);
  meta_stage_update_cursor_overlay (META_STAGE (stage), priv->stage_overlay,
                                    texture, &rect, buffer_transform);
}

static void
meta_cursor_renderer_after_paint (ClutterStage       *stage,
                                  ClutterStageView   *stage_view,
                                  ClutterFrame       *frame,
                                  MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  if (priv->displayed_cursor && priv->needs_overlay)
    {
      graphene_rect_t rect;
      MtkRectangle view_layout;
      graphene_rect_t view_rect;

      rect = meta_cursor_renderer_calculate_rect (renderer,
                                                  priv->displayed_cursor);
      clutter_stage_view_get_layout (stage_view, &view_layout);
      view_rect = mtk_rectangle_to_graphene_rect (&view_layout);
      if (graphene_rect_intersection (&rect, &view_rect, NULL))
        {
          meta_cursor_renderer_emit_painted (renderer,
                                             priv->displayed_cursor,
                                             stage_view);
        }
    }
}

static gboolean
meta_cursor_renderer_real_update_cursor (MetaCursorRenderer *renderer,
                                         MetaCursorSprite   *cursor_sprite)
{
  if (cursor_sprite)
    meta_cursor_sprite_realize_texture (cursor_sprite);

  return TRUE;
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
    case PROP_DEVICE:
      g_value_set_object (value, priv->device);
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
    case PROP_DEVICE:
      priv->device = g_value_get_object (value);
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
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_DEVICE] =
    g_param_spec_object ("device", NULL, NULL,
                         CLUTTER_TYPE_INPUT_DEVICE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[CURSOR_PAINTED] = g_signal_new ("cursor-painted",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 2,
                                          G_TYPE_POINTER,
                                          CLUTTER_TYPE_STAGE_VIEW);
}

static void
meta_cursor_renderer_init (MetaCursorRenderer *renderer)
{
}

static gboolean
calculate_sprite_geometry (MetaCursorSprite *cursor_sprite,
                           graphene_size_t  *size,
                           graphene_point_t *hotspot)
{
  CoglTexture *texture;
  int hot_x, hot_y;
  int width, height;
  float texture_scale;

  meta_cursor_sprite_realize_texture (cursor_sprite);
  texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
  if (!texture)
    return FALSE;

  meta_cursor_sprite_get_hotspot (cursor_sprite, &hot_x, &hot_y);
  texture_scale = meta_cursor_sprite_get_texture_scale (cursor_sprite);
  width = cogl_texture_get_width (texture);
  height = cogl_texture_get_height (texture);

  *size = (graphene_size_t) {
    .width = width * texture_scale,
    .height = height * texture_scale
  };
  *hotspot = (graphene_point_t) {
    .x = hot_x * texture_scale,
    .y = hot_y * texture_scale
  };
  return TRUE;
}

graphene_rect_t
meta_cursor_renderer_calculate_rect (MetaCursorRenderer *renderer,
                                     MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  graphene_rect_t rect = GRAPHENE_RECT_INIT_ZERO;
  graphene_point_t hotspot;

  if (!calculate_sprite_geometry (cursor_sprite, &rect.size, &hotspot))
    return GRAPHENE_RECT_INIT_ZERO;

  rect.origin = (graphene_point_t) { .x = -hotspot.x, .y = -hotspot.y };
  graphene_rect_offset (&rect, priv->current_x, priv->current_y);
  return rect;
}

static float
find_highest_logical_monitor_scale (MetaBackend      *backend,
                                    MetaCursorSprite *cursor_sprite)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  graphene_rect_t cursor_rect;
  GList *logical_monitors;
  GList *l;
  float highest_scale = 0.0f;

  cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
                                                     cursor_sprite);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      graphene_rect_t logical_monitor_rect =
        mtk_rectangle_to_graphene_rect (&logical_monitor->rect);

      if (!graphene_rect_intersection (&cursor_rect,
                                       &logical_monitor_rect,
                                       NULL))
        continue;

      highest_scale = MAX (highest_scale, logical_monitor->scale);
    }

  return highest_scale;
}

static void
meta_cursor_renderer_update_cursor (MetaCursorRenderer *renderer,
                                    MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  if (cursor_sprite)
    {
      float scale = find_highest_logical_monitor_scale (priv->backend,
                                                        cursor_sprite);
      meta_cursor_sprite_prepare_at (cursor_sprite,
                                     MAX (1, scale),
                                     (int) priv->current_x,
                                     (int) priv->current_y);
    }

  priv->needs_overlay =
    META_CURSOR_RENDERER_GET_CLASS (renderer)->update_cursor (renderer,
                                                              cursor_sprite);

  meta_cursor_renderer_update_stage_overlay (renderer, cursor_sprite);
}

MetaCursorRenderer *
meta_cursor_renderer_new (MetaBackend        *backend,
                          ClutterInputDevice *device)
{
  return g_object_new (META_TYPE_CURSOR_RENDERER,
                       "backend", backend,
                       "device", device,
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
meta_cursor_renderer_update_position (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);
  graphene_point_t pos;

  clutter_seat_query_state (clutter_input_device_get_seat (priv->device),
                            priv->device, NULL, &pos, NULL);
  priv->current_x = pos.x;
  priv->current_y = pos.y;

  meta_cursor_renderer_update_cursor (renderer, priv->displayed_cursor);
}

MetaCursorSprite *
meta_cursor_renderer_get_cursor (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  return priv->overlay_cursor;
}

ClutterInputDevice *
meta_cursor_renderer_get_input_device (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  return priv->device;
}

MetaBackend *
meta_cursor_renderer_get_backend (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  return priv->backend;
}
