/*
 * Authored By Neil Roberts  <neil@linux.intel.com>
 * and Jasper St. Pierre <jstpierre@mecheye.net>
 *
 * Copyright (C) 2008 Intel Corporation
 * Copyright (C) 2012 Red Hat, Inc.
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
 */

/**
 * SECTION:meta-shaped-texture
 * @title: MetaShapedTexture
 * @short_description: An actor to draw a masked texture.
 */

#include "config.h"

#include "backends/meta-monitor-transform.h"
#include "compositor/meta-shaped-texture-private.h"
#include "core/boxes-private.h"

#include <gdk/gdk.h>
#include <math.h>

#include "cogl/cogl.h"
#include "compositor/clutter-utils.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-texture-tower.h"
#include "compositor/region-utils.h"
#include "core/boxes-private.h"
#include "meta/meta-shaped-texture.h"

/* MAX_MIPMAPPING_FPS needs to be as small as possible for the best GPU
 * performance, but higher than the refresh rate of commonly slow updating
 * windows like top or a blinking cursor, so that such windows do get
 * mipmapped.
 */
#define MAX_MIPMAPPING_FPS 5
#define MIN_MIPMAP_AGE_USEC (G_USEC_PER_SEC / MAX_MIPMAPPING_FPS)

/* MIN_FAST_UPDATES_BEFORE_UNMIPMAP allows windows to update themselves
 * occasionally without causing mipmapping to be disabled, so long as such
 * an update takes fewer update_area calls than:
 */
#define MIN_FAST_UPDATES_BEFORE_UNMIPMAP 20

static void meta_shaped_texture_dispose  (GObject    *object);

static void meta_shaped_texture_paint (ClutterActor       *actor);

static void meta_shaped_texture_get_preferred_width (ClutterActor *self,
                                                     gfloat        for_height,
                                                     gfloat       *min_width_p,
                                                     gfloat       *natural_width_p);

static void meta_shaped_texture_get_preferred_height (ClutterActor *self,
                                                      gfloat        for_width,
                                                      gfloat       *min_height_p,
                                                      gfloat       *natural_height_p);

static gboolean meta_shaped_texture_get_paint_volume (ClutterActor *self, ClutterPaintVolume *volume);

static void cullable_iface_init (MetaCullableInterface *iface);

enum
{
  SIZE_CHANGED,

  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

struct _MetaShapedTexture
{
  ClutterActor parent;

  MetaTextureTower *paint_tower;

  CoglTexture *texture;
  CoglTexture *mask_texture;
  CoglSnippet *snippet;

  CoglPipeline *base_pipeline;
  CoglPipeline *masked_pipeline;
  CoglPipeline *unblended_pipeline;

  gboolean is_y_inverted;

  /* The region containing only fully opaque pixels */
  cairo_region_t *opaque_region;

  /* MetaCullable regions, see that documentation for more details */
  cairo_region_t *clip_region;
  cairo_region_t *unobscured_region;

  gboolean size_invalid;
  MetaMonitorTransform transform;
  gboolean has_viewport_src_rect;
  ClutterRect viewport_src_rect;
  gboolean has_viewport_dst_size;
  int viewport_dst_width;
  int viewport_dst_height;

  int tex_width, tex_height;
  int fallback_width, fallback_height;
  int dst_width, dst_height;

  gint64 prev_invalidation, last_invalidation;
  guint fast_updates;
  guint remipmap_timeout_id;
  gint64 earliest_remipmap;

  guint create_mipmaps : 1;
};

G_DEFINE_TYPE_WITH_CODE (MetaShapedTexture, meta_shaped_texture, CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init));

static void
meta_shaped_texture_class_init (MetaShapedTextureClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  ClutterActorClass *actor_class = (ClutterActorClass *) klass;

  gobject_class->dispose = meta_shaped_texture_dispose;

  actor_class->get_preferred_width = meta_shaped_texture_get_preferred_width;
  actor_class->get_preferred_height = meta_shaped_texture_get_preferred_height;
  actor_class->paint = meta_shaped_texture_paint;
  actor_class->get_paint_volume = meta_shaped_texture_get_paint_volume;

  signals[SIZE_CHANGED] = g_signal_new ("size-changed",
                                        G_TYPE_FROM_CLASS (gobject_class),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL, NULL, NULL,
                                        G_TYPE_NONE, 0);
}

static void
invalidate_size (MetaShapedTexture *stex)
{
  stex->size_invalid = TRUE;
}

static void
meta_shaped_texture_init (MetaShapedTexture *stex)
{
  stex->paint_tower = meta_texture_tower_new ();

  stex->texture = NULL;
  stex->mask_texture = NULL;
  stex->create_mipmaps = TRUE;
  stex->is_y_inverted = TRUE;
  stex->transform = META_MONITOR_TRANSFORM_NORMAL;

  g_signal_connect (stex,
                    "notify::scale-x",
                    G_CALLBACK (invalidate_size),
                    stex);
}

static void
update_size (MetaShapedTexture *stex)
{
  ClutterActor *actor = CLUTTER_ACTOR (stex);
  int dst_width;
  int dst_height;

  if (stex->has_viewport_dst_size)
    {
      double tex_scale;

      clutter_actor_get_scale (actor, &tex_scale, NULL);
      dst_width = ceil (stex->viewport_dst_width / tex_scale);
      dst_height = ceil (stex->viewport_dst_height / tex_scale);
    }
  else if (stex->has_viewport_src_rect)
    {
      double tex_scale;

      clutter_actor_get_scale (actor, &tex_scale, NULL);
      dst_width = ceil (stex->viewport_src_rect.size.width / tex_scale);
      dst_height = ceil (stex->viewport_src_rect.size.height / tex_scale);
    }
  else
    {
      if (meta_monitor_transform_is_rotated (stex->transform))
        {
          if (stex->texture)
            {
              dst_width = stex->tex_height;
              dst_height = stex->tex_width;
            }
          else
            {
              dst_width = stex->fallback_height;
              dst_height = stex->fallback_width;
            }
        }
      else
        {
          if (stex->texture)
            {
              dst_width = stex->tex_width;
              dst_height = stex->tex_height;
            }
          else
            {
              dst_width = stex->fallback_width;
              dst_height = stex->fallback_height;
            }
        }
    }

  stex->size_invalid = FALSE;

  if (stex->dst_width != dst_width ||
      stex->dst_height != dst_height)
    {
      stex->dst_width = dst_width;
      stex->dst_height = dst_height;
      meta_shaped_texture_set_mask_texture (stex, NULL);
      clutter_actor_queue_relayout (CLUTTER_ACTOR (stex));
      g_signal_emit (stex, signals[SIZE_CHANGED], 0);
    }
}

static void
ensure_size_valid (MetaShapedTexture *stex)
{
  if (stex->size_invalid)
    update_size (stex);
}

static void
set_unobscured_region (MetaShapedTexture *stex,
                       cairo_region_t    *unobscured_region)
{
  g_clear_pointer (&stex->unobscured_region, cairo_region_destroy);
  if (unobscured_region)
    {
      int width, height;

      ensure_size_valid (stex);
      width = stex->dst_width;
      height = stex->dst_height;

      cairo_rectangle_int_t bounds = { 0, 0, width, height };
      stex->unobscured_region = cairo_region_copy (unobscured_region);
      cairo_region_intersect_rectangle (stex->unobscured_region, &bounds);
    }
}

static void
set_clip_region (MetaShapedTexture *stex,
                 cairo_region_t    *clip_region)
{
  g_clear_pointer (&stex->clip_region, cairo_region_destroy);
  if (clip_region)
    stex->clip_region = cairo_region_copy (clip_region);
}

static void
meta_shaped_texture_reset_pipelines (MetaShapedTexture *stex)
{
  g_clear_pointer (&stex->base_pipeline, cogl_object_unref);
  g_clear_pointer (&stex->masked_pipeline, cogl_object_unref);
  g_clear_pointer (&stex->unblended_pipeline, cogl_object_unref);
}

static void
meta_shaped_texture_dispose (GObject *object)
{
  MetaShapedTexture *stex = (MetaShapedTexture *) object;

  if (stex->remipmap_timeout_id)
    {
      g_source_remove (stex->remipmap_timeout_id);
      stex->remipmap_timeout_id = 0;
    }

  if (stex->paint_tower)
    meta_texture_tower_free (stex->paint_tower);
  stex->paint_tower = NULL;

  g_clear_pointer (&stex->texture, cogl_object_unref);
  g_clear_pointer (&stex->opaque_region, cairo_region_destroy);

  meta_shaped_texture_set_mask_texture (stex, NULL);
  set_unobscured_region (stex, NULL);
  set_clip_region (stex, NULL);

  meta_shaped_texture_reset_pipelines (stex);

  g_clear_pointer (&stex->snippet, cogl_object_unref);

  G_OBJECT_CLASS (meta_shaped_texture_parent_class)->dispose (object);
}

static CoglPipeline *
get_base_pipeline (MetaShapedTexture *stex,
                   CoglContext       *ctx)
{
  CoglPipeline *pipeline;
  CoglMatrix matrix;

  if (stex->base_pipeline)
    return stex->base_pipeline;

  pipeline = cogl_pipeline_new (ctx);
  cogl_pipeline_set_layer_wrap_mode_s (pipeline, 0,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  cogl_pipeline_set_layer_wrap_mode_t (pipeline, 0,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  cogl_pipeline_set_layer_wrap_mode_s (pipeline, 1,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  cogl_pipeline_set_layer_wrap_mode_t (pipeline, 1,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);

  cogl_matrix_init_identity (&matrix);

  if (!stex->is_y_inverted)
    {
      cogl_matrix_scale (&matrix, 1, -1, 1);
      cogl_matrix_translate (&matrix, 0, -1, 0);
      cogl_pipeline_set_layer_matrix (pipeline, 0, &matrix);
    }

  if (stex->transform != META_MONITOR_TRANSFORM_NORMAL)
    {
      CoglEuler euler;

      cogl_matrix_translate (&matrix, 0.5, 0.5, 0.0);
      switch (stex->transform)
        {
        case META_MONITOR_TRANSFORM_90:
          cogl_euler_init (&euler, 0.0, 0.0, 90.0);
          break;
        case META_MONITOR_TRANSFORM_180:
          cogl_euler_init (&euler, 0.0, 0.0, 180.0);
          break;
        case META_MONITOR_TRANSFORM_270:
          cogl_euler_init (&euler, 0.0, 0.0, 270.0);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED:
          cogl_euler_init (&euler, 180.0, 0.0, 0.0);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_90:
          cogl_euler_init (&euler, 0.0, 180.0, 90.0);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_180:
          cogl_euler_init (&euler, 180.0, 0.0, 180.0);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_270:
          cogl_euler_init (&euler, 0.0, 180.0, 270.0);
          break;
        case META_MONITOR_TRANSFORM_NORMAL:
          g_assert_not_reached ();
        }
      cogl_matrix_rotate_euler (&matrix, &euler);
      cogl_matrix_translate (&matrix, -0.5, -0.5, 0.0);
    }

  if (stex->has_viewport_src_rect)
    {
      ClutterActor *actor = CLUTTER_ACTOR (stex);
      double tex_scale;

      clutter_actor_get_scale (actor, &tex_scale, NULL);

      if (meta_monitor_transform_is_rotated (stex->transform))
        {
          cogl_matrix_scale (&matrix,
                             stex->viewport_src_rect.size.width /
                             (stex->tex_height * tex_scale),
                             stex->viewport_src_rect.size.height /
                             (stex->tex_width * tex_scale),
                             1);
        }
      else
        {
          cogl_matrix_scale (&matrix,
                             stex->viewport_src_rect.size.width /
                             (stex->tex_width * tex_scale),
                             stex->viewport_src_rect.size.height /
                             (stex->tex_height * tex_scale),
                             1);
        }

      cogl_matrix_translate (&matrix,
                             stex->viewport_src_rect.origin.x /
                             stex->viewport_src_rect.size.width,
                             stex->viewport_src_rect.origin.y /
                             stex->viewport_src_rect.size.height,
                             0);
    }

  cogl_pipeline_set_layer_matrix (pipeline, 0, &matrix);
  cogl_pipeline_set_layer_matrix (pipeline, 1, &matrix);

  if (stex->snippet)
    cogl_pipeline_add_layer_snippet (pipeline, 0, stex->snippet);

  stex->base_pipeline = pipeline;

  return stex->base_pipeline;
}

static CoglPipeline *
get_unmasked_pipeline (MetaShapedTexture *stex,
                       CoglContext       *ctx)
{
  return get_base_pipeline (stex, ctx);
}

static CoglPipeline *
get_masked_pipeline (MetaShapedTexture *stex,
                     CoglContext       *ctx)
{
  CoglPipeline *pipeline;

  if (stex->masked_pipeline)
    return stex->masked_pipeline;

  pipeline = cogl_pipeline_copy (get_base_pipeline (stex, ctx));
  cogl_pipeline_set_layer_combine (pipeline, 1,
                                   "RGBA = MODULATE (PREVIOUS, TEXTURE[A])",
                                   NULL);

  stex->masked_pipeline = pipeline;

  return pipeline;
}

static CoglPipeline *
get_unblended_pipeline (MetaShapedTexture *stex,
                        CoglContext       *ctx)
{
  CoglPipeline *pipeline;
  CoglColor color;

  if (stex->unblended_pipeline)
    return stex->unblended_pipeline;

  pipeline = cogl_pipeline_copy (get_base_pipeline (stex, ctx));
  cogl_color_init_from_4ub (&color, 255, 255, 255, 255);
  cogl_pipeline_set_blend (pipeline,
                           "RGBA = ADD (SRC_COLOR, 0)",
                           NULL);
  cogl_pipeline_set_color (pipeline, &color);

  stex->unblended_pipeline = pipeline;

  return pipeline;
}

static void
paint_clipped_rectangle (MetaShapedTexture     *stex,
                         CoglFramebuffer       *fb,
                         CoglPipeline          *pipeline,
                         cairo_rectangle_int_t *rect,
                         ClutterActorBox       *alloc)
{
  float coords[8];
  float x1, y1, x2, y2;
  float alloc_width;
  float alloc_height;

  x1 = rect->x;
  y1 = rect->y;
  x2 = rect->x + rect->width;
  y2 = rect->y + rect->height;
  alloc_width = alloc->x2 - alloc->x1;
  alloc_height = alloc->y2 - alloc->y1;

  coords[0] = rect->x / alloc_width;
  coords[1] = rect->y / alloc_height;
  coords[2] = (rect->x + rect->width) / alloc_width;
  coords[3] = (rect->y + rect->height) / alloc_height;

  coords[4] = coords[0];
  coords[5] = coords[1];
  coords[6] = coords[2];
  coords[7] = coords[3];

  cogl_framebuffer_draw_multitextured_rectangle (fb, pipeline,
                                                 x1, y1, x2, y2,
                                                 &coords[0], 8);
}

static void
set_cogl_texture (MetaShapedTexture *stex,
                  CoglTexture       *cogl_tex)
{
  int width, height;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  if (stex->texture)
    cogl_object_unref (stex->texture);

  stex->texture = cogl_tex;

  if (cogl_tex != NULL)
    {
      cogl_object_ref (cogl_tex);
      width = cogl_texture_get_width (COGL_TEXTURE (cogl_tex));
      height = cogl_texture_get_height (COGL_TEXTURE (cogl_tex));
    }
  else
    {
      width = 0;
      height = 0;
    }

  if (stex->tex_width != width ||
      stex->tex_height != height)
    {
      stex->tex_width = width;
      stex->tex_height = height;
      update_size (stex);
    }

  /* NB: We don't queue a redraw of the actor here because we don't
   * know how much of the buffer has changed with respect to the
   * previous buffer. We only queue a redraw in response to surface
   * damage. */

  if (stex->create_mipmaps)
    meta_texture_tower_set_base_texture (stex->paint_tower, cogl_tex);
}

static gboolean
texture_is_idle_and_not_mipmapped (gpointer user_data)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (user_data);

  if ((g_get_monotonic_time () - stex->earliest_remipmap) < 0)
    return G_SOURCE_CONTINUE;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stex));
  stex->remipmap_timeout_id = 0;

  return G_SOURCE_REMOVE;
}

static void
do_paint (MetaShapedTexture *stex,
          CoglFramebuffer   *fb,
          CoglTexture       *paint_tex,
          cairo_region_t    *clip_region)
{
  double tex_scale;
  int dst_width, dst_height;
  cairo_rectangle_int_t tex_rect;
  guchar opacity;
  gboolean use_opaque_region;
  cairo_region_t *clip_tex_region;
  cairo_region_t *opaque_tex_region;
  cairo_region_t *blended_tex_region;
  CoglContext *ctx;
  ClutterActorBox alloc;
  CoglPipelineFilter filter;

  clutter_actor_get_scale (CLUTTER_ACTOR (stex), &tex_scale, NULL);
  ensure_size_valid (stex);
  dst_width = stex->dst_width;

  dst_height = stex->dst_height;
  if (dst_width == 0 || dst_height == 0) /* no contents yet */
    return;

  tex_rect = (cairo_rectangle_int_t) { 0, 0, dst_width, dst_height };

  /* Use nearest-pixel interpolation if the texture is unscaled. This
   * improves performance, especially with software rendering.
   */

  filter = COGL_PIPELINE_FILTER_LINEAR;

  if (meta_actor_painting_untransformed (fb,
                                         dst_width, dst_height,
                                         NULL, NULL))
    filter = COGL_PIPELINE_FILTER_NEAREST;

  ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());

  opacity = clutter_actor_get_paint_opacity (CLUTTER_ACTOR (stex));
  clutter_actor_get_allocation_box (CLUTTER_ACTOR (stex), &alloc);

  if (stex->opaque_region && opacity == 255)
    {
      opaque_tex_region =
        meta_region_scale_double (stex->opaque_region,
                                  1.0 / tex_scale,
                                  META_ROUNDING_STRATEGY_SHRINK);
      use_opaque_region = TRUE;
    }
  else
    {
      opaque_tex_region = NULL;
      use_opaque_region = FALSE;
    }

  if (clip_region)
    {
      clip_tex_region =
        meta_region_scale_double (clip_region,
                                  1.0 / tex_scale,
                                  META_ROUNDING_STRATEGY_GROW);
    }
  else
    {
      clip_tex_region = NULL;
    }

  if (use_opaque_region)
    {
      if (clip_tex_region)
        blended_tex_region = cairo_region_copy (clip_tex_region);
      else
        blended_tex_region = cairo_region_create_rectangle (&tex_rect);

      cairo_region_subtract (blended_tex_region, opaque_tex_region);
    }
  else
    {
      if (clip_tex_region)
        blended_tex_region = cairo_region_reference (clip_tex_region);
      else
        blended_tex_region = NULL;
    }

  /* Limit to how many separate rectangles we'll draw; beyond this just
   * fall back and draw the whole thing */
#define MAX_RECTS 16

  if (blended_tex_region)
    {
      int n_rects = cairo_region_num_rectangles (blended_tex_region);
      if (n_rects > MAX_RECTS)
        {
          /* Fall back to taking the fully blended path. */
          use_opaque_region = FALSE;

          g_clear_pointer (&blended_tex_region, cairo_region_destroy);
        }
    }

  /* First, paint the unblended parts, which are part of the opaque region. */
  if (use_opaque_region)
    {
      CoglPipeline *opaque_pipeline;
      cairo_region_t *region;
      int n_rects;
      int i;

      if (clip_tex_region)
        {
          region = cairo_region_copy (clip_tex_region);
          cairo_region_intersect (region, opaque_tex_region);
        }
      else
        {
          region = cairo_region_reference (opaque_tex_region);
        }

      if (!cairo_region_is_empty (region))
        {
          opaque_pipeline = get_unblended_pipeline (stex, ctx);
          cogl_pipeline_set_layer_texture (opaque_pipeline, 0, paint_tex);
          cogl_pipeline_set_layer_filters (opaque_pipeline, 0, filter, filter);

          n_rects = cairo_region_num_rectangles (region);
          for (i = 0; i < n_rects; i++)
            {
              cairo_rectangle_int_t rect;
              cairo_region_get_rectangle (region, i, &rect);
              paint_clipped_rectangle (stex,
                                       fb,
                                       opaque_pipeline,
                                       &rect,
                                       &alloc);
            }
        }

      cairo_region_destroy (region);
    }

  /* Now, go ahead and paint the blended parts. */

  /* We have three cases:
   *   1) blended_tex_region has rectangles - paint the rectangles.
   *   2) blended_tex_region is empty - don't paint anything
   *   3) blended_tex_region is NULL - paint fully-blended.
   *
   *   1) and 3) are the times where we have to paint stuff. This tests
   *   for 1) and 3).
   */
  if (!blended_tex_region || !cairo_region_is_empty (blended_tex_region))
    {
      CoglPipeline *blended_pipeline;

      if (stex->mask_texture == NULL)
        {
          blended_pipeline = get_unmasked_pipeline (stex, ctx);
        }
      else
        {
          blended_pipeline = get_masked_pipeline (stex, ctx);
          cogl_pipeline_set_layer_texture (blended_pipeline, 1, stex->mask_texture);
          cogl_pipeline_set_layer_filters (blended_pipeline, 1, filter, filter);
        }

      cogl_pipeline_set_layer_texture (blended_pipeline, 0, paint_tex);
      cogl_pipeline_set_layer_filters (blended_pipeline, 0, filter, filter);

      CoglColor color;
      cogl_color_init_from_4ub (&color, opacity, opacity, opacity, opacity);
      cogl_pipeline_set_color (blended_pipeline, &color);

      if (blended_tex_region)
        {
          /* 1) blended_tex_region is not empty. Paint the rectangles. */
          int i;
          int n_rects = cairo_region_num_rectangles (blended_tex_region);

          for (i = 0; i < n_rects; i++)
            {
              cairo_rectangle_int_t rect;
              cairo_region_get_rectangle (blended_tex_region, i, &rect);

              if (!gdk_rectangle_intersect (&tex_rect, &rect, &rect))
                continue;

              paint_clipped_rectangle (stex,
                                       fb,
                                       blended_pipeline,
                                       &rect,
                                       &alloc);
            }
        }
      else
        {
          /* 3) blended_tex_region is NULL. Do a full paint. */
          cogl_framebuffer_draw_rectangle (fb, blended_pipeline,
                                           0, 0,
                                           alloc.x2 - alloc.x1,
                                           alloc.y2 - alloc.y1);
        }
    }

  g_clear_pointer (&clip_tex_region, cairo_region_destroy);
  g_clear_pointer (&opaque_tex_region, cairo_region_destroy);
  g_clear_pointer (&blended_tex_region, cairo_region_destroy);
}

static void
meta_shaped_texture_paint (ClutterActor *actor)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (actor);
  CoglTexture *paint_tex;
  CoglFramebuffer *fb;

  if (!stex->texture)
    return;

  if (stex->clip_region && cairo_region_is_empty (stex->clip_region))
    return;

  if (!CLUTTER_ACTOR_IS_REALIZED (CLUTTER_ACTOR (stex)))
    clutter_actor_realize (CLUTTER_ACTOR (stex));

  /* The GL EXT_texture_from_pixmap extension does allow for it to be
   * used together with SGIS_generate_mipmap, however this is very
   * rarely supported. Also, even when it is supported there
   * are distinct performance implications from:
   *
   *  - Updating mipmaps that we don't need
   *  - Having to reallocate pixmaps on the server into larger buffers
   *
   * So, we just unconditionally use our mipmap emulation code. If we
   * wanted to use SGIS_generate_mipmap, we'd have to  query COGL to
   * see if it was supported (no API currently), and then if and only
   * if that was the case, set the clutter texture quality to HIGH.
   * Setting the texture quality to high without SGIS_generate_mipmap
   * support for TFP textures will result in fallbacks to XGetImage.
   */
  if (stex->create_mipmaps)
    {
      int64_t now = g_get_monotonic_time ();
      int64_t age = now - stex->last_invalidation;

      if (age >= MIN_MIPMAP_AGE_USEC ||
          stex->fast_updates < MIN_FAST_UPDATES_BEFORE_UNMIPMAP)
        {
          paint_tex = meta_texture_tower_get_paint_texture (stex->paint_tower);
          if (!paint_tex)
            paint_tex = stex->texture;
        }
      else
        {
          paint_tex = stex->texture;

          /* Minus 1000 to ensure we don't fail the age test in timeout */
          stex->earliest_remipmap = now + MIN_MIPMAP_AGE_USEC - 1000;

          if (!stex->remipmap_timeout_id)
            stex->remipmap_timeout_id =
              g_timeout_add (MIN_MIPMAP_AGE_USEC / 1000,
                             texture_is_idle_and_not_mipmapped,
                             stex);
        }
    }
  else
    {
      paint_tex = COGL_TEXTURE (stex->texture);
    }

  if (cogl_texture_get_width (paint_tex) == 0 ||
      cogl_texture_get_height (paint_tex) == 0)
    return;

  fb = cogl_get_draw_framebuffer ();
  do_paint (META_SHAPED_TEXTURE (actor), fb, paint_tex, stex->clip_region);
}

static void
meta_shaped_texture_get_preferred_width (ClutterActor *self,
                                         gfloat        for_height,
                                         gfloat       *min_width_p,
                                         gfloat       *natural_width_p)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (self);

  ensure_size_valid (stex);

  if (min_width_p)
    *min_width_p = stex->dst_width;
  if (natural_width_p)
    *natural_width_p = stex->dst_width;
}

static void
meta_shaped_texture_get_preferred_height (ClutterActor *self,
                                          gfloat        for_width,
                                          gfloat       *min_height_p,
                                          gfloat       *natural_height_p)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (self);

  ensure_size_valid (stex);

  if (min_height_p)
    *min_height_p = stex->dst_height;
  if (natural_height_p)
    *natural_height_p = stex->dst_height;
}

static cairo_region_t *
effective_unobscured_region (MetaShapedTexture *stex)
{
  ClutterActor *actor;

  /* Fail if we have any mapped clones. */
  actor = CLUTTER_ACTOR (stex);
  do
    {
      if (clutter_actor_has_mapped_clones (actor))
        return NULL;
      actor = clutter_actor_get_parent (actor);
    }
  while (actor != NULL);

  return stex->unobscured_region;
}

static gboolean
meta_shaped_texture_get_paint_volume (ClutterActor *actor,
                                      ClutterPaintVolume *volume)
{
  return clutter_paint_volume_set_from_allocation (volume, actor);
}

void
meta_shaped_texture_set_create_mipmaps (MetaShapedTexture *stex,
					gboolean           create_mipmaps)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  create_mipmaps = create_mipmaps != FALSE;

  if (create_mipmaps != stex->create_mipmaps)
    {
      CoglTexture *base_texture;
      stex->create_mipmaps = create_mipmaps;
      base_texture = create_mipmaps ? stex->texture : NULL;
      meta_texture_tower_set_base_texture (stex->paint_tower, base_texture);
    }
}

void
meta_shaped_texture_set_mask_texture (MetaShapedTexture *stex,
                                      CoglTexture       *mask_texture)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  g_clear_pointer (&stex->mask_texture, cogl_object_unref);

  if (mask_texture != NULL)
    {
      stex->mask_texture = mask_texture;
      cogl_object_ref (stex->mask_texture);
    }

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stex));
}

gboolean
meta_shaped_texture_is_obscured (MetaShapedTexture *stex)
{
  cairo_region_t *unobscured_region = effective_unobscured_region (stex);

  if (unobscured_region)
    return cairo_region_is_empty (unobscured_region);
  else
    return FALSE;
}

/**
 * meta_shaped_texture_update_area:
 * @stex: #MetaShapedTexture
 * @x: the x coordinate of the damaged area
 * @y: the y coordinate of the damaged area
 * @width: the width of the damaged area
 * @height: the height of the damaged area
 *
 * Repairs the damaged area indicated by @x, @y, @width and @height
 * and potentially queues a redraw.
 *
 * Return value: Whether a redraw have been queued or not
 */
gboolean
meta_shaped_texture_update_area (MetaShapedTexture *stex,
                                 int                x,
                                 int                y,
                                 int                width,
                                 int                height)
{
  cairo_region_t *unobscured_region;
  cairo_rectangle_int_t clip;
  MetaMonitorTransform inverted_transform;

  if (stex->texture == NULL)
    return FALSE;

  clip = (cairo_rectangle_int_t) {
    .x = x,
    .y = y,
    .width = width,
    .height = height
  };

  inverted_transform = meta_monitor_transform_invert (stex->transform);
  ensure_size_valid (stex);
  meta_rectangle_transform (&clip,
                            inverted_transform,
                            stex->dst_width,
                            stex->dst_height,
                            &clip);

  if (stex->has_viewport_src_rect || stex->has_viewport_dst_size)
    {
      ClutterRect viewport;
      ClutterRect inverted_viewport;
      double tex_scale;
      float dst_width;
      float dst_height;
      int inverted_dst_width;
      int inverted_dst_height;

      clutter_actor_get_scale (CLUTTER_ACTOR (stex), &tex_scale, NULL);

      if (stex->has_viewport_src_rect)
        {
          viewport = stex->viewport_src_rect;
        }
      else
        {
          viewport = (ClutterRect) {
            .origin.x = 0,
            .origin.y = 0,
            .size.width = stex->tex_width * tex_scale,
            .size.height = stex->tex_height * tex_scale
          };
        }

      if (stex->has_viewport_dst_size)
        {
          dst_width = (float) stex->viewport_dst_width;
          dst_height = (float) stex->viewport_dst_height;
        }
      else
        {
          dst_width = (float) stex->tex_width * tex_scale;
          dst_height = (float) stex->tex_height * tex_scale;
        }

      inverted_viewport = (ClutterRect) {
        .origin.x = -((viewport.origin.x * (dst_width / viewport.size.width)) / tex_scale),
        .origin.y = -((viewport.origin.y * (dst_height / viewport.size.height)) / tex_scale),
        .size.width = dst_width,
        .size.height = dst_height
      };
      inverted_dst_width = ceilf (viewport.size.width);
      inverted_dst_height = ceilf (viewport.size.height);

      meta_rectangle_crop_and_scale (&clip,
                                     &inverted_viewport,
                                     inverted_dst_width,
                                     inverted_dst_height,
                                     &clip);
    }

  meta_texture_tower_update_area (stex->paint_tower,
                                  clip.x,
                                  clip.y,
                                  clip.width,
                                  clip.height);

  stex->prev_invalidation = stex->last_invalidation;
  stex->last_invalidation = g_get_monotonic_time ();

  if (stex->prev_invalidation)
    {
      gint64 interval = stex->last_invalidation - stex->prev_invalidation;
      gboolean fast_update = interval < MIN_MIPMAP_AGE_USEC;

      if (!fast_update)
        stex->fast_updates = 0;
      else if (stex->fast_updates < MIN_FAST_UPDATES_BEFORE_UNMIPMAP)
        stex->fast_updates++;
    }

  unobscured_region = effective_unobscured_region (stex);
  if (unobscured_region)
    {
      cairo_region_t *intersection;

      if (cairo_region_is_empty (unobscured_region))
        return FALSE;

      intersection = cairo_region_copy (unobscured_region);
      cairo_region_intersect_rectangle (intersection, &clip);

      if (!cairo_region_is_empty (intersection))
        {
          cairo_rectangle_int_t damage_rect;
          cairo_region_get_extents (intersection, &damage_rect);
          clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stex), &damage_rect);
          cairo_region_destroy (intersection);
          return TRUE;
        }

      cairo_region_destroy (intersection);
      return FALSE;
    }
  else
    {
      clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stex), &clip);
      return TRUE;
    }
}

/**
 * meta_shaped_texture_set_texture:
 * @stex: The #MetaShapedTexture
 * @pixmap: The #CoglTexture to display
 */
void
meta_shaped_texture_set_texture (MetaShapedTexture *stex,
                                 CoglTexture       *texture)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  set_cogl_texture (stex, texture);
}

/**
 * meta_shaped_texture_set_is_y_inverted: (skip)
 */
void
meta_shaped_texture_set_is_y_inverted (MetaShapedTexture *stex,
                                       gboolean           is_y_inverted)
{
  if (stex->is_y_inverted == is_y_inverted)
    return;

  meta_shaped_texture_reset_pipelines (stex);

  stex->is_y_inverted = is_y_inverted;
}

/**
 * meta_shaped_texture_set_snippet: (skip)
 */
void
meta_shaped_texture_set_snippet (MetaShapedTexture *stex,
                                 CoglSnippet       *snippet)
{
  if (stex->snippet == snippet)
    return;

  meta_shaped_texture_reset_pipelines (stex);

  g_clear_pointer (&stex->snippet, cogl_object_unref);
  if (snippet)
    stex->snippet = cogl_object_ref (snippet);
}

/**
 * meta_shaped_texture_get_texture:
 * @stex: The #MetaShapedTexture
 *
 * Returns: (transfer none): the unshaped texture
 */
CoglTexture *
meta_shaped_texture_get_texture (MetaShapedTexture *stex)
{
  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), NULL);
  return COGL_TEXTURE (stex->texture);
}

/**
 * meta_shaped_texture_set_opaque_region:
 * @stex: a #MetaShapedTexture
 * @opaque_region: (transfer full): the region of the texture that
 *   can have blending turned off.
 *
 * As most windows have a large portion that does not require blending,
 * we can easily turn off blending if we know the areas that do not
 * require blending. This sets the region where we will not blend for
 * optimization purposes.
 */
void
meta_shaped_texture_set_opaque_region (MetaShapedTexture *stex,
                                       cairo_region_t    *opaque_region)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  if (stex->opaque_region)
    cairo_region_destroy (stex->opaque_region);

  if (opaque_region)
    stex->opaque_region = cairo_region_reference (opaque_region);
  else
    stex->opaque_region = NULL;
}

cairo_region_t *
meta_shaped_texture_get_opaque_region (MetaShapedTexture *stex)
{
  return stex->opaque_region;
}

void
meta_shaped_texture_set_transform (MetaShapedTexture    *stex,
                                   MetaMonitorTransform  transform)
{
  if (stex->transform == transform)
    return;

  stex->transform = transform;

  meta_shaped_texture_reset_pipelines (stex);
  invalidate_size (stex);
}

void
meta_shaped_texture_set_viewport_src_rect (MetaShapedTexture *stex,
                                           ClutterRect       *src_rect)
{
  if (!stex->has_viewport_src_rect ||
      stex->viewport_src_rect.origin.x != src_rect->origin.x ||
      stex->viewport_src_rect.origin.y != src_rect->origin.y ||
      stex->viewport_src_rect.size.width != src_rect->size.width ||
      stex->viewport_src_rect.size.height != src_rect->size.height)
    {
      stex->has_viewport_src_rect = TRUE;
      stex->viewport_src_rect = *src_rect;
      meta_shaped_texture_reset_pipelines (stex);
      invalidate_size (stex);
    }
}

void
meta_shaped_texture_reset_viewport_src_rect (MetaShapedTexture *stex)
{
  if (!stex->has_viewport_src_rect)
    return;

  stex->has_viewport_src_rect = FALSE;
  meta_shaped_texture_reset_pipelines (stex);
  invalidate_size (stex);
}

void
meta_shaped_texture_set_viewport_dst_size (MetaShapedTexture *stex,
                                           int                dst_width,
                                           int                dst_height)
{
  if (!stex->has_viewport_dst_size ||
      stex->viewport_dst_width != dst_width ||
      stex->viewport_dst_height != dst_height)
    {
      stex->has_viewport_dst_size = TRUE;
      stex->viewport_dst_width = dst_width;
      stex->viewport_dst_height = dst_height;
      invalidate_size (stex);
    }
}

void
meta_shaped_texture_reset_viewport_dst_size (MetaShapedTexture *stex)
{
  if (!stex->has_viewport_dst_size)
    return;

  stex->has_viewport_dst_size = FALSE;
  invalidate_size (stex);
}

static gboolean
should_get_via_offscreen (MetaShapedTexture *stex)
{
  if (!cogl_texture_is_get_data_supported (stex->texture))
    return TRUE;

  if (stex->has_viewport_src_rect || stex->has_viewport_dst_size)
    return TRUE;

  switch (stex->transform)
    {
    case META_MONITOR_TRANSFORM_90:
    case META_MONITOR_TRANSFORM_180:
    case META_MONITOR_TRANSFORM_270:
    case META_MONITOR_TRANSFORM_FLIPPED:
    case META_MONITOR_TRANSFORM_FLIPPED_90:
    case META_MONITOR_TRANSFORM_FLIPPED_180:
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      return TRUE;
    case META_MONITOR_TRANSFORM_NORMAL:
      break;
    }

  return FALSE;
}

static cairo_surface_t *
get_image_via_offscreen (MetaShapedTexture     *stex,
                         cairo_rectangle_int_t *clip)
{
  ClutterBackend *clutter_backend = clutter_get_default_backend ();
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglTexture *image_texture;
  GError *error = NULL;
  CoglOffscreen *offscreen;
  CoglFramebuffer *fb;
  CoglMatrix projection_matrix;
  cairo_rectangle_int_t fallback_clip;
  CoglColor clear_color;
  cairo_surface_t *surface;

  if (!clip)
    {
      fallback_clip = (cairo_rectangle_int_t) {
        .width = stex->dst_width,
        .height = stex->dst_height,
      };
      clip = &fallback_clip;
    }

  image_texture =
    COGL_TEXTURE (cogl_texture_2d_new_with_size (cogl_context,
                                                 stex->dst_width,
                                                 stex->dst_height));
  cogl_primitive_texture_set_auto_mipmap (COGL_PRIMITIVE_TEXTURE (image_texture),
                                          FALSE);
  if (!cogl_texture_allocate (COGL_TEXTURE (image_texture), &error))
    {
      g_error_free (error);
      cogl_object_unref (image_texture);
      return FALSE;
    }

  offscreen = cogl_offscreen_new_with_texture (COGL_TEXTURE (image_texture));
  fb = COGL_FRAMEBUFFER (offscreen);
  cogl_object_unref (image_texture);
  if (!cogl_framebuffer_allocate (fb, &error))
    {
      g_error_free (error);
      cogl_object_unref (fb);
      return FALSE;
    }

  cogl_framebuffer_push_matrix (fb);
  cogl_matrix_init_identity (&projection_matrix);
  cogl_matrix_scale (&projection_matrix,
                     1.0 / (stex->dst_width / 2.0),
                     -1.0 / (stex->dst_height / 2.0), 0);
  cogl_matrix_translate (&projection_matrix,
                         -(stex->dst_width / 2.0),
                         -(stex->dst_height / 2.0), 0);

  cogl_framebuffer_set_projection_matrix (fb, &projection_matrix);

  cogl_color_init_from_4ub (&clear_color, 0, 0, 0, 0);
  cogl_framebuffer_clear (fb, COGL_BUFFER_BIT_COLOR, &clear_color);

  do_paint (stex, fb, stex->texture, NULL);

  cogl_framebuffer_pop_matrix (fb);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        clip->width, clip->height);
  cogl_framebuffer_read_pixels (fb,
                                clip->x, clip->y,
                                clip->width, clip->height,
                                CLUTTER_CAIRO_FORMAT_ARGB32,
                                cairo_image_surface_get_data (surface));
  cogl_object_unref (fb);

  cairo_surface_mark_dirty (surface);

  return surface;
}

/**
 * meta_shaped_texture_get_image:
 * @stex: A #MetaShapedTexture
 * @clip: A clipping rectangle, to help prevent extra processing.
 * In the case that the clipping rectangle is partially or fully
 * outside the bounds of the texture, the rectangle will be clipped.
 *
 * Flattens the two layers of the shaped texture into one ARGB32
 * image by alpha blending the two images, and returns the flattened
 * image.
 *
 * Returns: (transfer full): a new cairo surface to be freed with
 * cairo_surface_destroy().
 */
cairo_surface_t *
meta_shaped_texture_get_image (MetaShapedTexture     *stex,
                               cairo_rectangle_int_t *clip)
{
  cairo_rectangle_int_t *transformed_clip = NULL;
  CoglTexture *texture, *mask_texture;
  cairo_surface_t *surface;

  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), NULL);

  texture = COGL_TEXTURE (stex->texture);

  if (texture == NULL)
    return NULL;

  ensure_size_valid (stex);

  if (stex->dst_width == 0 || stex->dst_height == 0)
    return NULL;

  if (clip != NULL)
    {
      double tex_scale;
      cairo_rectangle_int_t dst_rect;

      transformed_clip = alloca (sizeof (cairo_rectangle_int_t));

      clutter_actor_get_scale (CLUTTER_ACTOR (stex), &tex_scale, NULL);
      meta_rectangle_scale_double (clip, 1.0 / tex_scale,
                                   META_ROUNDING_STRATEGY_GROW,
                                   transformed_clip);

      dst_rect = (cairo_rectangle_int_t) {
        .width = stex->dst_width,
        .height = stex->dst_height,
      };

      if (!meta_rectangle_intersect (&dst_rect, transformed_clip,
                                     transformed_clip))
        return NULL;
    }

  if (should_get_via_offscreen (stex))
    return get_image_via_offscreen (stex, transformed_clip);

  if (transformed_clip)
    texture = cogl_texture_new_from_sub_texture (texture,
                                                 transformed_clip->x,
                                                 transformed_clip->y,
                                                 transformed_clip->width,
                                                 transformed_clip->height);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        cogl_texture_get_width (texture),
                                        cogl_texture_get_height (texture));

  cogl_texture_get_data (texture, CLUTTER_CAIRO_FORMAT_ARGB32,
                         cairo_image_surface_get_stride (surface),
                         cairo_image_surface_get_data (surface));

  cairo_surface_mark_dirty (surface);

  if (transformed_clip)
    cogl_object_unref (texture);

  mask_texture = stex->mask_texture;
  if (mask_texture != NULL)
    {
      cairo_t *cr;
      cairo_surface_t *mask_surface;

      if (transformed_clip)
        mask_texture =
          cogl_texture_new_from_sub_texture (mask_texture,
                                             transformed_clip->x,
                                             transformed_clip->y,
                                             transformed_clip->width,
                                             transformed_clip->height);

      mask_surface = cairo_image_surface_create (CAIRO_FORMAT_A8,
                                                 cogl_texture_get_width (mask_texture),
                                                 cogl_texture_get_height (mask_texture));

      cogl_texture_get_data (mask_texture, COGL_PIXEL_FORMAT_A_8,
                             cairo_image_surface_get_stride (mask_surface),
                             cairo_image_surface_get_data (mask_surface));

      cairo_surface_mark_dirty (mask_surface);

      cr = cairo_create (surface);
      cairo_set_source_surface (cr, mask_surface, 0, 0);
      cairo_set_operator (cr, CAIRO_OPERATOR_DEST_IN);
      cairo_paint (cr);
      cairo_destroy (cr);

      cairo_surface_destroy (mask_surface);

      if (transformed_clip)
        cogl_object_unref (mask_texture);
    }

  return surface;
}

void
meta_shaped_texture_set_fallback_size (MetaShapedTexture *stex,
                                       int                fallback_width,
                                       int                fallback_height)
{
  stex->fallback_width = fallback_width;
  stex->fallback_height = fallback_height;

  invalidate_size (stex);
}

static void
meta_shaped_texture_cull_out (MetaCullable   *cullable,
                              cairo_region_t *unobscured_region,
                              cairo_region_t *clip_region)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (cullable);

  set_unobscured_region (stex, unobscured_region);
  set_clip_region (stex, clip_region);

  if (clutter_actor_get_paint_opacity (CLUTTER_ACTOR (stex)) == 0xff)
    {
      if (stex->opaque_region)
        {
          if (unobscured_region)
            cairo_region_subtract (unobscured_region, stex->opaque_region);
          if (clip_region)
            cairo_region_subtract (clip_region, stex->opaque_region);
        }
    }
}

static void
meta_shaped_texture_reset_culling (MetaCullable *cullable)
{
  MetaShapedTexture *self = META_SHAPED_TEXTURE (cullable);
  set_clip_region (self, NULL);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_out = meta_shaped_texture_cull_out;
  iface->reset_culling = meta_shaped_texture_reset_culling;
}

ClutterActor *
meta_shaped_texture_new (void)
{
  return g_object_new (META_TYPE_SHAPED_TEXTURE, NULL);
}
