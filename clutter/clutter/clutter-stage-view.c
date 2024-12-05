/*
 * Copyright (C) 2016 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#define COGL_DISABLE_DEPRECATION_WARNINGS

#include "clutter/clutter-stage-view.h"
#include "clutter/clutter-stage-view-private.h"

#include <math.h>

#include "clutter/clutter-context-private.h"
#include "clutter/clutter-damage-history.h"
#include "clutter/clutter-frame-clock.h"
#include "clutter/clutter-frame-private.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter-stage-private.h"
#include "cogl/cogl.h"

enum
{
  PROP_0,

  PROP_NAME,
  PROP_STAGE,
  PROP_LAYOUT,
  PROP_FRAMEBUFFER,
  PROP_USE_SHADOWFB,
  PROP_COLOR_STATE,
  PROP_OUTPUT_COLOR_STATE,
  PROP_SCALE,
  PROP_REFRESH_RATE,
  PROP_VBLANK_DURATION_US,
  PROP_TRANSFORM,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

enum
{
  DESTROY,
  N_SIGNALS
};

guint stage_view_signals[N_SIGNALS] = { 0 };

typedef struct _ClutterStageViewPrivate
{
  char *name;

  ClutterStage *stage;

  MtkRectangle layout;
  float scale;
  MtkMonitorTransform transform;
  CoglFramebuffer *framebuffer;
  ClutterColorState *color_state;
  ClutterColorState *output_color_state;

  guint ensure_offscreen_idle_id;
  CoglOffscreen *offscreen;
  CoglPipeline *offscreen_pipeline;

  gboolean use_shadowfb;
  struct {
    CoglOffscreen *framebuffer;
  } shadow;

  CoglScanout *next_scanout;

  gboolean has_redraw_clip;
  MtkRegion *redraw_clip;
  gboolean has_accumulated_redraw_clip;
  MtkRegion *accumulated_redraw_clip;

  float refresh_rate;
  int64_t vblank_duration_us;
  ClutterFrameClock *frame_clock;

  struct {
    int frame_count;
    int64_t last_print_time_us;
    int64_t cumulative_draw_time_us;
    int64_t began_draw_time_us;
    int64_t worst_draw_time_us;
  } frame_timings;

  guint dirty_viewport   : 1;
  guint dirty_projection : 1;
  guint needs_update_devices : 1;
} ClutterStageViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterStageView, clutter_stage_view, G_TYPE_OBJECT)

void
clutter_stage_view_destroy (ClutterStageView *view)
{
  g_object_run_dispose (G_OBJECT (view));
  g_object_unref (view);
}

void
clutter_stage_view_get_layout (ClutterStageView *view,
                               MtkRectangle     *rect)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  *rect = priv->layout;
}

/**
 * clutter_stage_view_get_framebuffer:
 * @view: a #ClutterStageView
 *
 * Retrieves the framebuffer of @view to draw to.
 *
 * Returns: (transfer none): a #CoglFramebuffer
 */
CoglFramebuffer *
clutter_stage_view_get_framebuffer (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_warn_if_fail (priv->ensure_offscreen_idle_id == 0);

  if (priv->offscreen)
    return COGL_FRAMEBUFFER (priv->offscreen);
  else if (priv->shadow.framebuffer)
    return COGL_FRAMEBUFFER (priv->shadow.framebuffer);
  else
    return priv->framebuffer;
}

/**
 * clutter_stage_view_get_onscreen:
 * @view: a #ClutterStageView
 *
 * Retrieves the onscreen framebuffer of @view if available.
 *
 * Returns: (transfer none): a #CoglFramebuffer
 */
CoglFramebuffer *
clutter_stage_view_get_onscreen (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->framebuffer;
}

static CoglOffscreen *
create_offscreen (ClutterStageView  *view,
                  CoglPixelFormat    format,
                  int                width,
                  int                height,
                  GError           **error)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  CoglContext *cogl_context;
  g_autoptr (CoglOffscreen) framebuffer = NULL;
  g_autoptr (CoglTexture) texture = NULL;

  cogl_context = cogl_framebuffer_get_context (priv->framebuffer);

  if (format == COGL_PIXEL_FORMAT_ANY)
    {
      texture = cogl_texture_2d_new_with_size (cogl_context, width, height);
    }
  else
    {
      texture = cogl_texture_2d_new_with_format (cogl_context,
                                                 width, height, format);
    }

  cogl_texture_set_auto_mipmap (texture, FALSE);

  if (!cogl_texture_allocate (texture, error))
    return FALSE;

  framebuffer = cogl_offscreen_new_with_texture (texture);

  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (framebuffer), error))
    return FALSE;

  return g_steal_pointer (&framebuffer);
}

static CoglOffscreen *
create_offscreen_with_formats (ClutterStageView  *view,
                               CoglPixelFormat   *formats,
                               size_t             n_formats,
                               int                width,
                               int                height,
                               GError           **error)
{
  g_autoptr (GError) local_error = NULL;
  size_t i;

  for (i = 0; i < n_formats; i++)
    {
      CoglOffscreen *offscreen;

      g_clear_error (&local_error);

      offscreen = create_offscreen (view, formats[i], width, height, &local_error);
      if (offscreen)
        return offscreen;
    }

  g_propagate_error (error, g_steal_pointer (&local_error));
  return NULL;
}

static void
ensure_stage_view_offscreen (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  ClutterEncodingRequiredFormat required_format;
  CoglPixelFormat formats[10];
  size_t n_formats = 0;
  int offscreen_width, offscreen_height;
  int onscreen_width, onscreen_height;
  g_autoptr (CoglOffscreen) offscreen = NULL;
  g_autoptr (GError) local_error = NULL;

  if (priv->offscreen)
    return;

  required_format = clutter_color_state_required_format (priv->color_state);

  if (required_format <= CLUTTER_ENCODING_REQUIRED_FORMAT_UINT8)
    {
      formats[n_formats++] =
        cogl_framebuffer_get_internal_format (priv->framebuffer);
    }
  else
    {
      formats[n_formats++] = COGL_PIXEL_FORMAT_XRGB_FP_16161616;
      formats[n_formats++] = COGL_PIXEL_FORMAT_XBGR_FP_16161616;
      formats[n_formats++] = COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE;
      formats[n_formats++] = COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE;
      formats[n_formats++] = COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE;
      formats[n_formats++] = COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE;
    }

  onscreen_width = cogl_framebuffer_get_width (priv->framebuffer);
  onscreen_height = cogl_framebuffer_get_height (priv->framebuffer);

  if (mtk_monitor_transform_is_rotated (priv->transform))
    {
      offscreen_width = onscreen_height;
      offscreen_height = onscreen_width;
    }
  else
    {
      offscreen_width = onscreen_width;
      offscreen_height = onscreen_height;
    }

  offscreen = create_offscreen_with_formats (view,
                                             formats,
                                             n_formats,
                                             offscreen_width,
                                             offscreen_height,
                                             &local_error);
  if (!offscreen)
    g_error ("Failed to allocate back buffer texture: %s", local_error->message);

  g_set_object (&priv->offscreen, g_steal_pointer (&offscreen));
}

static void
ensure_stage_view_offscreen_pipeline (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (priv->offscreen);
  g_autoptr (CoglPipeline) pipeline = NULL;

  if (priv->offscreen_pipeline)
    return;

  pipeline = cogl_pipeline_new (cogl_framebuffer_get_context (framebuffer));
  cogl_pipeline_set_static_name (pipeline, "ClutterStageView (offscreen)");

  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);
  cogl_pipeline_set_layer_texture (pipeline, 0,
                                   cogl_offscreen_get_texture (priv->offscreen));
  cogl_pipeline_set_layer_wrap_mode (pipeline, 0,
                                     COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);

  if (priv->transform != MTK_MONITOR_TRANSFORM_NORMAL)
    {
      graphene_matrix_t matrix;

      clutter_stage_view_get_offscreen_transformation_matrix (view, &matrix);
      cogl_pipeline_set_layer_matrix (pipeline, 0, &matrix);
    }

  clutter_color_state_add_pipeline_transform (priv->color_state,
                                              priv->output_color_state,
                                              pipeline);

  g_set_object (&priv->offscreen_pipeline, g_steal_pointer (&pipeline));
}

static gboolean
on_ensure_offscreen_idle (gpointer data)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (data);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  ensure_stage_view_offscreen (view);
  ensure_stage_view_offscreen_pipeline (view);

  priv->ensure_offscreen_idle_id = 0;
  return G_SOURCE_REMOVE;
}

static void
clutter_stage_view_invalidate_offscreen (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->frame_clock)
    {
      clutter_stage_view_add_redraw_clip (view, NULL);
      clutter_stage_view_schedule_update (view);
    }

  if (priv->transform == MTK_MONITOR_TRANSFORM_NORMAL &&
      clutter_color_state_equals (priv->color_state, priv->output_color_state))
    {
      g_clear_object (&priv->offscreen_pipeline);
      g_clear_object (&priv->offscreen);
      g_clear_handle_id (&priv->ensure_offscreen_idle_id, g_source_remove);
      return;
    }

  g_clear_object (&priv->offscreen_pipeline);

  if (priv->ensure_offscreen_idle_id != 0)
    return;

  priv->ensure_offscreen_idle_id = g_idle_add_full (CLUTTER_PRIORITY_REDRAW - 1,
                                                    on_ensure_offscreen_idle,
                                                    view, NULL);
}

static void
set_color_state (ClutterStageView   *view,
                 ClutterColorState **dest_color_state,
                 ClutterColorState  *color_state)
{
  if (*dest_color_state == color_state)
    return;

  g_set_object (dest_color_state, color_state);

  clutter_stage_view_invalidate_offscreen (view);
}

void
clutter_stage_view_set_color_state (ClutterStageView  *view,
                                    ClutterColorState *color_state)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  set_color_state (view, &priv->color_state, color_state);
}

void
clutter_stage_view_set_output_color_state (ClutterStageView  *view,
                                           ClutterColorState *color_state)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  set_color_state (view, &priv->output_color_state, color_state);
}

static void
clutter_stage_view_ensure_color_states (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  ClutterContext *context =
    clutter_actor_get_context (CLUTTER_ACTOR (priv->stage));
  ClutterColorManager *color_manager =
    clutter_context_get_color_manager (context);
  ClutterColorState *color_state =
    clutter_color_manager_get_default_color_state (color_manager);

  if (!priv->color_state)
    set_color_state (view, &priv->color_state, color_state);

  if (!priv->output_color_state)
    set_color_state (view, &priv->output_color_state, color_state);
}

static void
paint_transformed_framebuffer (ClutterStageView *view,
                               CoglPipeline     *pipeline,
                               CoglOffscreen    *src_framebuffer,
                               CoglFramebuffer  *dst_framebuffer,
                               const MtkRegion  *redraw_clip)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  graphene_matrix_t matrix;
  unsigned int n_rectangles, i;
  int dst_width, dst_height;
  MtkRectangle view_layout;
  MtkRectangle onscreen_layout;
  float view_scale;
  float *coordinates;

  dst_width = cogl_framebuffer_get_width (dst_framebuffer);
  dst_height = cogl_framebuffer_get_height (dst_framebuffer);
  clutter_stage_view_get_layout (view, &view_layout);

  mtk_rectangle_transform (&MTK_RECTANGLE_INIT (0, 0,
                                                view_layout.width, view_layout.height),
                           priv->transform,
                           view_layout.width,
                           view_layout.height,
                           &onscreen_layout);
  view_scale = clutter_stage_view_get_scale (view);

  cogl_framebuffer_push_matrix (dst_framebuffer);

  graphene_matrix_init_translate (&matrix,
                                  &GRAPHENE_POINT3D_INIT (-dst_width / 2.0f,
                                                          -dst_height / 2.0f,
                                                          0.f));
  graphene_matrix_scale (&matrix,
                         1.0f / (dst_width / 2.0f),
                         -1.0f / (dst_height / 2.0f),
                         0.f);
  cogl_framebuffer_set_projection_matrix (dst_framebuffer, &matrix);
  cogl_framebuffer_set_viewport (dst_framebuffer,
                                 0, 0, dst_width, dst_height);

  n_rectangles = mtk_region_num_rectangles (redraw_clip);
  coordinates = g_newa (float, 2 * 4 * n_rectangles);

  for (i = 0; i < n_rectangles; i++)
    {
      MtkRectangle src_rect;
      MtkRectangle dst_rect;

      src_rect = mtk_region_get_rectangle (redraw_clip, i);
      src_rect.x -= view_layout.x;
      src_rect.y -= view_layout.y;

      mtk_rectangle_transform (&src_rect,
                               priv->transform,
                               onscreen_layout.width,
                               onscreen_layout.height,
                               &dst_rect);

      coordinates[i * 8 + 0] = (float) dst_rect.x * view_scale;
      coordinates[i * 8 + 1] = (float) dst_rect.y * view_scale;
      coordinates[i * 8 + 2] = ((float) (dst_rect.x + dst_rect.width) *
                                view_scale);
      coordinates[i * 8 + 3] = ((float) (dst_rect.y + dst_rect.height) *
                                view_scale);

      coordinates[i * 8 + 4] = (((float) dst_rect.x / (float) dst_width) *
                                view_scale);
      coordinates[i * 8 + 5] = (((float) dst_rect.y / (float) dst_height) *
                                view_scale);
      coordinates[i * 8 + 6] = ((float) (dst_rect.x + dst_rect.width) /
                                (float) dst_width) * view_scale;
      coordinates[i * 8 + 7] = ((float) (dst_rect.y + dst_rect.height) /
                                (float) dst_height) * view_scale;
    }

  cogl_framebuffer_draw_textured_rectangles (dst_framebuffer,
                                             pipeline,
                                             coordinates,
                                             n_rectangles);

  cogl_framebuffer_pop_matrix (dst_framebuffer);
}

static void
init_shadowfb (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  g_autoptr (GError) error = NULL;
  CoglPixelFormat format;
  int width;
  int height;
  CoglOffscreen *offscreen;

  format = cogl_framebuffer_get_internal_format (priv->framebuffer);
  width = cogl_framebuffer_get_width (priv->framebuffer);
  height = cogl_framebuffer_get_height (priv->framebuffer);

  offscreen = create_offscreen (view, format, width, height, &error);
  if (!offscreen)
    {
      g_warning ("Failed to create shadow framebuffer: %s", error->message);
      return;
    }

  priv->shadow.framebuffer = offscreen;
  return;
}

void
clutter_stage_view_after_paint (ClutterStageView *view,
                                MtkRegion        *redraw_clip)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_warn_if_fail (priv->ensure_offscreen_idle_id == 0);

  if (priv->offscreen)
    {
      if (priv->shadow.framebuffer)
        {
          CoglFramebuffer *shadowfb =
            COGL_FRAMEBUFFER (priv->shadow.framebuffer);

          paint_transformed_framebuffer (view,
                                         priv->offscreen_pipeline,
                                         priv->offscreen,
                                         shadowfb,
                                         redraw_clip);
        }
      else
        {
          paint_transformed_framebuffer (view,
                                         priv->offscreen_pipeline,
                                         priv->offscreen,
                                         priv->framebuffer,
                                         redraw_clip);
        }
    }
}

static void
copy_shadowfb_to_onscreen (ClutterStageView *view,
                           const MtkRegion  *swap_region)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  g_autoptr (MtkRegion) damage_region = NULL;
  int i;

  if (mtk_region_is_empty (swap_region))
    {
      MtkRectangle full_damage = {
        .width = cogl_framebuffer_get_width (priv->framebuffer),
        .height = cogl_framebuffer_get_height (priv->framebuffer),
      };
      damage_region = mtk_region_create_rectangle (&full_damage);
    }
  else
    {
      damage_region = mtk_region_copy (swap_region);
    }

  for (i = 0; i < mtk_region_num_rectangles (damage_region); i++)
    {
      CoglFramebuffer *shadowfb = COGL_FRAMEBUFFER (priv->shadow.framebuffer);
      g_autoptr (GError) error = NULL;
      MtkRectangle rect;

      rect = mtk_region_get_rectangle (damage_region, i);

      if (!cogl_blit_framebuffer (shadowfb,
                                  priv->framebuffer,
                                  rect.x, rect.y,
                                  rect.x, rect.y,
                                  rect.width, rect.height,
                                  &error))
        {
          g_warning ("Failed to blit shadow buffer: %s", error->message);
          return;
        }
    }
}

void
clutter_stage_view_before_swap_buffer (ClutterStageView *view,
                                       const MtkRegion  *swap_region)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  COGL_TRACE_BEGIN_SCOPED (BeforeSwap,
                           "Clutter::StageView::before_swap_buffer()");

  if (priv->shadow.framebuffer)
    copy_shadowfb_to_onscreen (view, swap_region);
}

float
clutter_stage_view_get_scale (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->scale;
}

typedef void (*FrontBufferCallback) (CoglFramebuffer *framebuffer,
                                     gconstpointer    user_data);

static void
clutter_stage_view_foreach_front_buffer (ClutterStageView    *view,
                                         FrontBufferCallback  callback,
                                         gconstpointer        user_data)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_warn_if_fail (priv->ensure_offscreen_idle_id == 0);

  if (priv->offscreen)
    {
      callback (COGL_FRAMEBUFFER (priv->offscreen), user_data);
    }
  else if (priv->shadow.framebuffer)
    {
      callback (COGL_FRAMEBUFFER (priv->shadow.framebuffer), user_data);
    }
  else
    {
      callback (priv->framebuffer, user_data);
    }
}

gboolean
clutter_stage_view_is_dirty_viewport (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->dirty_viewport;
}

void
clutter_stage_view_invalidate_viewport (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->dirty_viewport = TRUE;
}

static void
set_framebuffer_viewport (CoglFramebuffer *framebuffer,
                          gconstpointer    user_data)
{
  const graphene_rect_t *rect = user_data;

  cogl_framebuffer_set_viewport (framebuffer,
                                 rect->origin.x,
                                 rect->origin.y,
                                 rect->size.width,
                                 rect->size.height);
}

void
clutter_stage_view_set_viewport (ClutterStageView *view,
                                 float             x,
                                 float             y,
                                 float             width,
                                 float             height)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  graphene_rect_t rect;

  priv->dirty_viewport = FALSE;

  rect = (graphene_rect_t) {
    .origin = { .x = x, .y = y },
    .size = { .width = width, .height = height },
  };
  clutter_stage_view_foreach_front_buffer (view,
                                           set_framebuffer_viewport,
                                           &rect);
}

gboolean
clutter_stage_view_is_dirty_projection (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->dirty_projection;
}

static void
set_framebuffer_projection_matrix (CoglFramebuffer *framebuffer,
                                   gconstpointer    user_data)
{
  cogl_framebuffer_set_projection_matrix (framebuffer, user_data);
}

void
clutter_stage_view_invalidate_projection (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->dirty_projection = TRUE;
}

void
clutter_stage_view_set_projection (ClutterStageView        *view,
                                   const graphene_matrix_t *matrix)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->dirty_projection = FALSE;
  clutter_stage_view_foreach_front_buffer (view,
                                           set_framebuffer_projection_matrix,
                                           matrix);
}

void
clutter_stage_view_get_offscreen_transformation_matrix (ClutterStageView  *view,
                                                        graphene_matrix_t *matrix)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  graphene_matrix_init_identity (matrix);

  mtk_monitor_transform_transform_matrix (
    mtk_monitor_transform_invert (priv->transform), matrix);
}

static void
maybe_mark_full_redraw (ClutterStageView  *view,
                        MtkRegion        **region)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (mtk_region_num_rectangles (*region) == 1)
    {
      MtkRectangle region_extents;

      region_extents = mtk_region_get_extents (*region);
      if (mtk_rectangle_equal (&priv->layout, &region_extents))
        g_clear_pointer (region, mtk_region_unref);
    }
}

void
clutter_stage_view_add_redraw_clip (ClutterStageView   *view,
                                    const MtkRectangle *clip)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->has_redraw_clip && !priv->redraw_clip)
    return;

  if (!clip)
    {
      g_clear_pointer (&priv->redraw_clip, mtk_region_unref);
      priv->has_redraw_clip = TRUE;
      return;
    }

  if (clip->width == 0 || clip->height == 0)
    return;

  if (!priv->redraw_clip)
    {
      if (!mtk_rectangle_equal (&priv->layout, clip))
        priv->redraw_clip = mtk_region_create_rectangle (clip);
    }
  else
    {
      mtk_region_union_rectangle (priv->redraw_clip, clip);
      maybe_mark_full_redraw (view, &priv->redraw_clip);
    }

  priv->has_redraw_clip = TRUE;
}

gboolean
clutter_stage_view_has_redraw_clip (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->has_redraw_clip;
}

gboolean
clutter_stage_view_has_full_redraw_clip (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->has_redraw_clip && !priv->redraw_clip;
}

MtkRegion *
clutter_stage_view_take_accumulated_redraw_clip (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_return_val_if_fail (priv->has_redraw_clip, NULL);

  clutter_stage_view_accumulate_redraw_clip (view);

  priv->has_accumulated_redraw_clip = FALSE;
  return g_steal_pointer (&priv->accumulated_redraw_clip);
}

void
clutter_stage_view_accumulate_redraw_clip (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_return_if_fail (priv->has_redraw_clip);

  if (priv->redraw_clip && priv->accumulated_redraw_clip)
    {
      mtk_region_union (priv->accumulated_redraw_clip, priv->redraw_clip);
      maybe_mark_full_redraw (view, &priv->accumulated_redraw_clip);
    }
  else if (priv->redraw_clip && !priv->has_accumulated_redraw_clip)
    {
      priv->accumulated_redraw_clip = g_steal_pointer (&priv->redraw_clip);
    }
  else
    {
      g_clear_pointer (&priv->accumulated_redraw_clip, mtk_region_unref);
    }

  g_clear_pointer (&priv->redraw_clip, mtk_region_unref);
  priv->has_accumulated_redraw_clip = TRUE;
  priv->has_redraw_clip = FALSE;
}

void
clutter_stage_view_assign_next_scanout (ClutterStageView *view,
                                        CoglScanout      *scanout)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_set_object (&priv->next_scanout, scanout);
}

CoglScanout *
clutter_stage_view_take_scanout (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return g_steal_pointer (&priv->next_scanout);
}

/**
 * clutter_stage_view_peek_scanout: (skip)
 */
CoglScanout *
clutter_stage_view_peek_scanout (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->next_scanout;
}

void
clutter_stage_view_schedule_update (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  ClutterStageViewClass *view_class = CLUTTER_STAGE_VIEW_GET_CLASS (view);

  if (view_class->schedule_update)
    view_class->schedule_update (view);

  clutter_frame_clock_schedule_update (priv->frame_clock);
}

void
clutter_stage_view_schedule_update_now (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  clutter_frame_clock_schedule_update_now (priv->frame_clock);
}

float
clutter_stage_view_get_refresh_rate (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->refresh_rate;
}

/**
 * clutter_stage_view_get_frame_clock: (skip)
 */
ClutterFrameClock *
clutter_stage_view_get_frame_clock (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->frame_clock;
}

gboolean
clutter_stage_view_has_shadowfb (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->use_shadowfb;
}

static void
handle_frame_clock_before_frame (ClutterFrameClock *frame_clock,
                                 ClutterFrame      *frame,
                                 gpointer           user_data)
{
  ClutterStageView *view = user_data;
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  _clutter_stage_process_queued_events (priv->stage);
}

static void
begin_frame_timing_measurement (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->frame_timings.began_draw_time_us = g_get_monotonic_time ();
}

static void
end_frame_timing_measurement (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  int64_t now_us = g_get_monotonic_time ();
  int64_t draw_time_us;

  draw_time_us = now_us - priv->frame_timings.began_draw_time_us;

  priv->frame_timings.frame_count++;
  priv->frame_timings.cumulative_draw_time_us += draw_time_us;
  if (draw_time_us > priv->frame_timings.worst_draw_time_us)
    priv->frame_timings.worst_draw_time_us = draw_time_us;

  if (priv->frame_timings.frame_count && priv->frame_timings.last_print_time_us)
    {
      float time_since_last_print_s;

      time_since_last_print_s =
        (now_us - priv->frame_timings.last_print_time_us) /
        (float) G_USEC_PER_SEC;

      if (time_since_last_print_s >= 1.0f)
        {
          float avg_fps, avg_draw_time_ms, worst_draw_time_ms;

          avg_fps = priv->frame_timings.frame_count / time_since_last_print_s;

          avg_draw_time_ms =
            (priv->frame_timings.cumulative_draw_time_us / 1000.0f) /
            priv->frame_timings.frame_count;

          worst_draw_time_ms = priv->frame_timings.worst_draw_time_us / 1000.0f;

          g_print ("*** %s frame timings over %.01fs: "
                   "%.02f FPS, average: %.01fms, peak: %.01fms\n",
                   priv->name,
                   time_since_last_print_s,
                   avg_fps,
                   avg_draw_time_ms,
                   worst_draw_time_ms);

          priv->frame_timings.frame_count = 0;
          priv->frame_timings.cumulative_draw_time_us = 0;
          priv->frame_timings.worst_draw_time_us = 0;
          priv->frame_timings.last_print_time_us = now_us;
        }
    }
  else if (!priv->frame_timings.last_print_time_us)
    {
      priv->frame_timings.last_print_time_us = now_us;
    }
}

static ClutterFrameResult
handle_frame_clock_frame (ClutterFrameClock *frame_clock,
                          ClutterFrame      *frame,
                          gpointer           user_data)
{
  ClutterStageView *view = user_data;
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  ClutterStage *stage = priv->stage;
  ClutterStageWindow *stage_window = _clutter_stage_get_window (stage);
  ClutterContext *context = clutter_actor_get_context (CLUTTER_ACTOR (stage));

  if (CLUTTER_ACTOR_IN_DESTRUCTION (stage))
    return CLUTTER_FRAME_RESULT_IDLE;

  if (!clutter_actor_is_realized (CLUTTER_ACTOR (stage)) ||
      !clutter_actor_is_mapped (CLUTTER_ACTOR (stage)))
    {
      clutter_stage_frame_discarded (stage, view, frame);
      return CLUTTER_FRAME_RESULT_IDLE;
    }

  if (clutter_context_get_show_fps (context))
    begin_frame_timing_measurement (view);

  _clutter_run_repaint_functions (CLUTTER_REPAINT_FLAGS_PRE_PAINT);
  clutter_stage_emit_before_update (stage, view, frame);

  clutter_stage_maybe_relayout (CLUTTER_ACTOR (stage));

  clutter_stage_finish_layout (stage);

  _clutter_stage_window_prepare_frame (stage_window, view, frame);
  clutter_stage_emit_prepare_frame (stage, view, frame);

  if (clutter_stage_view_has_redraw_clip (view))
    {
      clutter_stage_emit_before_paint (stage, view, frame);

      _clutter_stage_window_redraw_view (stage_window, view, frame);

      clutter_frame_clock_record_flip_time (frame_clock,
                                            g_get_monotonic_time ());

      clutter_stage_emit_after_paint (stage, view, frame);

      if (clutter_context_get_show_fps (context))
        end_frame_timing_measurement (view);
    }

  _clutter_stage_window_finish_frame (stage_window, view, frame);

  if (priv->needs_update_devices)
    {
      clutter_stage_update_devices_in_view (stage, view);
      priv->needs_update_devices = FALSE;
    }

  _clutter_run_repaint_functions (CLUTTER_REPAINT_FLAGS_POST_PAINT);
  clutter_stage_after_update (stage, view, frame);

  return clutter_frame_get_result (frame);
}

static ClutterFrame *
handle_frame_clock_new_frame (ClutterFrameClock *frame_clock,
                              gpointer           user_data)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (user_data);
  ClutterStageViewClass *view_class = CLUTTER_STAGE_VIEW_GET_CLASS (view);

  if (view_class->new_frame)
    return view_class->new_frame (view);
  else
    return NULL;
}

static const ClutterFrameListenerIface frame_clock_listener_iface = {
  .before_frame = handle_frame_clock_before_frame,
  .frame = handle_frame_clock_frame,
  .new_frame = handle_frame_clock_new_frame,
};

void
clutter_stage_view_notify_presented (ClutterStageView *view,
                                     ClutterFrameInfo *frame_info)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  clutter_stage_presented (priv->stage, view, frame_info);
  clutter_frame_clock_notify_presented (priv->frame_clock, frame_info);
}

void
clutter_stage_view_notify_ready (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  clutter_frame_clock_notify_ready (priv->frame_clock);
}

static void
sanity_check_framebuffer (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  G_GNUC_UNUSED int fb_width, fb_height;

  fb_width = cogl_framebuffer_get_width (priv->framebuffer);
  fb_height = cogl_framebuffer_get_height (priv->framebuffer);

  g_warn_if_fail (fabsf (roundf (fb_width / priv->scale) -
                         fb_width / priv->scale) < FLT_EPSILON);
  g_warn_if_fail (fabsf (roundf (fb_height / priv->scale) -
                         fb_height / priv->scale) < FLT_EPSILON);
}

static void
clutter_stage_view_set_framebuffer (ClutterStageView *view,
                                    CoglFramebuffer  *framebuffer)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_warn_if_fail (!priv->framebuffer);
  if (framebuffer)
    {
      priv->framebuffer = g_object_ref (framebuffer);
      sanity_check_framebuffer (view);
    }
}

static void
clutter_stage_view_set_transform (ClutterStageView    *view,
                                  MtkMonitorTransform  transform)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->transform == transform)
    return;

  priv->transform = transform;

  clutter_stage_view_invalidate_offscreen (CLUTTER_STAGE_VIEW (view));
}

MtkMonitorTransform
clutter_stage_view_get_transform (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->transform;
}

static void
clutter_stage_view_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_STAGE:
      g_value_set_boxed (value, &priv->stage);
      break;
    case PROP_LAYOUT:
      g_value_set_boxed (value, &priv->layout);
      break;
    case PROP_FRAMEBUFFER:
      g_value_set_object (value, priv->framebuffer);
      break;
    case PROP_USE_SHADOWFB:
      g_value_set_boolean (value, priv->use_shadowfb);
      break;
    case PROP_COLOR_STATE:
      g_value_set_object (value, priv->color_state);
      break;
    case PROP_OUTPUT_COLOR_STATE:
      g_value_set_object (value, priv->output_color_state);
      break;
    case PROP_SCALE:
      g_value_set_float (value, priv->scale);
      break;
    case PROP_REFRESH_RATE:
      g_value_set_float (value, priv->refresh_rate);
      break;
    case PROP_VBLANK_DURATION_US:
      g_value_set_int64 (value, priv->vblank_duration_us);
      break;
    case PROP_TRANSFORM:
      g_value_set_uint (value, priv->transform);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
clutter_stage_view_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  MtkRectangle *layout;
  switch (prop_id)
    {
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;
    case PROP_STAGE:
      priv->stage = g_value_get_object (value);
      break;
    case PROP_LAYOUT:
      layout = g_value_get_boxed (value);
      priv->layout = *layout;
      break;
    case PROP_FRAMEBUFFER:
      clutter_stage_view_set_framebuffer (view, g_value_get_object (value));
      break;
    case PROP_USE_SHADOWFB:
      priv->use_shadowfb = g_value_get_boolean (value);
      break;
    case PROP_COLOR_STATE:
      set_color_state (view, &priv->color_state, g_value_get_object (value));
      break;
    case PROP_OUTPUT_COLOR_STATE:
      set_color_state (view,
                       &priv->output_color_state, g_value_get_object (value));
      break;
    case PROP_SCALE:
      priv->scale = g_value_get_float (value);
      break;
    case PROP_REFRESH_RATE:
      priv->refresh_rate = g_value_get_float (value);
      break;
    case PROP_VBLANK_DURATION_US:
      priv->vblank_duration_us = g_value_get_int64 (value);
      break;
    case PROP_TRANSFORM:
      clutter_stage_view_set_transform (view, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
clutter_stage_view_constructed (GObject *object)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->use_shadowfb)
    init_shadowfb (view);

  priv->frame_clock = clutter_frame_clock_new (priv->refresh_rate,
                                               priv->vblank_duration_us,
                                               priv->name,
                                               &frame_clock_listener_iface,
                                               view);

  clutter_stage_view_ensure_color_states (view);

  clutter_stage_view_add_redraw_clip (view, NULL);
  clutter_stage_view_schedule_update (view);

  G_OBJECT_CLASS (clutter_stage_view_parent_class)->constructed (object);
}

static void
clutter_stage_view_dispose (GObject *object)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_signal_emit (view, stage_view_signals[DESTROY], 0);

  g_clear_pointer (&priv->name, g_free);

  g_clear_object (&priv->shadow.framebuffer);

  g_clear_object (&priv->color_state);
  g_clear_object (&priv->offscreen);
  g_clear_object (&priv->offscreen_pipeline);
  g_clear_pointer (&priv->redraw_clip, mtk_region_unref);
  g_clear_pointer (&priv->accumulated_redraw_clip, mtk_region_unref);
  g_clear_pointer (&priv->frame_clock, clutter_frame_clock_destroy);
  g_clear_handle_id (&priv->ensure_offscreen_idle_id, g_source_remove);

  G_OBJECT_CLASS (clutter_stage_view_parent_class)->dispose (object);
}

static void
clutter_stage_view_finalize (GObject *object)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_clear_object (&priv->framebuffer);

  G_OBJECT_CLASS (clutter_stage_view_parent_class)->finalize (object);
}

static void
clutter_stage_view_init (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->dirty_viewport = TRUE;
  priv->dirty_projection = TRUE;
  priv->scale = 1.0;
  priv->refresh_rate = 60.0;
}

static void
clutter_stage_view_class_init (ClutterStageViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = clutter_stage_view_get_property;
  object_class->set_property = clutter_stage_view_set_property;
  object_class->constructed = clutter_stage_view_constructed;
  object_class->dispose = clutter_stage_view_dispose;
  object_class->finalize = clutter_stage_view_finalize;

  obj_props[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_STAGE] =
    g_param_spec_object ("stage", NULL, NULL,
                         CLUTTER_TYPE_STAGE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_LAYOUT] =
    g_param_spec_boxed ("layout", NULL, NULL,
                        MTK_TYPE_RECTANGLE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT |
                        G_PARAM_STATIC_STRINGS);

  obj_props[PROP_FRAMEBUFFER] =
    g_param_spec_object ("framebuffer", NULL, NULL,
                         COGL_TYPE_FRAMEBUFFER,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_USE_SHADOWFB] =
    g_param_spec_boolean ("use-shadowfb", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  obj_props[PROP_COLOR_STATE] =
    g_param_spec_object ("color-state", NULL, NULL,
                         CLUTTER_TYPE_COLOR_STATE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_OUTPUT_COLOR_STATE] =
    g_param_spec_object ("output-color-state", NULL, NULL,
                         CLUTTER_TYPE_COLOR_STATE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_SCALE] =
    g_param_spec_float ("scale", NULL, NULL,
                        0.5, G_MAXFLOAT, 1.0,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT |
                        G_PARAM_STATIC_STRINGS);

  obj_props[PROP_REFRESH_RATE] =
    g_param_spec_float ("refresh-rate", NULL, NULL,
                        1.0, G_MAXFLOAT, 60.0,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT |
                        G_PARAM_STATIC_STRINGS);

  obj_props[PROP_VBLANK_DURATION_US] =
    g_param_spec_int64 ("vblank-duration-us", NULL, NULL,
                        0, G_MAXINT64, 0,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  obj_props[PROP_TRANSFORM] =
    g_param_spec_uint ("transform", NULL, NULL,
                       MTK_MONITOR_TRANSFORM_NORMAL,
                       MTK_MONITOR_TRANSFORM_FLIPPED_270,
                       MTK_MONITOR_TRANSFORM_NORMAL,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);

  stage_view_signals[DESTROY] =
    g_signal_new ("destroy",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

void
clutter_stage_view_invalidate_input_devices (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->needs_update_devices = TRUE;
}

ClutterPaintFlag
clutter_stage_view_get_default_paint_flags (ClutterStageView *view)
{
  ClutterStageViewClass *view_class =
    CLUTTER_STAGE_VIEW_GET_CLASS (view);

  if (view_class->get_default_paint_flags)
    return view_class->get_default_paint_flags (view);
  else
    return CLUTTER_PAINT_FLAG_NONE;
}

/**
 * clutter_stage_view_get_color_state: (skip)
 */
ClutterColorState *
clutter_stage_view_get_color_state (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->color_state;
}

/**
 * clutter_stage_view_get_output_color_state: (skip)
 */
ClutterColorState *
clutter_stage_view_get_output_color_state (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->output_color_state;
}

const char *
clutter_stage_view_get_name (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->name;
}
