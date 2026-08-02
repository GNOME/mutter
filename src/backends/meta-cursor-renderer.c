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
#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-stage-private.h"
#include "backends/meta-stage-view.h"
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
  PROP_SPRITE,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _MetaCursorRendererPrivate
{
  MetaBackend *backend;

  float current_x;
  float current_y;

  ClutterSprite *sprite;
  ClutterCursor *displayed_cursor;
  ClutterCursor *overlay_cursor;

  MetaOverlay *stage_overlay;
  gulong after_paint_handler_id;
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
                                   ClutterCursor      *cursor,
                                   ClutterStageView   *stage_view,
                                   int64_t             view_frame_counter)
{
  g_signal_emit (renderer, signals[CURSOR_PAINTED], 0, cursor,
                 stage_view, view_frame_counter);
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
                                           ClutterCursor      *cursor)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);
  ClutterActor *stage = meta_backend_get_stage (priv->backend);
  CoglTexture *texture = NULL;
  graphene_rect_t dst_rect = GRAPHENE_RECT_INIT_ZERO;
  graphene_matrix_t matrix;
  gboolean cursor_has_content;
  gboolean any_view_needs_overlay = FALSE;
  GList *l;

  g_set_object (&priv->overlay_cursor, cursor);

  if (!priv->stage_overlay)
    priv->stage_overlay = meta_stage_create_cursor_overlay (META_STAGE (stage));

  cursor_has_content =
    cursor &&
    clutter_cursor_get_cursor_type (cursor) != CLUTTER_CURSOR_NONE &&
    (clutter_cursor_get_data (cursor, NULL) ||
     clutter_cursor_get_texture (cursor));

  for (l = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage)); l; l = l->next)
    {
      ClutterStageView *view = CLUTTER_STAGE_VIEW (l->data);
      gboolean view_needs_overlay;

      view_needs_overlay =
        cursor_has_content &&
        !META_CURSOR_RENDERER_GET_CLASS (renderer)->view_has_hw_cursor (renderer,
                                                                        view);
      meta_overlay_set_view_visible (priv->stage_overlay, view, view_needs_overlay);
      any_view_needs_overlay |= view_needs_overlay;
    }

  graphene_matrix_init_identity (&matrix);
  if (any_view_needs_overlay)
    {
      dst_rect = meta_cursor_renderer_calculate_rect (renderer, cursor);
      align_cursor_position (renderer, &dst_rect);

      texture = clutter_cursor_get_texture (cursor);
      if (texture)
        {
          int cursor_width, cursor_height;
          float cursor_scale;
          MtkMonitorTransform cursor_transform;
          const graphene_rect_t *src_rect;

          clutter_cursor_get_geometry (cursor,
                                       &cursor_width, &cursor_height,
                                       NULL, NULL);
          cursor_scale = clutter_cursor_get_scale (cursor);
          cursor_transform = clutter_cursor_get_transform (cursor);
          src_rect = clutter_cursor_get_viewport_src_rect (cursor);
          mtk_compute_viewport_matrix (&matrix,
                                       cursor_width,
                                       cursor_height,
                                       cursor_scale,
                                       cursor_transform,
                                       src_rect);
        }
    }

  meta_stage_update_cursor_overlay (META_STAGE (stage),
                                    priv->stage_overlay,
                                    texture,
                                    &matrix,
                                    &dst_rect);
}

/**
 * meta_cursor_renderer_needs_overlay_on_view:
 * @renderer: a #MetaCursorRenderer
 * @view: the #ClutterStageView to query
 *
 * Returns whether the software cursor overlay is currently visible on
 * @view specifically - never a stage-wide aggregate, since different views
 * can independently need (or not need) a software-composited cursor.
 *
 * This reflects the cursor state only. It does not account for a view whose
 * cursor overlay is inhibited (see
 * meta_stage_view_is_cursor_overlay_inhibited()), which suppresses the paint
 * independently of whether the cursor needs one: callers that care whether
 * the cursor actually ends up composited must check that as well.
 */
gboolean
meta_cursor_renderer_needs_overlay_on_view (MetaCursorRenderer *renderer,
                                            ClutterStageView   *view)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  if (!priv->stage_overlay)
    return FALSE;

  return meta_overlay_get_view_visible (priv->stage_overlay, view);
}

static void
meta_cursor_renderer_after_paint (ClutterStage       *stage,
                                  ClutterStageView   *stage_view,
                                  ClutterFrame       *frame,
                                  MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  /* An inhibited view never paints the overlay, so ::cursor-painted must not
   * claim the cursor was composited there. */
  if (priv->displayed_cursor &&
      meta_cursor_renderer_needs_overlay_on_view (renderer, stage_view) &&
      !meta_stage_view_is_cursor_overlay_inhibited (META_STAGE_VIEW (stage_view)))
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
                                             stage_view,
                                             frame->frame_count);
        }
    }
}

static void
meta_cursor_renderer_real_update_cursor (MetaCursorRenderer *renderer,
                                         ClutterCursor      *cursor)
{
}

static gboolean
meta_cursor_renderer_real_view_has_hw_cursor (MetaCursorRenderer *renderer,
                                              ClutterStageView   *view)
{
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
    case PROP_SPRITE:
      g_value_set_object (value, priv->sprite);
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
    case PROP_SPRITE:
      meta_cursor_renderer_set_sprite (renderer, g_value_get_object (value));
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
  g_clear_object (&priv->sprite);

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
  klass->view_has_hw_cursor = meta_cursor_renderer_real_view_has_hw_cursor;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_SPRITE] =
    g_param_spec_object ("sprite", NULL, NULL,
                         CLUTTER_TYPE_SPRITE,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[CURSOR_PAINTED] = g_signal_new ("cursor-painted",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 3,
                                          G_TYPE_POINTER,
                                          CLUTTER_TYPE_STAGE_VIEW,
                                          G_TYPE_INT64);
}

static void
meta_cursor_renderer_init (MetaCursorRenderer *renderer)
{
}

static gboolean
calculate_sprite_geometry (MetaCursorRenderer *renderer,
                           ClutterCursor      *cursor,
                           graphene_size_t    *size,
                           graphene_point_t   *hotspot)
{
  MtkMonitorTransform cursor_transform;
  const graphene_rect_t *src_rect;
  int hot_x, hot_y;
  int cursor_width, cursor_height;
  int dst_width, dst_height;

  clutter_cursor_get_geometry (cursor,
                               &cursor_width, &cursor_height,
                               &hot_x, &hot_y);
  cursor_transform = clutter_cursor_get_transform (cursor);
  src_rect = clutter_cursor_get_viewport_src_rect (cursor);

  if (clutter_cursor_get_viewport_dst_size (cursor,
                                            &dst_width,
                                            &dst_height))
    {
      float scale_x;
      float scale_y;

      scale_x = (float) dst_width / cursor_width;
      scale_y = (float) dst_height / cursor_height;

      *size = (graphene_size_t) {
        .width = dst_width,
        .height = dst_height,
      };
      *hotspot = (graphene_point_t) {
        .x = roundf (hot_x * scale_x),
        .y = roundf (hot_y * scale_y),
      };
    }
  else if (src_rect)
    {
      float cursor_scale = clutter_cursor_get_scale (cursor);

      *size = (graphene_size_t) {
        .width = src_rect->size.width * cursor_scale,
        .height = src_rect->size.height * cursor_scale
      };
      *hotspot = (graphene_point_t) {
        .x = roundf (hot_x * cursor_scale),
        .y = roundf (hot_y * cursor_scale),
      };
    }
  else
    {
      float cursor_scale = clutter_cursor_get_scale (cursor);

      if (mtk_monitor_transform_is_rotated (cursor_transform))
        {
          *size = (graphene_size_t) {
            .width = cursor_height * cursor_scale,
            .height = cursor_width * cursor_scale
          };
        }
      else
        {
          *size = (graphene_size_t) {
            .width = cursor_width * cursor_scale,
            .height = cursor_height * cursor_scale
          };
        }

      *hotspot = (graphene_point_t) {
        .x = roundf (hot_x * cursor_scale),
        .y = roundf (hot_y * cursor_scale),
      };
    }
  return TRUE;
}

graphene_rect_t
meta_cursor_renderer_calculate_rect (MetaCursorRenderer *renderer,
                                     ClutterCursor      *cursor)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  graphene_rect_t rect = GRAPHENE_RECT_INIT_ZERO;
  graphene_point_t hotspot;

  if (!calculate_sprite_geometry (renderer,
                                  cursor,
                                  &rect.size,
                                  &hotspot))
    return GRAPHENE_RECT_INIT_ZERO;

  rect.origin = (graphene_point_t) { .x = -hotspot.x, .y = -hotspot.y };
  graphene_rect_offset (&rect, priv->current_x, priv->current_y);
  return rect;
}

static float
find_highest_logical_monitor_scale (MetaCursorRenderer *renderer,
                                    ClutterCursor      *cursor)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (priv->backend);
  graphene_rect_t cursor_rect;
  GList *logical_monitors;
  GList *l;
  float highest_scale = 0.0f;

  cursor_rect = meta_cursor_renderer_calculate_rect (renderer, cursor);

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
                                    ClutterCursor      *cursor)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  if (cursor)
    {
      float scale = find_highest_logical_monitor_scale (renderer, cursor);
      clutter_cursor_prepare_at (cursor,
                                 MAX (1, scale),
                                 priv->current_x,
                                 priv->current_y);
    }

  META_CURSOR_RENDERER_GET_CLASS (renderer)->update_cursor (renderer, cursor);

  meta_cursor_renderer_update_stage_overlay (renderer, cursor);
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
                                 ClutterCursor      *cursor)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  if (priv->displayed_cursor == cursor)
    return;
  g_set_object (&priv->displayed_cursor, cursor);

  meta_cursor_renderer_update_cursor (renderer, cursor);
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
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (priv->backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  graphene_point_t pos;

  clutter_seat_query_state (seat, priv->sprite, &pos, NULL);
  priv->current_x = pos.x;
  priv->current_y = pos.y;

  meta_cursor_renderer_update_cursor (renderer, priv->displayed_cursor);
}

ClutterCursor *
meta_cursor_renderer_get_cursor (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  return priv->overlay_cursor;
}

ClutterSprite *
meta_cursor_renderer_get_sprite (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  return priv->sprite;
}

void
meta_cursor_renderer_set_sprite (MetaCursorRenderer *renderer,
                                 ClutterSprite      *sprite)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  ClutterCursor *cursor;

  g_set_object (&priv->sprite, sprite);

  if (META_CURSOR_RENDERER_GET_CLASS (renderer)->update_sprite)
    META_CURSOR_RENDERER_GET_CLASS (renderer)->update_sprite (renderer, sprite);

  if (priv->sprite)
    {
      cursor = clutter_sprite_get_cursor (sprite);
      meta_cursor_renderer_update_position (renderer);
      meta_cursor_renderer_set_cursor (renderer, cursor);
    }
  else
    {
      meta_cursor_renderer_set_cursor (renderer, NULL);
    }
}
