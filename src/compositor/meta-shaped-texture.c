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

#include "compositor/meta-shaped-texture-private.h"

#include <gdk/gdk.h>

#include "cogl/cogl.h"
#include "compositor/clutter-utils.h"
#include "compositor/region-utils.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-texture-tower.h"
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

enum {
  SIZE_CHANGED,

  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

/**
 * MetaShapedTexture:
 *
 * The <structname>MetaShapedTexture</structname> structure contains
 * only private data and should be accessed using the provided API
 */
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
meta_shaped_texture_init (MetaShapedTexture *self)
{
  self->paint_tower = meta_texture_tower_new ();

  self->texture = NULL;
  self->mask_texture = NULL;
  self->create_mipmaps = TRUE;
  self->is_y_inverted = TRUE;
  self->transform = META_MONITOR_TRANSFORM_NORMAL;

  g_signal_connect (self,
                    "notify::scale-x",
                    G_CALLBACK (invalidate_size),
                    self);
}

static void
update_size (MetaShapedTexture *stex)
{
  int dst_width;
  int dst_height;

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
set_unobscured_region (MetaShapedTexture *self,
                       cairo_region_t    *unobscured_region)
{
  g_clear_pointer (&self->unobscured_region, cairo_region_destroy);
  if (unobscured_region)
    {
      int width, height;

      ensure_size_valid (self);
      width = self->dst_width;
      height = self->dst_height;

      cairo_rectangle_int_t bounds = { 0, 0, width, height };
      self->unobscured_region = cairo_region_copy (unobscured_region);
      cairo_region_intersect_rectangle (self->unobscured_region, &bounds);
    }
}

static void
set_clip_region (MetaShapedTexture *self,
                 cairo_region_t    *clip_region)
{
  g_clear_pointer (&self->clip_region, cairo_region_destroy);
  if (clip_region)
    self->clip_region = cairo_region_copy (clip_region);
}

static void
meta_shaped_texture_reset_pipelines (MetaShapedTexture *self)
{
  g_clear_pointer (&self->base_pipeline, cogl_object_unref);
  g_clear_pointer (&self->masked_pipeline, cogl_object_unref);
  g_clear_pointer (&self->unblended_pipeline, cogl_object_unref);
}

static void
meta_shaped_texture_dispose (GObject *object)
{
  MetaShapedTexture *self = (MetaShapedTexture *) object;

  if (self->remipmap_timeout_id)
    {
      g_source_remove (self->remipmap_timeout_id);
      self->remipmap_timeout_id = 0;
    }

  if (self->paint_tower)
    meta_texture_tower_free (self->paint_tower);
  self->paint_tower = NULL;

  g_clear_pointer (&self->texture, cogl_object_unref);
  g_clear_pointer (&self->opaque_region, cairo_region_destroy);

  meta_shaped_texture_set_mask_texture (self, NULL);
  set_unobscured_region (self, NULL);
  set_clip_region (self, NULL);

  meta_shaped_texture_reset_pipelines (self);

  g_clear_pointer (&self->snippet, cogl_object_unref);

  G_OBJECT_CLASS (meta_shaped_texture_parent_class)->dispose (object);
}

static CoglPipeline *
get_base_pipeline (MetaShapedTexture *stex,
                   CoglContext       *ctx)
{
  CoglPipeline *pipeline;

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
  if (!stex->is_y_inverted)
    {
      CoglMatrix matrix;

      cogl_matrix_init_identity (&matrix);
      cogl_matrix_scale (&matrix, 1, -1, 1);
      cogl_matrix_translate (&matrix, 0, -1, 0);
      cogl_pipeline_set_layer_matrix (pipeline, 0, &matrix);
    }

  if (stex->transform != META_MONITOR_TRANSFORM_NORMAL)
    {
      CoglMatrix matrix;
      CoglEuler euler;

      cogl_matrix_init_translation (&matrix, 0.5, 0.5, 0.0);
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

      cogl_pipeline_set_layer_matrix (pipeline, 0, &matrix);
      cogl_pipeline_set_layer_matrix (pipeline, 1, &matrix);
    }

  if (stex->snippet)
    cogl_pipeline_add_layer_snippet (pipeline, 0, stex->snippet);

  stex->base_pipeline = pipeline;

  return stex->base_pipeline;
}

static CoglPipeline *
get_unmasked_pipeline (MetaShapedTexture *self,
                       CoglContext       *ctx)
{
  return get_base_pipeline (self, ctx);
}

static CoglPipeline *
get_masked_pipeline (MetaShapedTexture *self,
                     CoglContext       *ctx)
{
  CoglPipeline *pipeline;

  if (self->masked_pipeline)
    return self->masked_pipeline;

  pipeline = cogl_pipeline_copy (get_base_pipeline (self, ctx));
  cogl_pipeline_set_layer_combine (pipeline, 1,
                                   "RGBA = MODULATE (PREVIOUS, TEXTURE[A])",
                                   NULL);

  self->masked_pipeline = pipeline;

  return pipeline;
}

static CoglPipeline *
get_unblended_pipeline (MetaShapedTexture *self,
                        CoglContext       *ctx)
{
  CoglPipeline *pipeline;
  CoglColor color;

  if (self->unblended_pipeline)
    return self->unblended_pipeline;

  pipeline = cogl_pipeline_copy (get_base_pipeline (self, ctx));
  cogl_color_init_from_4ub (&color, 255, 255, 255, 255);
  cogl_pipeline_set_blend (pipeline,
                           "RGBA = ADD (SRC_COLOR, 0)",
                           NULL);
  cogl_pipeline_set_color (pipeline, &color);

  self->unblended_pipeline = pipeline;

  return pipeline;
}

static void
paint_clipped_rectangle (CoglFramebuffer       *fb,
                         CoglPipeline          *pipeline,
                         cairo_rectangle_int_t *rect,
                         ClutterActorBox       *alloc)
{
  float coords[8];
  float x1, y1, x2, y2;

  x1 = rect->x;
  y1 = rect->y;
  x2 = rect->x + rect->width;
  y2 = rect->y + rect->height;

  coords[0] = rect->x / (alloc->x2 - alloc->x1);
  coords[1] = rect->y / (alloc->y2 - alloc->y1);
  coords[2] = (rect->x + rect->width) / (alloc->x2 - alloc->x1);
  coords[3] = (rect->y + rect->height) / (alloc->y2 - alloc->y1);

  coords[4] = coords[0];
  coords[5] = coords[1];
  coords[6] = coords[2];
  coords[7] = coords[3];

  cogl_framebuffer_draw_multitextured_rectangle (fb, pipeline,
                                                 x1, y1, x2, y2,
                                                 &coords[0], 8);
}

static void
set_cogl_texture (MetaShapedTexture *self,
                  CoglTexture       *cogl_tex)
{
  int width, height;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (self));

  if (self->texture)
    cogl_object_unref (self->texture);

  self->texture = cogl_tex;

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

  if (self->tex_width != width ||
      self->tex_height != height)
    {
      self->tex_width = width;
      self->tex_height = height;
      update_size (self);
    }

  /* NB: We don't queue a redraw of the actor here because we don't
   * know how much of the buffer has changed with respect to the
   * previous buffer. We only queue a redraw in response to surface
   * damage. */

  if (self->create_mipmaps)
    meta_texture_tower_set_base_texture (self->paint_tower, cogl_tex);
}

static gboolean
texture_is_idle_and_not_mipmapped (gpointer user_data)
{
  MetaShapedTexture *self = META_SHAPED_TEXTURE (user_data);

  if ((g_get_monotonic_time () - self->earliest_remipmap) < 0)
    return G_SOURCE_CONTINUE;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
  self->remipmap_timeout_id = 0;

  return G_SOURCE_REMOVE;
}

static void
meta_shaped_texture_paint (ClutterActor *actor)
{
  MetaShapedTexture *self = (MetaShapedTexture *) actor;
  double tex_scale;
  int dst_width, dst_height;
  cairo_rectangle_int_t tex_rect;
  guchar opacity;
  gboolean use_opaque_region;
  cairo_region_t *clip_tex_region;
  cairo_region_t *opaque_tex_region;
  cairo_region_t *blended_tex_region;
  CoglContext *ctx;
  CoglFramebuffer *fb;
  CoglTexture *paint_tex = NULL;
  ClutterActorBox alloc;
  CoglPipelineFilter filter;
  gint64 now = g_get_monotonic_time ();

  if (self->clip_region && cairo_region_is_empty (self->clip_region))
    return;

  if (!CLUTTER_ACTOR_IS_REALIZED (CLUTTER_ACTOR (self)))
    clutter_actor_realize (CLUTTER_ACTOR (self));

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
  if (self->create_mipmaps && self->last_invalidation)
    {
      gint64 age = now - self->last_invalidation;

      if (age >= MIN_MIPMAP_AGE_USEC ||
          self->fast_updates < MIN_FAST_UPDATES_BEFORE_UNMIPMAP)
        paint_tex = meta_texture_tower_get_paint_texture (self->paint_tower);
    }

  if (paint_tex == NULL)
    {
      paint_tex = COGL_TEXTURE (self->texture);

      if (paint_tex == NULL)
        return;

      if (self->create_mipmaps)
        {
          /* Minus 1000 to ensure we don't fail the age test in timeout */
          self->earliest_remipmap = now + MIN_MIPMAP_AGE_USEC - 1000;

          if (!self->remipmap_timeout_id)
            self->remipmap_timeout_id =
              g_timeout_add (MIN_MIPMAP_AGE_USEC / 1000,
                             texture_is_idle_and_not_mipmapped,
                             self);
        }
    }

  clutter_actor_get_scale (actor, &tex_scale, NULL);
  ensure_size_valid (self);
  dst_width = self->dst_width;

  dst_height = self->dst_height;
  if (dst_width == 0 || dst_height == 0) /* no contents yet */
    return;

  tex_rect = (cairo_rectangle_int_t) { 0, 0, dst_width, dst_height };

  /* Use nearest-pixel interpolation if the texture is unscaled. This
   * improves performance, especially with software rendering.
   */

  filter = COGL_PIPELINE_FILTER_LINEAR;

  if (meta_actor_painting_untransformed (dst_width, dst_height, NULL, NULL))
    filter = COGL_PIPELINE_FILTER_NEAREST;

  ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
  fb = cogl_get_draw_framebuffer ();

  opacity = clutter_actor_get_paint_opacity (actor);
  clutter_actor_get_allocation_box (actor, &alloc);

  if (self->opaque_region && opacity == 255)
    {
      opaque_tex_region =
        meta_region_scale_double (self->opaque_region,
                                  1.0 / tex_scale,
                                  META_ROUNDING_STRATEGY_SHRINK);
      use_opaque_region = TRUE;
    }
  else
    {
      opaque_tex_region = NULL;
      use_opaque_region = FALSE;
    }

  if (self->clip_region)
    {
      clip_tex_region =
        meta_region_scale_double (self->clip_region,
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
          opaque_pipeline = get_unblended_pipeline (self, ctx);
          cogl_pipeline_set_layer_texture (opaque_pipeline, 0, paint_tex);
          cogl_pipeline_set_layer_filters (opaque_pipeline, 0, filter, filter);

          n_rects = cairo_region_num_rectangles (region);
          for (i = 0; i < n_rects; i++)
            {
              cairo_rectangle_int_t rect;
              cairo_region_get_rectangle (region, i, &rect);
              paint_clipped_rectangle (fb, opaque_pipeline, &rect, &alloc);
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

      if (self->mask_texture == NULL)
        {
          blended_pipeline = get_unmasked_pipeline (self, ctx);
        }
      else
        {
          blended_pipeline = get_masked_pipeline (self, ctx);
          cogl_pipeline_set_layer_texture (blended_pipeline, 1, self->mask_texture);
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

              paint_clipped_rectangle (fb, blended_pipeline, &rect, &alloc);
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
effective_unobscured_region (MetaShapedTexture *self)
{
  ClutterActor *actor;

  /* Fail if we have any mapped clones. */
  actor = CLUTTER_ACTOR (self);
  do
    {
      if (clutter_actor_has_mapped_clones (actor))
        return NULL;
      actor = clutter_actor_get_parent (actor);
    }
  while (actor != NULL);

  return self->unobscured_region;
}

static gboolean
meta_shaped_texture_get_paint_volume (ClutterActor *actor,
                                      ClutterPaintVolume *volume)
{
  return clutter_paint_volume_set_from_allocation (volume, actor);
}

void
meta_shaped_texture_set_create_mipmaps (MetaShapedTexture *self,
					gboolean           create_mipmaps)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (self));

  create_mipmaps = create_mipmaps != FALSE;

  if (create_mipmaps != self->create_mipmaps)
    {
      CoglTexture *base_texture;
      self->create_mipmaps = create_mipmaps;
      base_texture = create_mipmaps ? self->texture : NULL;
      meta_texture_tower_set_base_texture (self->paint_tower, base_texture);
    }
}

void
meta_shaped_texture_set_mask_texture (MetaShapedTexture *self,
                                      CoglTexture       *mask_texture)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (self));

  g_clear_pointer (&self->mask_texture, cogl_object_unref);

  if (mask_texture != NULL)
    {
      self->mask_texture = mask_texture;
      cogl_object_ref (self->mask_texture);
    }

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

gboolean
meta_shaped_texture_is_obscured (MetaShapedTexture *self)
{
  cairo_region_t *unobscured_region = effective_unobscured_region (self);

  if (unobscured_region)
    return cairo_region_is_empty (unobscured_region);
  else
    return FALSE;
}

/**
 * meta_shaped_texture_update_area:
 * @self: #MetaShapedTexture
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
meta_shaped_texture_update_area (MetaShapedTexture *self,
				 int                x,
				 int                y,
				 int                width,
				 int                height)
{
  cairo_region_t *unobscured_region;
  const cairo_rectangle_int_t clip = { x, y, width, height };

  if (self->texture == NULL)
    return FALSE;

  meta_texture_tower_update_area (self->paint_tower, x, y, width, height);

  self->prev_invalidation = self->last_invalidation;
  self->last_invalidation = g_get_monotonic_time ();

  if (self->prev_invalidation)
    {
      gint64 interval = self->last_invalidation - self->prev_invalidation;
      gboolean fast_update = interval < MIN_MIPMAP_AGE_USEC;

      if (!fast_update)
        self->fast_updates = 0;
      else if (self->fast_updates < MIN_FAST_UPDATES_BEFORE_UNMIPMAP)
        self->fast_updates++;
    }

  unobscured_region = effective_unobscured_region (self);
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
          clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (self), &damage_rect);
          cairo_region_destroy (intersection);
          return TRUE;
        }

      cairo_region_destroy (intersection);
      return FALSE;
    }
  else
    {
      clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (self), &clip);
      return TRUE;
    }
}

/**
 * meta_shaped_texture_set_texture:
 * @self: The #MetaShapedTexture
 * @pixmap: The #CoglTexture to display
 */
void
meta_shaped_texture_set_texture (MetaShapedTexture *self,
                                 CoglTexture       *texture)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (self));

  set_cogl_texture (self, texture);
}

/**
 * meta_shaped_texture_set_is_y_inverted: (skip)
 */
void
meta_shaped_texture_set_is_y_inverted (MetaShapedTexture *self,
                                       gboolean           is_y_inverted)
{
  if (self->is_y_inverted == is_y_inverted)
    return;

  meta_shaped_texture_reset_pipelines (self);

  self->is_y_inverted = is_y_inverted;
}

/**
 * meta_shaped_texture_set_snippet: (skip)
 */
void
meta_shaped_texture_set_snippet (MetaShapedTexture *self,
                                 CoglSnippet       *snippet)
{
  if (self->snippet == snippet)
    return;

  meta_shaped_texture_reset_pipelines (self);

  g_clear_pointer (&self->snippet, cogl_object_unref);
  if (snippet)
    self->snippet = cogl_object_ref (snippet);
}

/**
 * meta_shaped_texture_get_texture:
 * @self: The #MetaShapedTexture
 *
 * Returns: (transfer none): the unshaped texture
 */
CoglTexture *
meta_shaped_texture_get_texture (MetaShapedTexture *self)
{
  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (self), NULL);
  return COGL_TEXTURE (self->texture);
}

/**
 * meta_shaped_texture_set_opaque_region:
 * @self: a #MetaShapedTexture
 * @opaque_region: (transfer full): the region of the texture that
 *   can have blending turned off.
 *
 * As most windows have a large portion that does not require blending,
 * we can easily turn off blending if we know the areas that do not
 * require blending. This sets the region where we will not blend for
 * optimization purposes.
 */
void
meta_shaped_texture_set_opaque_region (MetaShapedTexture *self,
                                       cairo_region_t    *opaque_region)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (self));

  if (self->opaque_region)
    cairo_region_destroy (self->opaque_region);

  if (opaque_region)
    self->opaque_region = cairo_region_reference (opaque_region);
  else
    self->opaque_region = NULL;
}

cairo_region_t *
meta_shaped_texture_get_opaque_region (MetaShapedTexture *self)
{
  return self->opaque_region;
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

/**
 * meta_shaped_texture_get_image:
 * @self: A #MetaShapedTexture
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
meta_shaped_texture_get_image (MetaShapedTexture     *self,
                               cairo_rectangle_int_t *clip)
{
  CoglTexture *texture, *mask_texture;
  cairo_rectangle_int_t texture_rect = { 0, 0, 0, 0 };
  cairo_surface_t *surface;

  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (self), NULL);

  texture = COGL_TEXTURE (self->texture);

  if (texture == NULL)
    return NULL;

  texture_rect.width = cogl_texture_get_width (texture);
  texture_rect.height = cogl_texture_get_height (texture);

  if (clip != NULL)
    {
      /* GdkRectangle is just a typedef of cairo_rectangle_int_t,
       * so we can use the gdk_rectangle_* APIs on these. */
      if (!gdk_rectangle_intersect (&texture_rect, clip, clip))
        return NULL;
    }

  if (clip != NULL)
    texture = cogl_texture_new_from_sub_texture (texture,
                                                 clip->x,
                                                 clip->y,
                                                 clip->width,
                                                 clip->height);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        cogl_texture_get_width (texture),
                                        cogl_texture_get_height (texture));

  cogl_texture_get_data (texture, CLUTTER_CAIRO_FORMAT_ARGB32,
                         cairo_image_surface_get_stride (surface),
                         cairo_image_surface_get_data (surface));

  cairo_surface_mark_dirty (surface);

  if (clip != NULL)
    cogl_object_unref (texture);

  mask_texture = self->mask_texture;
  if (mask_texture != NULL)
    {
      cairo_t *cr;
      cairo_surface_t *mask_surface;

      if (clip != NULL)
        mask_texture = cogl_texture_new_from_sub_texture (mask_texture,
                                                          clip->x,
                                                          clip->y,
                                                          clip->width,
                                                          clip->height);

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

      if (clip != NULL)
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
  MetaShapedTexture *self = META_SHAPED_TEXTURE (cullable);

  set_unobscured_region (self, unobscured_region);
  set_clip_region (self, clip_region);

  if (clutter_actor_get_paint_opacity (CLUTTER_ACTOR (self)) == 0xff)
    {
      if (self->opaque_region)
        {
          if (unobscured_region)
            cairo_region_subtract (unobscured_region, self->opaque_region);
          if (clip_region)
            cairo_region_subtract (clip_region, self->opaque_region);
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
