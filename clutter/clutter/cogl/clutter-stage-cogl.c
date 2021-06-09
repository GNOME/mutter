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

#include "clutter-build-config.h"

#include "clutter-config.h"

#include "clutter-stage-cogl.h"

#include <stdlib.h>
#include <math.h>

#include "clutter-actor-private.h"
#include "clutter-backend-private.h"
#include "clutter-damage-history.h"
#include "clutter-debug.h"
#include "clutter-event.h"
#include "clutter-enum-types.h"
#include "clutter-feature.h"
#include "clutter-frame.h"
#include "clutter-main.h"
#include "clutter-private.h"
#include "clutter-stage-private.h"
#include "clutter-stage-view-private.h"
#include "cogl.h"

#define MAX_STACK_RECTS 256

typedef struct _ClutterStageViewCoglPrivate
{
  /* Damage history, in stage view render target framebuffer coordinate space.
   */
  ClutterDamageHistory *damage_history;

  guint notify_presented_handle_id;

  CoglFrameClosure *frame_cb_closure;
} ClutterStageViewCoglPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterStageViewCogl, clutter_stage_view_cogl,
                            CLUTTER_TYPE_STAGE_VIEW)

typedef struct _ClutterStageCoglPrivate
{
  int64_t global_frame_counter;
} ClutterStageCoglPrivate;

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterStageCogl,
                         _clutter_stage_cogl,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (ClutterStageCogl)
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_STAGE_WINDOW,
                                                clutter_stage_window_iface_init));

enum
{
  PROP_0,
  PROP_WRAPPER,
  PROP_BACKEND,
  PROP_LAST
};

static void
clutter_stage_cogl_unrealize (ClutterStageWindow *stage_window)
{
  CLUTTER_NOTE (BACKEND, "Unrealizing Cogl stage [%p]", stage_window);
}

static gboolean
clutter_stage_cogl_realize (ClutterStageWindow *stage_window)
{
  ClutterBackend *backend;

  CLUTTER_NOTE (BACKEND, "Realizing stage '%s' [%p]",
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
clutter_stage_cogl_get_frame_counter (ClutterStageWindow *stage_window)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);
  ClutterStageCoglPrivate *priv =
    _clutter_stage_cogl_get_instance_private (stage_cogl);

  return priv->global_frame_counter;
}

static ClutterActor *
clutter_stage_cogl_get_wrapper (ClutterStageWindow *stage_window)
{
  return CLUTTER_ACTOR (CLUTTER_STAGE_COGL (stage_window)->wrapper);
}

static void
clutter_stage_cogl_show (ClutterStageWindow *stage_window,
			 gboolean            do_raise)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);

  clutter_actor_map (CLUTTER_ACTOR (stage_cogl->wrapper));
}

static void
clutter_stage_cogl_hide (ClutterStageWindow *stage_window)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);

  clutter_actor_unmap (CLUTTER_ACTOR (stage_cogl->wrapper));
}

static void
clutter_stage_cogl_resize (ClutterStageWindow *stage_window,
                           gint                width,
                           gint                height)
{
}

static void
paint_damage_region (ClutterStageWindow *stage_window,
                     ClutterStageView   *view,
                     cairo_region_t     *swap_region,
                     cairo_region_t     *queued_redraw_clip)
{
  CoglFramebuffer *framebuffer = clutter_stage_view_get_framebuffer (view);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  static CoglPipeline *overlay_blue = NULL;
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);
  ClutterActor *actor = CLUTTER_ACTOR (stage_cogl->wrapper);
  graphene_matrix_t transform;
  int n_rects, i;

  cogl_framebuffer_push_matrix (framebuffer);
  clutter_actor_get_transform (actor, &transform);
  cogl_framebuffer_transform (framebuffer, &transform);

  /* Blue for the swap region */
  if (G_UNLIKELY (overlay_blue == NULL))
    {
      overlay_blue = cogl_pipeline_new (ctx);
      cogl_pipeline_set_color4ub (overlay_blue, 0x00, 0x00, 0x33, 0x33);
    }

  n_rects = cairo_region_num_rectangles (swap_region);
  for (i = 0; i < n_rects; i++)
    {
      cairo_rectangle_int_t rect;
      float x_1, x_2, y_1, y_2;

      cairo_region_get_rectangle (swap_region, i, &rect);
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
          cogl_pipeline_set_color4ub (overlay_red, 0x33, 0x00, 0x00, 0x33);
        }

      n_rects = cairo_region_num_rectangles (queued_redraw_clip);
      for (i = 0; i < n_rects; i++)
        {
          cairo_rectangle_int_t rect;
          float x_1, x_2, y_1, y_2;

          cairo_region_get_rectangle (queued_redraw_clip, i, &rect);
          x_1 = rect.x;
          x_2 = rect.x + rect.width;
          y_1 = rect.y;
          y_2 = rect.y + rect.height;

          cogl_framebuffer_draw_rectangle (framebuffer, overlay_red, x_1, y_1, x_2, y_2);
        }
    }

  cogl_framebuffer_pop_matrix (framebuffer);
}

typedef struct _NotifyPresentedClosure
{
  ClutterStageView *view;
  ClutterFrameInfo frame_info;
} NotifyPresentedClosure;

static gboolean
notify_presented_idle (gpointer user_data)
{
  NotifyPresentedClosure *closure = user_data;
  ClutterStageViewCogl *view_cogl = CLUTTER_STAGE_VIEW_COGL (closure->view);
  ClutterStageViewCoglPrivate *view_priv =
    clutter_stage_view_cogl_get_instance_private (view_cogl);

  view_priv->notify_presented_handle_id = 0;
  clutter_stage_view_notify_presented (closure->view, &closure->frame_info);

  return G_SOURCE_REMOVE;
}

static void
swap_framebuffer (ClutterStageWindow *stage_window,
                  ClutterStageView   *view,
                  cairo_region_t     *swap_region,
                  gboolean            swap_with_damage,
                  ClutterFrame       *frame)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);
  ClutterStageCoglPrivate *priv =
    _clutter_stage_cogl_get_instance_private (stage_cogl);
  CoglFramebuffer *framebuffer = clutter_stage_view_get_onscreen (view);

  clutter_stage_view_before_swap_buffer (view, swap_region);

  if (COGL_IS_ONSCREEN (framebuffer))
    {
      CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
      int *damage, n_rects, i;
      CoglFrameInfo *frame_info;

      n_rects = cairo_region_num_rectangles (swap_region);
      damage = g_newa (int, n_rects * 4);
      for (i = 0; i < n_rects; i++)
        {
          cairo_rectangle_int_t rect;

          cairo_region_get_rectangle (swap_region, i, &rect);
          damage[i * 4] = rect.x;
          damage[i * 4 + 1] = rect.y;
          damage[i * 4 + 2] = rect.width;
          damage[i * 4 + 3] = rect.height;
        }

      frame_info = cogl_frame_info_new (priv->global_frame_counter);
      priv->global_frame_counter++;

      /* push on the screen */
      if (n_rects > 0 && !swap_with_damage)
        {
          CLUTTER_NOTE (BACKEND,
                        "cogl_onscreen_swap_region (onscreen: %p)",
                        onscreen);

          cogl_onscreen_swap_region (onscreen,
                                     damage, n_rects,
                                     frame_info,
                                     frame);
        }
      else
        {
          CLUTTER_NOTE (BACKEND, "cogl_onscreen_swap_buffers (onscreen: %p)",
                        onscreen);

          cogl_onscreen_swap_buffers_with_damage (onscreen,
                                                  damage, n_rects,
                                                  frame_info,
                                                  frame);
        }
    }
  else
    {
      ClutterStageViewCogl *view_cogl = CLUTTER_STAGE_VIEW_COGL (view);
      ClutterStageViewCoglPrivate *view_priv =
        clutter_stage_view_cogl_get_instance_private (view_cogl);
      NotifyPresentedClosure *closure;

      CLUTTER_NOTE (BACKEND, "fake offscreen swap (framebuffer: %p)",
                    framebuffer);

      closure = g_new0 (NotifyPresentedClosure, 1);
      closure->view = view;
      closure->frame_info = (ClutterFrameInfo) {
        .frame_counter = priv->global_frame_counter,
        .refresh_rate = clutter_stage_view_get_refresh_rate (view),
        .presentation_time = g_get_monotonic_time (),
        .flags = CLUTTER_FRAME_INFO_FLAG_NONE,
        .sequence = 0,
      };
      priv->global_frame_counter++;

      g_warn_if_fail (view_priv->notify_presented_handle_id == 0);
      view_priv->notify_presented_handle_id =
        g_idle_add_full (G_PRIORITY_DEFAULT,
                         notify_presented_idle,
                         closure, g_free);
    }
}

static cairo_region_t *
offset_scale_and_clamp_region (const cairo_region_t *region,
                               int                   offset_x,
                               int                   offset_y,
                               float                 scale)
{
  int n_rects, i;
  cairo_rectangle_int_t *rects;
  g_autofree cairo_rectangle_int_t *freeme = NULL;

  n_rects = cairo_region_num_rectangles (region);

  if (n_rects == 0)
    return cairo_region_create ();

  if (n_rects < MAX_STACK_RECTS)
    rects = g_newa (cairo_rectangle_int_t, n_rects);
  else
    rects = freeme = g_new (cairo_rectangle_int_t, n_rects);

  for (i = 0; i < n_rects; i++)
    {
      cairo_rectangle_int_t *rect = &rects[i];
      graphene_rect_t tmp;

      cairo_region_get_rectangle (region, i, rect);

      _clutter_util_rect_from_rectangle (rect, &tmp);
      graphene_rect_offset (&tmp, offset_x, offset_y);
      graphene_rect_scale (&tmp, scale, scale, &tmp);
      _clutter_util_rectangle_int_extents (&tmp, rect);
    }

  return cairo_region_create_rectangles (rects, n_rects);
}

static cairo_region_t *
scale_offset_and_clamp_region (const cairo_region_t *region,
                               float                 scale,
                               int                   offset_x,
                               int                   offset_y)
{
  int n_rects, i;
  cairo_rectangle_int_t *rects;
  g_autofree cairo_rectangle_int_t *freeme = NULL;

  n_rects = cairo_region_num_rectangles (region);

  if (n_rects == 0)
    return cairo_region_create ();

  if (n_rects < MAX_STACK_RECTS)
    rects = g_newa (cairo_rectangle_int_t, n_rects);
  else
    rects = freeme = g_new (cairo_rectangle_int_t, n_rects);

  for (i = 0; i < n_rects; i++)
    {
      cairo_rectangle_int_t *rect = &rects[i];
      graphene_rect_t tmp;

      cairo_region_get_rectangle (region, i, rect);

      _clutter_util_rect_from_rectangle (rect, &tmp);
      graphene_rect_scale (&tmp, scale, scale, &tmp);
      graphene_rect_offset (&tmp, offset_x, offset_y);
      _clutter_util_rectangle_int_extents (&tmp, rect);
    }

  return cairo_region_create_rectangles (rects, n_rects);
}

static void
paint_stage (ClutterStageCogl *stage_cogl,
             ClutterStageView *view,
             cairo_region_t   *redraw_clip)
{
  ClutterStage *stage = stage_cogl->wrapper;

  _clutter_stage_maybe_setup_viewport (stage, view);
  clutter_stage_paint_view (stage, view, redraw_clip);

  clutter_stage_view_after_paint (view, redraw_clip);
}

static cairo_region_t *
transform_swap_region_to_onscreen (ClutterStageView *view,
                                   cairo_region_t   *swap_region)
{
  CoglFramebuffer *onscreen = clutter_stage_view_get_onscreen (view);
  int n_rects, i;
  cairo_rectangle_int_t *rects;
  cairo_region_t *transformed_region;
  int width, height;

  width = cogl_framebuffer_get_width (onscreen);
  height = cogl_framebuffer_get_height (onscreen);

  n_rects = cairo_region_num_rectangles (swap_region);
  rects = g_newa (cairo_rectangle_int_t, n_rects);
  for (i = 0; i < n_rects; i++)
    {
      cairo_region_get_rectangle (swap_region, i, &rects[i]);
      clutter_stage_view_transform_rect_to_onscreen (view,
                                                     &rects[i],
                                                     width,
                                                     height,
                                                     &rects[i]);
    }
  transformed_region = cairo_region_create_rectangles (rects, n_rects);

  return transformed_region;
}

static void
clutter_stage_cogl_redraw_view_primary (ClutterStageCogl *stage_cogl,
                                        ClutterStageView *view,
                                        ClutterFrame     *frame)
{
  ClutterStageWindow *stage_window = CLUTTER_STAGE_WINDOW (stage_cogl);
  ClutterStageViewCogl *view_cogl = CLUTTER_STAGE_VIEW_COGL (view);
  ClutterStageViewCoglPrivate *view_priv =
    clutter_stage_view_cogl_get_instance_private (view_cogl);
  CoglFramebuffer *fb = clutter_stage_view_get_framebuffer (view);
  CoglFramebuffer *onscreen = clutter_stage_view_get_onscreen (view);
  cairo_rectangle_int_t view_rect;
  gboolean is_full_redraw;
  gboolean use_clipped_redraw = TRUE;
  gboolean can_blit_sub_buffer;
  gboolean has_buffer_age;
  gboolean swap_with_damage;
  cairo_region_t *redraw_clip;
  cairo_region_t *queued_redraw_clip = NULL;
  cairo_region_t *fb_clip_region;
  cairo_region_t *swap_region;
  float fb_scale;
  int fb_width, fb_height;
  int buffer_age = 0;

  clutter_stage_view_get_layout (view, &view_rect);
  fb_scale = clutter_stage_view_get_scale (view);
  fb_width = cogl_framebuffer_get_width (fb);
  fb_height = cogl_framebuffer_get_height (fb);

  can_blit_sub_buffer =
    COGL_IS_ONSCREEN (onscreen) &&
    cogl_clutter_winsys_has_feature (COGL_WINSYS_FEATURE_SWAP_REGION);

  has_buffer_age =
    COGL_IS_ONSCREEN (onscreen) &&
    cogl_clutter_winsys_has_feature (COGL_WINSYS_FEATURE_BUFFER_AGE);

  redraw_clip = clutter_stage_view_take_redraw_clip (view);

  /* NB: a NULL redraw clip == full stage redraw */
  if (!redraw_clip)
    is_full_redraw = TRUE;
  else
    is_full_redraw = FALSE;

  if (has_buffer_age)
    {
      buffer_age = cogl_onscreen_get_buffer_age (COGL_ONSCREEN (onscreen));
      if (!clutter_damage_history_is_age_valid (view_priv->damage_history,
                                                buffer_age))
        {
          CLUTTER_NOTE (CLIPPING,
                        "Invalid back buffer(age=%d): forcing full redraw\n",
                        buffer_age);
          use_clipped_redraw = FALSE;
        }
    }

  use_clipped_redraw =
    use_clipped_redraw &&
    !(clutter_paint_debug_flags & CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS) &&
    _clutter_stage_window_can_clip_redraws (stage_window) &&
    (can_blit_sub_buffer || has_buffer_age) &&
    !is_full_redraw &&
    /* some drivers struggle to get going and produce some junk
     * frames when starting up... */
    cogl_onscreen_get_frame_counter (COGL_ONSCREEN (onscreen)) > 3;

  if (use_clipped_redraw)
    {
      fb_clip_region = offset_scale_and_clamp_region (redraw_clip,
                                                      -view_rect.x,
                                                      -view_rect.y,
                                                      fb_scale);

      if (G_UNLIKELY (clutter_paint_debug_flags &
                      CLUTTER_DEBUG_PAINT_DAMAGE_REGION))
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
      cairo_rectangle_int_t fb_rect;

      fb_rect = (cairo_rectangle_int_t) {
        .width = fb_width,
        .height = fb_height,
      };
      fb_clip_region = cairo_region_create_rectangle (&fb_rect);

      g_clear_pointer (&redraw_clip, cairo_region_destroy);
      redraw_clip = cairo_region_create_rectangle (&view_rect);

      if (G_UNLIKELY (clutter_paint_debug_flags &
                      CLUTTER_DEBUG_PAINT_DAMAGE_REGION))
        queued_redraw_clip = cairo_region_reference (redraw_clip);
    }

  g_return_if_fail (!cairo_region_is_empty (fb_clip_region));

  swap_with_damage = FALSE;
  if (has_buffer_age)
    {
      clutter_damage_history_record (view_priv->damage_history,
                                     fb_clip_region);

      if (use_clipped_redraw)
        {
          int age;

          for (age = 1; age <= buffer_age; age++)
            {
              const cairo_region_t *old_damage;

              old_damage =
                clutter_damage_history_lookup (view_priv->damage_history, age);
              cairo_region_union (fb_clip_region, old_damage);
            }

          CLUTTER_NOTE (CLIPPING, "Reusing back buffer(age=%d) - repairing region: num rects: %d\n",
                        buffer_age,
                        cairo_region_num_rectangles (fb_clip_region));

          swap_with_damage = TRUE;
        }

      clutter_damage_history_step (view_priv->damage_history);
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
      cairo_region_destroy (redraw_clip);
      redraw_clip = scale_offset_and_clamp_region (fb_clip_region,
                                                   1.0 / fb_scale,
                                                   view_rect.x,
                                                   view_rect.y);
    }

  if (clutter_paint_debug_flags & CLUTTER_DEBUG_PAINT_DAMAGE_REGION)
    {
      cairo_region_t *debug_redraw_clip;

      debug_redraw_clip = cairo_region_create_rectangle (&view_rect);
      paint_stage (stage_cogl, view, debug_redraw_clip);
      cairo_region_destroy (debug_redraw_clip);
    }
  else if (use_clipped_redraw)
    {
      cogl_framebuffer_push_region_clip (fb, fb_clip_region);

      paint_stage (stage_cogl, view, redraw_clip);

      cogl_framebuffer_pop_clip (fb);
    }
  else
    {
      CLUTTER_NOTE (CLIPPING, "Unclipped stage paint\n");

      paint_stage (stage_cogl, view, redraw_clip);
    }

  /* XXX: It seems there will be a race here in that the stage
   * window may be resized before the cogl_onscreen_swap_region
   * is handled and so we may copy the wrong region. I can't
   * really see how we can handle this with the current state of X
   * but at least in this case a full redraw should be queued by
   * the resize anyway so it should only exhibit temporary
   * artefacts.
   */
  if (use_clipped_redraw)
    swap_region = cairo_region_reference (fb_clip_region);
  else
    swap_region = cairo_region_create ();

  g_clear_pointer (&redraw_clip, cairo_region_destroy);
  g_clear_pointer (&fb_clip_region, cairo_region_destroy);

  COGL_TRACE_BEGIN_SCOPED (ClutterStageCoglRedrawViewSwapFramebuffer,
                           "Paint (swap framebuffer)");

  if (clutter_stage_view_get_onscreen (view) !=
      clutter_stage_view_get_framebuffer (view))
    {
      cairo_region_t *transformed_swap_region;

      transformed_swap_region =
        transform_swap_region_to_onscreen (view, swap_region);
      cairo_region_destroy (swap_region);
      swap_region = transformed_swap_region;
    }

  if (queued_redraw_clip)
    {
      cairo_region_t *swap_region_in_stage_space;

      swap_region_in_stage_space =
        scale_offset_and_clamp_region (swap_region,
                                       1.0f / fb_scale,
                                       view_rect.x,
                                       view_rect.y);

      cairo_region_subtract (swap_region_in_stage_space, queued_redraw_clip);

      paint_damage_region (stage_window, view,
                           swap_region_in_stage_space, queued_redraw_clip);

      cairo_region_destroy (queued_redraw_clip);
      cairo_region_destroy (swap_region_in_stage_space);
    }

  swap_framebuffer (stage_window,
                    view,
                    swap_region,
                    swap_with_damage,
                    frame);

  cairo_region_destroy (swap_region);
}

static gboolean
clutter_stage_cogl_scanout_view (ClutterStageCogl  *stage_cogl,
                                 ClutterStageView  *view,
                                 CoglScanout       *scanout,
                                 ClutterFrame      *frame,
                                 GError           **error)
{
  ClutterStageCoglPrivate *priv =
    _clutter_stage_cogl_get_instance_private (stage_cogl);
  CoglFramebuffer *framebuffer = clutter_stage_view_get_framebuffer (view);
  CoglOnscreen *onscreen;
  CoglFrameInfo *frame_info;

  g_assert (COGL_IS_ONSCREEN (framebuffer));

  onscreen = COGL_ONSCREEN (framebuffer);

  frame_info = cogl_frame_info_new (priv->global_frame_counter);

  if (!cogl_onscreen_direct_scanout (onscreen,
                                     scanout,
                                     frame_info,
                                     frame,
                                     error))
    {
      cogl_object_unref (frame_info);
      return FALSE;
    }

  priv->global_frame_counter++;

  return TRUE;
}

static void
clutter_stage_cogl_redraw_view (ClutterStageWindow *stage_window,
                                ClutterStageView   *view,
                                ClutterFrame       *frame)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);
  g_autoptr (CoglScanout) scanout = NULL;

  scanout = clutter_stage_view_take_scanout (view);
  if (scanout)
    {
      g_autoptr (GError) error = NULL;

      if (clutter_stage_cogl_scanout_view (stage_cogl,
                                           view,
                                           scanout,
                                           frame,
                                           &error))
        return;

      if (!g_error_matches (error,
                            COGL_SCANOUT_ERROR,
                            COGL_SCANOUT_ERROR_INHIBITED))
        g_warning ("Failed to scan out client buffer: %s", error->message);
    }

  clutter_stage_cogl_redraw_view_primary (stage_cogl, view, frame);
}

void
clutter_stage_cogl_add_onscreen_frame_info (ClutterStageCogl *stage_cogl,
                                            ClutterStageView *view)
{
  ClutterStageCoglPrivate *priv =
    _clutter_stage_cogl_get_instance_private (stage_cogl);
  CoglFramebuffer *framebuffer = clutter_stage_view_get_onscreen (view);
  CoglFrameInfo *frame_info;

  frame_info = cogl_frame_info_new (priv->global_frame_counter);
  priv->global_frame_counter++;

  cogl_onscreen_add_frame_info (COGL_ONSCREEN (framebuffer), frame_info);
}

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface)
{
  iface->realize = clutter_stage_cogl_realize;
  iface->unrealize = clutter_stage_cogl_unrealize;
  iface->get_wrapper = clutter_stage_cogl_get_wrapper;
  iface->resize = clutter_stage_cogl_resize;
  iface->show = clutter_stage_cogl_show;
  iface->hide = clutter_stage_cogl_hide;
  iface->get_frame_counter = clutter_stage_cogl_get_frame_counter;
  iface->redraw_view = clutter_stage_cogl_redraw_view;
}

static void
clutter_stage_cogl_set_property (GObject      *gobject,
				 guint         prop_id,
				 const GValue *value,
				 GParamSpec   *pspec)
{
  ClutterStageCogl *self = CLUTTER_STAGE_COGL (gobject);

  switch (prop_id)
    {
    case PROP_WRAPPER:
      self->wrapper = g_value_get_object (value);
      break;

    case PROP_BACKEND:
      self->backend = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
_clutter_stage_cogl_class_init (ClutterStageCoglClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = clutter_stage_cogl_set_property;

  g_object_class_override_property (gobject_class, PROP_WRAPPER, "wrapper");
  g_object_class_override_property (gobject_class, PROP_BACKEND, "backend");
}

static void
_clutter_stage_cogl_init (ClutterStageCogl *stage)
{
}

static void
frame_cb (CoglOnscreen  *onscreen,
          CoglFrameEvent frame_event,
          CoglFrameInfo *frame_info,
          void          *user_data)
{
  ClutterStageView *view = user_data;

  if (frame_event == COGL_FRAME_EVENT_SYNC)
    return;

  if (cogl_frame_info_get_is_symbolic (frame_info))
    {
      clutter_stage_view_notify_ready (view);
    }
  else
    {
      ClutterFrameInfo clutter_frame_info;
      ClutterFrameInfoFlag flags = CLUTTER_FRAME_INFO_FLAG_NONE;

      if (cogl_frame_info_is_hw_clock (frame_info))
        flags |= CLUTTER_FRAME_INFO_FLAG_HW_CLOCK;

      if (cogl_frame_info_is_zero_copy (frame_info))
        flags |= CLUTTER_FRAME_INFO_FLAG_ZERO_COPY;

      if (cogl_frame_info_is_vsync (frame_info))
        flags |= CLUTTER_FRAME_INFO_FLAG_VSYNC;

      clutter_frame_info = (ClutterFrameInfo) {
        .frame_counter = cogl_frame_info_get_global_frame_counter (frame_info),
        .refresh_rate = cogl_frame_info_get_refresh_rate (frame_info),
        .presentation_time =
          cogl_frame_info_get_presentation_time_us (frame_info),
        .flags = flags,
        .sequence = cogl_frame_info_get_sequence (frame_info),
      };
      clutter_stage_view_notify_presented (view, &clutter_frame_info);
    }
}

static void
clutter_stage_view_cogl_dispose (GObject *object)
{
  ClutterStageViewCogl *view_cogl = CLUTTER_STAGE_VIEW_COGL (object);
  ClutterStageViewCoglPrivate *view_priv =
    clutter_stage_view_cogl_get_instance_private (view_cogl);
  ClutterStageView *view = CLUTTER_STAGE_VIEW (view_cogl);

  g_clear_handle_id (&view_priv->notify_presented_handle_id, g_source_remove);
  g_clear_pointer (&view_priv->damage_history, clutter_damage_history_free);

  if (view_priv->frame_cb_closure)
    {
      CoglFramebuffer *framebuffer;

      framebuffer = clutter_stage_view_get_onscreen (view);
      cogl_onscreen_remove_frame_callback (COGL_ONSCREEN (framebuffer),
                                           view_priv->frame_cb_closure);
      view_priv->frame_cb_closure = NULL;
    }

  G_OBJECT_CLASS (clutter_stage_view_cogl_parent_class)->dispose (object);
}

static void
clutter_stage_view_cogl_constructed (GObject *object)
{
  ClutterStageViewCogl *view_cogl = CLUTTER_STAGE_VIEW_COGL (object);
  ClutterStageViewCoglPrivate *view_priv =
    clutter_stage_view_cogl_get_instance_private (view_cogl);
  ClutterStageView *view = CLUTTER_STAGE_VIEW (view_cogl);
  CoglFramebuffer *framebuffer;

  framebuffer = clutter_stage_view_get_onscreen (view);
  if (framebuffer && COGL_IS_ONSCREEN (framebuffer))
    {
      view_priv->frame_cb_closure =
        cogl_onscreen_add_frame_callback (COGL_ONSCREEN (framebuffer),
                                          frame_cb,
                                          view,
                                          NULL);
    }

  G_OBJECT_CLASS (clutter_stage_view_cogl_parent_class)->constructed (object);
}

static void
clutter_stage_view_cogl_init (ClutterStageViewCogl *view_cogl)
{
  ClutterStageViewCoglPrivate *view_priv =
    clutter_stage_view_cogl_get_instance_private (view_cogl);

  view_priv->damage_history = clutter_damage_history_new ();
}

static void
clutter_stage_view_cogl_class_init (ClutterStageViewCoglClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = clutter_stage_view_cogl_constructed;
  object_class->dispose = clutter_stage_view_cogl_dispose;
}
