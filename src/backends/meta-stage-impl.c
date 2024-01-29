/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2007,2008,2009,2010,2011  Intel Corporation.
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

 * Authors:
 *  Matthew Allum
 *  Robert Bragg
 *  Neil Roberts
 *  Emmanuele Bassi
 */

#define COGL_DISABLE_DEPRECATION_WARNINGS

#include "config.h"

#include "backends/meta-stage-impl-private.h"

#include <stdlib.h>
#include <math.h>

#include "backends/meta-stage-view-private.h"
#include "clutter/clutter-mutter.h"
#include "cogl/cogl.h"
#include "core/util-private.h"
#include "meta/meta-backend.h"

#define MAX_STACK_RECTS 256

typedef struct _MetaStageImplPrivate
{
  MetaBackend *backend;
  int64_t global_frame_counter;
} MetaStageImplPrivate;

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaStageImpl,
                         meta_stage_impl,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (MetaStageImpl)
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_STAGE_WINDOW,
                                                clutter_stage_window_iface_init));

enum
{
  PROP_0,

  PROP_WRAPPER,
  PROP_BACKEND,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

static void
meta_stage_impl_unrealize (ClutterStageWindow *stage_window)
{
  meta_topic (META_DEBUG_BACKEND, "Unrealizing Cogl stage [%p]", stage_window);
}

static gboolean
meta_stage_impl_realize (ClutterStageWindow *stage_window)
{
  ClutterBackend *backend;

  meta_topic (META_DEBUG_BACKEND,
              "Realizing stage '%s' [%p]",
              G_OBJECT_TYPE_NAME (stage_window),
              stage_window);

  backend = clutter_get_default_backend ();

  if (backend->cogl_context == NULL)
    {
      g_warning ("Failed to realize stage: missing Cogl context");
      return FALSE;
    }

  return TRUE;
}

static int64_t
meta_stage_impl_get_frame_counter (ClutterStageWindow *stage_window)
{
  MetaStageImpl *stage_impl = META_STAGE_IMPL (stage_window);
  MetaStageImplPrivate *priv =
    meta_stage_impl_get_instance_private (stage_impl);

  return priv->global_frame_counter;
}

static void
meta_stage_impl_show (ClutterStageWindow *stage_window,
                      gboolean            do_raise)
{
  MetaStageImpl *stage_impl = META_STAGE_IMPL (stage_window);

  clutter_actor_map (CLUTTER_ACTOR (stage_impl->wrapper));
}

static void
meta_stage_impl_hide (ClutterStageWindow *stage_window)
{
  MetaStageImpl *stage_impl = META_STAGE_IMPL (stage_window);

  clutter_actor_unmap (CLUTTER_ACTOR (stage_impl->wrapper));
}

static void
meta_stage_impl_resize (ClutterStageWindow *stage_window,
                        gint                width,
                        gint                height)
{
}

static void
paint_damage_region (ClutterStageWindow *stage_window,
                     ClutterStageView   *view,
                     MtkRegion          *swap_region,
                     MtkRegion          *queued_redraw_clip)
{
  CoglFramebuffer *framebuffer = clutter_stage_view_get_framebuffer (view);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  static CoglPipeline *overlay_blue = NULL;
  CoglColor blue_color, red_color;
  MetaStageImpl *stage_impl = META_STAGE_IMPL (stage_window);
  ClutterActor *actor = CLUTTER_ACTOR (stage_impl->wrapper);
  graphene_matrix_t transform;
  int n_rects, i;

  COGL_TRACE_BEGIN_SCOPED (PaintDamageRegion,
                           "Meta::StageImpl::paint_damage_region()");

  cogl_framebuffer_push_matrix (framebuffer);
  clutter_actor_get_transform (actor, &transform);
  cogl_framebuffer_transform (framebuffer, &transform);

  /* Blue for the swap region */
  if (G_UNLIKELY (overlay_blue == NULL))
    {
      overlay_blue = cogl_pipeline_new (ctx);
      cogl_color_init_from_4f (&blue_color, 0.0, 0.0, 0.2, 0.2);
      cogl_pipeline_set_color (overlay_blue, &blue_color);
    }

  n_rects = mtk_region_num_rectangles (swap_region);
  for (i = 0; i < n_rects; i++)
    {
      MtkRectangle rect;
      float x_1, x_2, y_1, y_2;

      rect = mtk_region_get_rectangle (swap_region, i);
      x_1 = rect.x;
      x_2 = rect.x + rect.width;
      y_1 = rect.y;
      y_2 = rect.y + rect.height;

      cogl_framebuffer_draw_rectangle (framebuffer, overlay_blue, x_1, y_1, x_2, y_2);
    }

  /* Red for the clip */
  if (queued_redraw_clip)
    {
      static CoglPipeline *overlay_red = NULL;

      if (G_UNLIKELY (overlay_red == NULL))
        {
          overlay_red = cogl_pipeline_new (ctx);
          cogl_color_init_from_4f (&red_color, 0.2, 0.0, 0.0, 0.2);
          cogl_pipeline_set_color (overlay_red, &red_color);
        }

      n_rects = mtk_region_num_rectangles (queued_redraw_clip);
      for (i = 0; i < n_rects; i++)
        {
          MtkRectangle rect;
          float x_1, x_2, y_1, y_2;

          rect = mtk_region_get_rectangle (queued_redraw_clip, i);
          x_1 = rect.x;
          x_2 = rect.x + rect.width;
          y_1 = rect.y;
          y_2 = rect.y + rect.height;

          cogl_framebuffer_draw_rectangle (framebuffer, overlay_red, x_1, y_1, x_2, y_2);
        }
    }

  cogl_framebuffer_pop_matrix (framebuffer);
}

static void
queue_damage_region (ClutterStageWindow *stage_window,
                     ClutterStageView   *stage_view,
                     MtkRegion          *damage_region)
{
  int *damage, n_rects, i;
  g_autofree int *freeme = NULL;
  CoglFramebuffer *framebuffer;
  CoglOnscreen *onscreen;
  int fb_width;
  int fb_height;

  if (mtk_region_is_empty (damage_region))
    return;

  framebuffer = clutter_stage_view_get_onscreen (stage_view);
  if (!COGL_IS_ONSCREEN (framebuffer))
    return;

  onscreen = COGL_ONSCREEN (framebuffer);
  fb_width = cogl_framebuffer_get_width (framebuffer);
  fb_height = cogl_framebuffer_get_height (framebuffer);

  n_rects = mtk_region_num_rectangles (damage_region);

  if (n_rects < MAX_STACK_RECTS)
    damage = g_newa (int, n_rects * 4);
  else
    damage = freeme = g_new (int, n_rects * 4);

  for (i = 0; i < n_rects; i++)
    {
      MtkRectangle rect;

      rect = mtk_region_get_rectangle (damage_region, i);

      clutter_stage_view_transform_rect_to_onscreen (stage_view,
                                                     &rect,
                                                     fb_width,
                                                     fb_height,
                                                     &rect);

      damage[i * 4] = rect.x;
      /* y coordinate needs to be flipped for OpenGL */
      damage[i * 4 + 1] = fb_height - rect.y - rect.height;
      damage[i * 4 + 2] = rect.width;
      damage[i * 4 + 3] = rect.height;
    }

  cogl_onscreen_queue_damage_region (onscreen, damage, n_rects);
}

static void
swap_framebuffer (ClutterStageWindow *stage_window,
                  ClutterStageView   *stage_view,
                  MtkRegion          *swap_region,
                  gboolean            swap_with_damage,
                  ClutterFrame       *frame)
{
  MetaStageImpl *stage_impl = META_STAGE_IMPL (stage_window);
  MetaStageImplPrivate *priv =
    meta_stage_impl_get_instance_private (stage_impl);
  CoglFramebuffer *framebuffer = clutter_stage_view_get_onscreen (stage_view);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);

  COGL_TRACE_BEGIN_SCOPED (SwapFramebuffer, "Meta::StageImpl::swap_framebuffer()");

  clutter_stage_view_before_swap_buffer (stage_view, swap_region);

  if (COGL_IS_ONSCREEN (framebuffer))
    {
      CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
      int64_t target_presentation_time_us;
      int *damage, n_rects, i;
      CoglFrameInfo *frame_info;

      n_rects = mtk_region_num_rectangles (swap_region);
      damage = g_newa (int, n_rects * 4);
      for (i = 0; i < n_rects; i++)
        {
          MtkRectangle rect;

          rect = mtk_region_get_rectangle (swap_region, i);
          damage[i * 4] = rect.x;
          damage[i * 4 + 1] = rect.y;
          damage[i * 4 + 2] = rect.width;
          damage[i * 4 + 3] = rect.height;
        }

      frame_info =
        cogl_frame_info_new (cogl_context, priv->global_frame_counter);
      priv->global_frame_counter++;

      if (clutter_frame_get_target_presentation_time (frame,
                                                      &target_presentation_time_us))
        {
          cogl_frame_info_set_target_presentation_time (frame_info,
                                                        target_presentation_time_us);
        }

      /* push on the screen */
      if (n_rects > 0 && !swap_with_damage)
        {
          meta_topic (META_DEBUG_BACKEND,
                      "cogl_onscreen_swap_region (onscreen: %p)",
                      onscreen);

          cogl_onscreen_swap_region (onscreen,
                                     damage, n_rects,
                                     frame_info,
                                     frame);
        }
      else
        {
          meta_topic (META_DEBUG_BACKEND,
                      "cogl_onscreen_swap_buffers (onscreen: %p)",
                      onscreen);

          cogl_onscreen_swap_buffers_with_damage (onscreen,
                                                  damage, n_rects,
                                                  frame_info,
                                                  frame);
        }
    }
  else
    {
      MetaStageView *view = META_STAGE_VIEW (stage_view);

      meta_topic (META_DEBUG_BACKEND,
                  "fake offscreen swap (framebuffer: %p)",
                  framebuffer);
      meta_stage_view_perform_fake_swap (view, priv->global_frame_counter);
      priv->global_frame_counter++;
    }
}

static MtkRegion *
offset_scale_and_clamp_region (const MtkRegion *region,
                               int              offset_x,
                               int              offset_y,
                               float            scale)
{
  int n_rects, i;
  MtkRectangle *rects;
  g_autofree MtkRectangle *freeme = NULL;

  n_rects = mtk_region_num_rectangles (region);

  if (n_rects == 0)
    return mtk_region_create ();

  if (n_rects < MAX_STACK_RECTS)
    rects = g_newa (MtkRectangle, n_rects);
  else
    rects = freeme = g_new (MtkRectangle, n_rects);

  for (i = 0; i < n_rects; i++)
    {
      MtkRectangle *rect = &rects[i];
      graphene_rect_t tmp;

      *rect = mtk_region_get_rectangle (region, i);

      tmp = mtk_rectangle_to_graphene_rect (rect);
      graphene_rect_offset (&tmp, offset_x, offset_y);
      graphene_rect_scale (&tmp, scale, scale, &tmp);
      mtk_rectangle_from_graphene_rect (&tmp, MTK_ROUNDING_STRATEGY_GROW,
                                        rect);
    }

  return mtk_region_create_rectangles (rects, n_rects);
}

static MtkRegion *
scale_offset_and_clamp_region (const MtkRegion *region,
                               float            scale,
                               int              offset_x,
                               int              offset_y)
{
  int n_rects, i;
  MtkRectangle *rects;
  g_autofree MtkRectangle *freeme = NULL;

  n_rects = mtk_region_num_rectangles (region);

  if (n_rects == 0)
    return mtk_region_create ();

  if (n_rects < MAX_STACK_RECTS)
    rects = g_newa (MtkRectangle, n_rects);
  else
    rects = freeme = g_new (MtkRectangle, n_rects);

  for (i = 0; i < n_rects; i++)
    {
      MtkRectangle *rect = &rects[i];
      graphene_rect_t tmp;

      *rect = mtk_region_get_rectangle (region, i);

      tmp = mtk_rectangle_to_graphene_rect (rect);
      graphene_rect_scale (&tmp, scale, scale, &tmp);
      graphene_rect_offset (&tmp, offset_x, offset_y);
      mtk_rectangle_from_graphene_rect (&tmp,
                                        MTK_ROUNDING_STRATEGY_GROW,
                                        rect);
    }

  return mtk_region_create_rectangles (rects, n_rects);
}

static void
paint_stage (MetaStageImpl    *stage_impl,
             ClutterStageView *stage_view,
             MtkRegion        *redraw_clip,
             ClutterFrame     *frame)
{
  ClutterStage *stage = stage_impl->wrapper;

  _clutter_stage_maybe_setup_viewport (stage, stage_view);
  clutter_stage_paint_view (stage, stage_view, redraw_clip, frame);

  clutter_stage_view_after_paint (stage_view, redraw_clip);
}

static MtkRegion *
transform_swap_region_to_onscreen (ClutterStageView *stage_view,
                                   MtkRegion        *swap_region)
{
  CoglFramebuffer *onscreen = clutter_stage_view_get_onscreen (stage_view);
  int n_rects, i;
  MtkRectangle *rects;
  MtkRegion *transformed_region;
  int width, height;

  width = cogl_framebuffer_get_width (onscreen);
  height = cogl_framebuffer_get_height (onscreen);

  n_rects = mtk_region_num_rectangles (swap_region);
  rects = g_newa (MtkRectangle, n_rects);
  for (i = 0; i < n_rects; i++)
    {
      rects[i] = mtk_region_get_rectangle (swap_region, i);
      clutter_stage_view_transform_rect_to_onscreen (stage_view,
                                                     &rects[i],
                                                     width,
                                                     height,
                                                     &rects[i]);
    }
  transformed_region = mtk_region_create_rectangles (rects, n_rects);

  return transformed_region;
}

static gboolean
should_use_clipped_redraw (gboolean              is_full_redraw,
                           gboolean              has_buffer_age,
                           gboolean              buffer_has_valid_damage_history,
                           ClutterDrawDebugFlag  paint_debug_flags,
                           CoglFramebuffer      *framebuffer,
                           ClutterStageWindow   *stage_window)
{
  gboolean can_blit_sub_buffer;
  gboolean can_use_clipped_redraw;
  gboolean is_warmed_up;

  if (is_full_redraw)
    return FALSE;

  if (paint_debug_flags & CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS)
    return FALSE;

  if (COGL_IS_OFFSCREEN (framebuffer))
    return TRUE;

  if (has_buffer_age && !buffer_has_valid_damage_history)
    {
      meta_topic (META_DEBUG_BACKEND,
                  "Invalid back buffer age: forcing full redraw");
      return FALSE;
    }

  can_blit_sub_buffer =
    cogl_clutter_winsys_has_feature (COGL_WINSYS_FEATURE_SWAP_REGION);
  can_use_clipped_redraw =
    _clutter_stage_window_can_clip_redraws (stage_window) &&
    (can_blit_sub_buffer || has_buffer_age);
  /* Some drivers struggle to get going and produce some junk
   * frames when starting up... */
  is_warmed_up =
    cogl_onscreen_get_frame_counter (COGL_ONSCREEN (framebuffer)) > 3;
  return is_warmed_up && can_use_clipped_redraw;
}

static void
meta_stage_impl_redraw_view_primary (MetaStageImpl    *stage_impl,
                                     ClutterStageView *stage_view,
                                     ClutterFrame     *frame)
{
  ClutterStageWindow *stage_window = CLUTTER_STAGE_WINDOW (stage_impl);
  MetaStageView *view = META_STAGE_VIEW (stage_view);
  CoglFramebuffer *fb = clutter_stage_view_get_framebuffer (stage_view);
  CoglFramebuffer *onscreen = clutter_stage_view_get_onscreen (stage_view);
  MtkRectangle view_rect;
  gboolean is_full_redraw;
  gboolean use_clipped_redraw;
  gboolean buffer_has_valid_damage_history = FALSE;
  gboolean has_buffer_age;
  gboolean swap_with_damage;
  g_autoptr (MtkRegion) redraw_clip = NULL;
  g_autoptr (MtkRegion) queued_redraw_clip = NULL;
  g_autoptr (MtkRegion) fb_clip_region = NULL;
  g_autoptr (MtkRegion) swap_region = NULL;
  ClutterDrawDebugFlag paint_debug_flags;
  ClutterDamageHistory *damage_history;
  float fb_scale;
  int fb_width, fb_height;
  int buffer_age = 0;

  COGL_TRACE_BEGIN_SCOPED (RedrawViewPrimary,
                           "Meta::StageImpl::redraw_view_primary()");

  clutter_stage_view_get_layout (stage_view, &view_rect);
  fb_scale = clutter_stage_view_get_scale (stage_view);
  fb_width = cogl_framebuffer_get_width (fb);
  fb_height = cogl_framebuffer_get_height (fb);

  has_buffer_age =
    COGL_IS_ONSCREEN (onscreen) &&
    cogl_clutter_winsys_has_feature (COGL_WINSYS_FEATURE_BUFFER_AGE);

  redraw_clip = clutter_stage_view_take_accumulated_redraw_clip (stage_view);

  /* NB: a NULL redraw clip == full stage redraw */
  if (!redraw_clip)
    is_full_redraw = TRUE;
  else
    is_full_redraw = FALSE;

  damage_history = meta_stage_view_get_damage_history (view);

  if (has_buffer_age)
    {
      buffer_age = cogl_onscreen_get_buffer_age (COGL_ONSCREEN (onscreen));
      buffer_has_valid_damage_history =
        clutter_damage_history_is_age_valid (damage_history,
                                             buffer_age);
    }

  meta_get_clutter_debug_flags (NULL, &paint_debug_flags, NULL);

  use_clipped_redraw =
    should_use_clipped_redraw (is_full_redraw,
                               has_buffer_age,
                               buffer_has_valid_damage_history,
                               paint_debug_flags,
                               onscreen,
                               stage_window);

  if (use_clipped_redraw)
    {
      fb_clip_region = offset_scale_and_clamp_region (redraw_clip,
                                                      -view_rect.x,
                                                      -view_rect.y,
                                                      fb_scale);

      if (G_UNLIKELY (paint_debug_flags & CLUTTER_DEBUG_PAINT_DAMAGE_REGION))
        {
          queued_redraw_clip =
            scale_offset_and_clamp_region (fb_clip_region,
                                           1.0 / fb_scale,
                                           view_rect.x,
                                           view_rect.y);
        }
    }
  else
    {
      MtkRectangle fb_rect;

      fb_rect = (MtkRectangle) {
        .width = fb_width,
        .height = fb_height,
      };
      fb_clip_region = mtk_region_create_rectangle (&fb_rect);

      g_clear_pointer (&redraw_clip, mtk_region_unref);
      redraw_clip = mtk_region_create_rectangle (&view_rect);

      if (G_UNLIKELY (paint_debug_flags & CLUTTER_DEBUG_PAINT_DAMAGE_REGION))
        queued_redraw_clip = mtk_region_ref (redraw_clip);
    }

  g_return_if_fail (!mtk_region_is_empty (fb_clip_region));

  /* XXX: It seems there will be a race here in that the stage
   * window may be resized before the cogl_onscreen_swap_region
   * is handled and so we may copy the wrong region. I can't
   * really see how we can handle this with the current state of X
   * but at least in this case a full redraw should be queued by
   * the resize anyway so it should only exhibit temporary
   * artefacts.
   */
  /* swap_region does not need damage history, set it up before that */
  if (!use_clipped_redraw)
    swap_region = mtk_region_create ();
  else if (clutter_stage_view_has_shadowfb (stage_view))
    swap_region = mtk_region_ref (fb_clip_region);
  else
    swap_region = mtk_region_copy (fb_clip_region);

  swap_with_damage = FALSE;
  if (has_buffer_age)
    {
      clutter_damage_history_record (damage_history, fb_clip_region);

      if (use_clipped_redraw)
        {
          int age;

          for (age = 1; age <= buffer_age; age++)
            {
              const MtkRegion *old_damage;

              old_damage =
                clutter_damage_history_lookup (damage_history, age);
              mtk_region_union (fb_clip_region, old_damage);
            }

          meta_topic (META_DEBUG_BACKEND,
                      "Reusing back buffer(age=%d) - repairing region: num rects: %d",
                      buffer_age,
                      mtk_region_num_rectangles (fb_clip_region));

          swap_with_damage = TRUE;
        }

      clutter_damage_history_step (damage_history);
    }

  if (use_clipped_redraw)
    {
      /* Regenerate redraw_clip because:
       *  1. It's missing the regions added from damage_history above; and
       *  2. If using fractional scaling then it might be a fraction of a
       *     logical pixel (or one physical pixel) smaller than
       *     fb_clip_region, due to the clamping from
       *     offset_scale_and_clamp_region. So we need to ensure redraw_clip
       *     is a superset of fb_clip_region to avoid such gaps.
       */
      g_clear_pointer (&redraw_clip, mtk_region_unref);
      redraw_clip = scale_offset_and_clamp_region (fb_clip_region,
                                                   1.0 / fb_scale,
                                                   view_rect.x,
                                                   view_rect.y);
    }

  if (paint_debug_flags & CLUTTER_DEBUG_PAINT_DAMAGE_REGION)
    {
      g_autoptr (MtkRegion) debug_redraw_clip = NULL;

      debug_redraw_clip = mtk_region_create_rectangle (&view_rect);
      paint_stage (stage_impl, stage_view, debug_redraw_clip, frame);
    }
  else if (use_clipped_redraw)
    {
      queue_damage_region (stage_window, stage_view, fb_clip_region);

      cogl_framebuffer_push_region_clip (fb, fb_clip_region);

      paint_stage (stage_impl, stage_view, redraw_clip, frame);

      cogl_framebuffer_pop_clip (fb);
    }
  else
    {
      meta_topic (META_DEBUG_BACKEND, "Unclipped stage paint");

      paint_stage (stage_impl, stage_view, redraw_clip, frame);
    }

  g_clear_pointer (&redraw_clip, mtk_region_unref);
  g_clear_pointer (&fb_clip_region, mtk_region_unref);

  if (queued_redraw_clip)
    {
      g_autoptr (MtkRegion) swap_region_in_stage_space = NULL;

      swap_region_in_stage_space =
        scale_offset_and_clamp_region (swap_region,
                                       1.0f / fb_scale,
                                       view_rect.x,
                                       view_rect.y);

      mtk_region_subtract (swap_region_in_stage_space, queued_redraw_clip);

      paint_damage_region (stage_window, stage_view,
                           swap_region_in_stage_space, queued_redraw_clip);
    }

  if (clutter_stage_view_get_onscreen (stage_view) !=
      clutter_stage_view_get_framebuffer (stage_view) &&
      mtk_region_num_rectangles (swap_region) != 0)
    {
      g_autoptr (MtkRegion) transformed_swap_region = NULL;

      transformed_swap_region =
        transform_swap_region_to_onscreen (stage_view, swap_region);
      g_clear_pointer (&swap_region, mtk_region_unref);
      swap_region = g_steal_pointer (&transformed_swap_region);
    }

  swap_framebuffer (stage_window,
                    stage_view,
                    swap_region,
                    swap_with_damage,
                    frame);
}

static gboolean
meta_stage_impl_scanout_view (MetaStageImpl     *stage_impl,
                              ClutterStageView  *stage_view,
                              CoglScanout       *scanout,
                              ClutterFrame      *frame,
                              GError           **error)
{
  MetaStageImplPrivate *priv =
    meta_stage_impl_get_instance_private (stage_impl);
  CoglFramebuffer *framebuffer =
    clutter_stage_view_get_onscreen (stage_view);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglOnscreen *onscreen;
  CoglFrameInfo *frame_info;

  g_assert (COGL_IS_ONSCREEN (framebuffer));

  onscreen = COGL_ONSCREEN (framebuffer);

  frame_info = cogl_frame_info_new (cogl_context, priv->global_frame_counter);

  if (!cogl_onscreen_direct_scanout (onscreen,
                                     scanout,
                                     frame_info,
                                     frame,
                                     error))
    {
      g_object_unref (frame_info);
      return FALSE;
    }

  priv->global_frame_counter++;

  return TRUE;
}

static void
meta_stage_impl_redraw_view (ClutterStageWindow *stage_window,
                             ClutterStageView   *stage_view,
                             ClutterFrame       *frame)
{
  MetaStageImpl *stage_impl = META_STAGE_IMPL (stage_window);
  g_autoptr (CoglScanout) scanout = NULL;

  scanout = clutter_stage_view_take_scanout (stage_view);
  if (scanout)
    {
      g_autoptr (GError) error = NULL;

      if (meta_stage_impl_scanout_view (stage_impl,
                                        stage_view,
                                        scanout,
                                        frame,
                                        &error))
        {
          clutter_stage_view_accumulate_redraw_clip (stage_view);
          return;
        }

      if (!g_error_matches (error,
                            COGL_SCANOUT_ERROR,
                            COGL_SCANOUT_ERROR_INHIBITED))
        g_warning ("Failed to scan out client buffer: %s", error->message);
    }

  meta_stage_impl_redraw_view_primary (stage_impl, stage_view, frame);
}

void
meta_stage_impl_add_onscreen_frame_info (MetaStageImpl    *stage_impl,
                                         ClutterStageView *stage_view)
{
  MetaStageImplPrivate *priv =
    meta_stage_impl_get_instance_private (stage_impl);
  CoglFramebuffer *framebuffer = clutter_stage_view_get_onscreen (stage_view);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglFrameInfo *frame_info;

  frame_info = cogl_frame_info_new (cogl_context, priv->global_frame_counter);
  priv->global_frame_counter++;

  cogl_onscreen_add_frame_info (COGL_ONSCREEN (framebuffer), frame_info);
}

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface)
{
  iface->realize = meta_stage_impl_realize;
  iface->unrealize = meta_stage_impl_unrealize;
  iface->resize = meta_stage_impl_resize;
  iface->show = meta_stage_impl_show;
  iface->hide = meta_stage_impl_hide;
  iface->get_frame_counter = meta_stage_impl_get_frame_counter;
  iface->redraw_view = meta_stage_impl_redraw_view;
}

static void
meta_stage_impl_set_property (GObject      *gobject,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  MetaStageImpl *self = META_STAGE_IMPL (gobject);
  MetaStageImplPrivate *priv = meta_stage_impl_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_WRAPPER:
      self->wrapper = g_value_get_object (value);
      break;

    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
meta_stage_impl_class_init (MetaStageImplClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_stage_impl_set_property;

  obj_props[PROP_WRAPPER] =
    g_param_spec_object ("wrapper", NULL, NULL,
                         CLUTTER_TYPE_STAGE,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_stage_impl_init (MetaStageImpl *stage)
{
}

MetaBackend *
meta_stage_impl_get_backend (MetaStageImpl *stage_impl)
{
  MetaStageImplPrivate *priv =
    meta_stage_impl_get_instance_private (stage_impl);

  return priv->backend;
}
