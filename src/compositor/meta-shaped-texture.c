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

#include <config.h>

#include <meta/meta-shaped-texture.h>
#include "meta-shaped-texture-private.h"
#include "meta-texture-rectangle.h"

#include <cogl/cogl.h>
#include <gdk/gdk.h> /* for gdk_rectangle_intersect() */

#include "clutter-utils.h"
#include "meta-texture-tower.h"
#include "core/boxes-private.h"

#include "meta-cullable.h"
#include <meta/meta-backend.h>

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

static void disable_backing_store (MetaShapedTexture *stex);

static void cullable_iface_init (MetaCullableInterface *iface);

static gboolean meta_debug_show_backing_store = FALSE;

G_DEFINE_TYPE_WITH_CODE (MetaShapedTexture, meta_shaped_texture, CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init));

#define META_SHAPED_TEXTURE_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), META_TYPE_SHAPED_TEXTURE, \
                                MetaShapedTexturePrivate))

enum {
  SIZE_CHANGED,

  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

typedef struct
{
  CoglTexture *texture;
  CoglTexture *mask_texture;
  cairo_surface_t *mask_surface;
  cairo_region_t *region;
} MetaTextureBackingStore;

struct _MetaShapedTexturePrivate
{
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

  /* textures get corrupted on suspend, so save them */
  cairo_surface_t *saved_base_surface;
  cairo_surface_t *saved_mask_surface;

  /* We can't just restore external textures, so we need to track
   * which parts of the external texture are freshly drawn from
   * the client after corruption, and fill in the rest from our
   * saved snapshot */
  MetaTextureBackingStore *backing_store;

  guint tex_width, tex_height;
  guint fallback_width, fallback_height;

  guint create_mipmaps : 1;
};

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

  g_type_class_add_private (klass, sizeof (MetaShapedTexturePrivate));

  if (g_getenv ("MUTTER_DEBUG_BACKING_STORE"))
    meta_debug_show_backing_store = TRUE;
}

static void
meta_shaped_texture_init (MetaShapedTexture *self)
{
  MetaShapedTexturePrivate *priv;
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = clutter_get_default_backend ();
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);

  priv = self->priv = META_SHAPED_TEXTURE_GET_PRIVATE (self);

  priv->paint_tower = meta_texture_tower_new ();

  priv->texture = NULL;
  priv->mask_texture = NULL;
  priv->create_mipmaps = TRUE;
  priv->is_y_inverted = TRUE;

  if (cogl_has_feature (cogl_context, COGL_FEATURE_ID_UNSTABLE_TEXTURES))
    {
      g_signal_connect_object (backend, "suspending", G_CALLBACK (meta_shaped_texture_save), self, G_CONNECT_SWAPPED);
      g_signal_connect_object (backend, "resuming", G_CALLBACK (meta_shaped_texture_restore), self, G_CONNECT_SWAPPED);
    }
}

static void
set_unobscured_region (MetaShapedTexture *self,
                       cairo_region_t    *unobscured_region)
{
  MetaShapedTexturePrivate *priv = self->priv;

  g_clear_pointer (&priv->unobscured_region, (GDestroyNotify) cairo_region_destroy);
  if (unobscured_region)
    {
      guint width, height;

      if (priv->texture)
        {
          width = priv->tex_width;
          height = priv->tex_height;
        }
      else
        {
          width = priv->fallback_width;
          height = priv->fallback_height;
        }

      cairo_rectangle_int_t bounds = { 0, 0, width, height };
      priv->unobscured_region = cairo_region_copy (unobscured_region);
      cairo_region_intersect_rectangle (priv->unobscured_region, &bounds);
    }
}

static void
set_clip_region (MetaShapedTexture *self,
                 cairo_region_t    *clip_region)
{
  MetaShapedTexturePrivate *priv = self->priv;

  g_clear_pointer (&priv->clip_region, (GDestroyNotify) cairo_region_destroy);
  if (clip_region)
    priv->clip_region = cairo_region_copy (clip_region);
}

static void
meta_shaped_texture_reset_pipelines (MetaShapedTexture *stex)
{
  MetaShapedTexturePrivate *priv = stex->priv;

  g_clear_pointer (&priv->base_pipeline, cogl_object_unref);
  g_clear_pointer (&priv->masked_pipeline, cogl_object_unref);
  g_clear_pointer (&priv->unblended_pipeline, cogl_object_unref);
}

static void
meta_shaped_texture_dispose (GObject *object)
{
  MetaShapedTexture *self = (MetaShapedTexture *) object;
  MetaShapedTexturePrivate *priv = self->priv;

  if (priv->paint_tower)
    meta_texture_tower_free (priv->paint_tower);
  priv->paint_tower = NULL;

  g_clear_pointer (&priv->texture, cogl_object_unref);
  g_clear_pointer (&priv->opaque_region, cairo_region_destroy);

  meta_shaped_texture_set_mask_texture (self, NULL);
  set_unobscured_region (self, NULL);
  set_clip_region (self, NULL);

  meta_shaped_texture_reset_pipelines (self);

  g_clear_pointer (&priv->snippet, cogl_object_unref);

  G_OBJECT_CLASS (meta_shaped_texture_parent_class)->dispose (object);
}

static int
get_layer_indices (MetaShapedTexture *stex,
                   int               *main_layer_index,
                   int               *backing_mask_layer_index,
                   int               *backing_layer_index,
                   int               *mask_layer_index)
{
  MetaShapedTexturePrivate *priv = stex->priv;
  int next_layer_index = 0;

  if (main_layer_index)
    *main_layer_index = next_layer_index;

  next_layer_index++;

  if (priv->backing_store)
    {
      if (backing_mask_layer_index)
        *backing_mask_layer_index = next_layer_index;
      next_layer_index++;
      if (backing_layer_index)
        *backing_layer_index = next_layer_index;
      next_layer_index++;
    }
  else
    {
      if (backing_mask_layer_index)
        *backing_mask_layer_index = -1;
      if (backing_layer_index)
        *backing_layer_index = -1;
    }

  if (mask_layer_index)
    *mask_layer_index = next_layer_index;

  return next_layer_index;
}

static CoglPipeline *
get_base_pipeline (MetaShapedTexture *stex,
                   CoglContext       *ctx)
{
  MetaShapedTexturePrivate *priv = stex->priv;
  CoglPipeline *pipeline;
  int main_layer_index;
  int backing_layer_index;
  int backing_mask_layer_index;
  int i, number_of_layers;

  if (priv->base_pipeline)
    return priv->base_pipeline;

  pipeline = cogl_pipeline_new (ctx);

  number_of_layers = get_layer_indices (stex,
                                        &main_layer_index,
                                        &backing_mask_layer_index,
                                        &backing_layer_index,
                                        NULL);

  for (i = 0; i < number_of_layers; i++)
    {
      cogl_pipeline_set_layer_wrap_mode_s (pipeline, i,
                                           COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
      cogl_pipeline_set_layer_wrap_mode_t (pipeline, i,
                                           COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
    }

  if (!priv->is_y_inverted)
    {
      CoglMatrix matrix;

      cogl_matrix_init_identity (&matrix);
      cogl_matrix_scale (&matrix, 1, -1, 1);
      cogl_matrix_translate (&matrix, 0, -1, 0);
      cogl_pipeline_set_layer_matrix (pipeline, main_layer_index, &matrix);
    }

  if (priv->backing_store)
    {
      g_autofree char *backing_description = NULL;
      cogl_pipeline_set_layer_combine (pipeline, backing_mask_layer_index,
                                       "RGBA = REPLACE(PREVIOUS)",
                                       NULL);
      backing_description = g_strdup_printf ("RGBA = INTERPOLATE(PREVIOUS, TEXTURE_%d, TEXTURE_%d[A])",
                                             backing_layer_index,
                                             backing_mask_layer_index);
      cogl_pipeline_set_layer_combine (pipeline,
                                       backing_layer_index,
                                       backing_description,
                                       NULL);
    }

  if (priv->snippet)
    cogl_pipeline_add_layer_snippet (pipeline, main_layer_index, priv->snippet);

  priv->base_pipeline = pipeline;

  return priv->base_pipeline;
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
  MetaShapedTexturePrivate *priv = stex->priv;
  CoglPipeline *pipeline;
  int mask_layer_index;

  if (priv->masked_pipeline)
    return priv->masked_pipeline;

  get_layer_indices (stex, NULL, NULL, NULL, &mask_layer_index);

  pipeline = cogl_pipeline_copy (get_base_pipeline (stex, ctx));
  cogl_pipeline_set_layer_combine (pipeline, mask_layer_index,
                                   "RGBA = MODULATE (PREVIOUS, TEXTURE[A])",
                                   NULL);

  priv->masked_pipeline = pipeline;

  return pipeline;
}

static CoglPipeline *
get_unblended_pipeline (MetaShapedTexture *stex,
                        CoglContext       *ctx)
{
  MetaShapedTexturePrivate *priv = stex->priv;
  CoglPipeline *pipeline;
  CoglColor color;

  if (priv->unblended_pipeline)
    return priv->unblended_pipeline;

  pipeline = cogl_pipeline_copy (get_base_pipeline (stex, ctx));
  cogl_color_init_from_4ub (&color, 255, 255, 255, 255);
  cogl_pipeline_set_blend (pipeline,
                           "RGBA = ADD (SRC_COLOR, 0)",
                           NULL);
  cogl_pipeline_set_color (pipeline, &color);

  priv->unblended_pipeline = pipeline;

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
set_cogl_texture (MetaShapedTexture *stex,
                  CoglTexture       *cogl_tex)
{
  MetaShapedTexturePrivate *priv;
  guint width, height;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  priv = stex->priv;

  if (priv->texture)
    cogl_object_unref (priv->texture);

  g_clear_pointer (&priv->saved_base_surface, cairo_surface_destroy);

  priv->texture = cogl_tex;

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

  if (priv->tex_width != width ||
      priv->tex_height != height)
    {
      priv->tex_width = width;
      priv->tex_height = height;
      meta_shaped_texture_set_mask_texture (stex, NULL);
      clutter_actor_queue_relayout (CLUTTER_ACTOR (stex));
      g_signal_emit (stex, signals[SIZE_CHANGED], 0);
    }

  /* NB: We don't queue a redraw of the actor here because we don't
   * know how much of the buffer has changed with respect to the
   * previous buffer. We only queue a redraw in response to surface
   * damage. */

  if (priv->create_mipmaps)
    meta_texture_tower_set_base_texture (priv->paint_tower, cogl_tex);
}

static void
do_paint (MetaShapedTexture *stex,
          CoglFramebuffer   *fb,
          CoglTexture       *paint_tex,
          cairo_region_t    *clip_region)
{
  MetaShapedTexturePrivate *priv = stex->priv;
  guint tex_width, tex_height;
  guchar opacity;
  CoglContext *ctx;
  ClutterActorBox alloc;
  CoglPipelineFilter filter;
  int main_layer_index;
  int backing_mask_layer_index;
  int backing_layer_index;
  int mask_layer_index;

  tex_width = priv->tex_width;
  tex_height = priv->tex_height;

  if (tex_width == 0 || tex_height == 0) /* no contents yet */
    return;

  cairo_rectangle_int_t tex_rect = { 0, 0, tex_width, tex_height };

  /* Use nearest-pixel interpolation if the texture is unscaled. This
   * improves performance, especially with software rendering.
   */

  filter = COGL_PIPELINE_FILTER_LINEAR;

  if (meta_actor_painting_untransformed (fb,
                                         tex_width, tex_height,
                                         NULL, NULL))
    filter = COGL_PIPELINE_FILTER_NEAREST;

  ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());

  opacity = clutter_actor_get_paint_opacity (CLUTTER_ACTOR (stex));
  clutter_actor_get_allocation_box (CLUTTER_ACTOR (stex), &alloc);

  cairo_region_t *blended_region;
  gboolean use_opaque_region = (priv->opaque_region != NULL && opacity == 255);

  if (use_opaque_region)
    {
      if (priv->clip_region != NULL)
        blended_region = cairo_region_copy (priv->clip_region);
      else
        blended_region = cairo_region_create_rectangle (&tex_rect);

      cairo_region_subtract (blended_region, priv->opaque_region);
    }
  else
    {
      if (priv->clip_region != NULL)
        blended_region = cairo_region_reference (priv->clip_region);
      else
        blended_region = NULL;
    }

  /* Limit to how many separate rectangles we'll draw; beyond this just
   * fall back and draw the whole thing */
#define MAX_RECTS 16

  if (blended_region != NULL)
    {
      int n_rects = cairo_region_num_rectangles (blended_region);
      if (n_rects > MAX_RECTS)
        {
          /* Fall back to taking the fully blended path. */
          use_opaque_region = FALSE;

          cairo_region_destroy (blended_region);
          blended_region = NULL;
        }
    }

  get_layer_indices (stex,
                     &main_layer_index,
                     &backing_mask_layer_index,
                     &backing_layer_index,
                     &mask_layer_index);

  /* First, paint the unblended parts, which are part of the opaque region. */
  if (use_opaque_region)
    {
      CoglPipeline *opaque_pipeline;
      cairo_region_t *region;
      int n_rects;
      int i;

      if (priv->clip_region != NULL)
        {
          region = cairo_region_copy (priv->clip_region);
          cairo_region_intersect (region, priv->opaque_region);
        }
      else
        {
          region = cairo_region_reference (priv->opaque_region);
        }

      if (!cairo_region_is_empty (region))
        {
          opaque_pipeline = get_unblended_pipeline (stex, ctx);
          cogl_pipeline_set_layer_texture (opaque_pipeline, main_layer_index, paint_tex);
          cogl_pipeline_set_layer_filters (opaque_pipeline, main_layer_index, filter, filter);

          if (priv->backing_store)
            {
              cogl_pipeline_set_layer_texture (opaque_pipeline,
                                               backing_mask_layer_index,
                                               priv->backing_store->mask_texture);
              cogl_pipeline_set_layer_filters (opaque_pipeline,
                                               backing_mask_layer_index,
                                               filter, filter);
              cogl_pipeline_set_layer_texture (opaque_pipeline,
                                               backing_layer_index,
                                               priv->backing_store->texture);
              cogl_pipeline_set_layer_filters (opaque_pipeline,
                                               backing_layer_index,
                                               filter, filter);
            }

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
   *   1) blended_region has rectangles - paint the rectangles.
   *   2) blended_region is empty - don't paint anything
   *   3) blended_region is NULL - paint fully-blended.
   *
   *   1) and 3) are the times where we have to paint stuff. This tests
   *   for 1) and 3).
   */
  if (blended_region == NULL || !cairo_region_is_empty (blended_region))
    {
      CoglPipeline *blended_pipeline;

      if (priv->mask_texture == NULL)
        {
          blended_pipeline = get_unmasked_pipeline (stex, ctx);
        }
      else
        {
          blended_pipeline = get_masked_pipeline (stex, ctx);
          cogl_pipeline_set_layer_texture (blended_pipeline, mask_layer_index, priv->mask_texture);
          cogl_pipeline_set_layer_filters (blended_pipeline, mask_layer_index, filter, filter);
        }

      cogl_pipeline_set_layer_texture (blended_pipeline, main_layer_index, paint_tex);
      cogl_pipeline_set_layer_filters (blended_pipeline, main_layer_index, filter, filter);

      if (priv->backing_store)
        {
          cogl_pipeline_set_layer_texture (blended_pipeline,
                                           backing_mask_layer_index,
                                           priv->backing_store->mask_texture);
          cogl_pipeline_set_layer_filters (blended_pipeline,
                                           backing_mask_layer_index,
                                           filter, filter);
          cogl_pipeline_set_layer_texture (blended_pipeline,
                                           backing_layer_index,
                                           priv->backing_store->texture);
          cogl_pipeline_set_layer_filters (blended_pipeline,
                                           backing_layer_index,
                                           filter, filter);
        }

      CoglColor color;
      cogl_color_init_from_4ub (&color, opacity, opacity, opacity, opacity);
      cogl_pipeline_set_color (blended_pipeline, &color);

      if (blended_region != NULL)
        {
          /* 1) blended_region is not empty. Paint the rectangles. */
          int i;
          int n_rects = cairo_region_num_rectangles (blended_region);

          for (i = 0; i < n_rects; i++)
            {
              cairo_rectangle_int_t rect;
              cairo_region_get_rectangle (blended_region, i, &rect);

              if (!gdk_rectangle_intersect (&tex_rect, &rect, &rect))
                continue;

              paint_clipped_rectangle (fb, blended_pipeline, &rect, &alloc);
            }
        }
      else
        {
          /* 3) blended_region is NULL. Do a full paint. */
          cogl_framebuffer_draw_rectangle (fb, blended_pipeline,
                                           0, 0,
                                           alloc.x2 - alloc.x1,
                                           alloc.y2 - alloc.y1);
        }
    }

  if (blended_region != NULL)
    cairo_region_destroy (blended_region);
}

static void
meta_shaped_texture_paint (ClutterActor *actor)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (actor);
  MetaShapedTexturePrivate *priv = stex->priv;
  CoglTexture *paint_tex = NULL;
  CoglFramebuffer *fb;

  if (!priv->texture)
    return;

  if (priv->clip_region && cairo_region_is_empty (priv->clip_region))
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
  if (priv->create_mipmaps)
    paint_tex = meta_texture_tower_get_paint_texture (priv->paint_tower);

  if (!paint_tex)
    paint_tex = COGL_TEXTURE (priv->texture);

  if (cogl_texture_get_width (paint_tex) == 0 ||
      cogl_texture_get_height (paint_tex) == 0)
    return;

  fb = cogl_get_draw_framebuffer ();
  do_paint (META_SHAPED_TEXTURE (actor), fb, paint_tex, priv->clip_region);
}

static void
meta_shaped_texture_get_preferred_width (ClutterActor *self,
                                         gfloat        for_height,
                                         gfloat       *min_width_p,
                                         gfloat       *natural_width_p)
{
  MetaShapedTexturePrivate *priv = META_SHAPED_TEXTURE (self)->priv;
  guint width;

  if (priv->texture)
    width = priv->tex_width;
  else
    width = priv->fallback_width;

  if (min_width_p)
    *min_width_p = width;
  if (natural_width_p)
    *natural_width_p = width;
}

static void
meta_shaped_texture_get_preferred_height (ClutterActor *self,
                                          gfloat        for_width,
                                          gfloat       *min_height_p,
                                          gfloat       *natural_height_p)
{
  MetaShapedTexturePrivate *priv = META_SHAPED_TEXTURE (self)->priv;
  guint height;

  if (priv->texture)
    height = priv->tex_height;
  else
    height = priv->fallback_height;

  if (min_height_p)
    *min_height_p = height;
  if (natural_height_p)
    *natural_height_p = height;
}

static cairo_region_t *
effective_unobscured_region (MetaShapedTexture *self)
{
  MetaShapedTexturePrivate *priv = self->priv;
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

  return priv->unobscured_region;
}

static gboolean
get_unobscured_bounds (MetaShapedTexture     *self,
                       cairo_rectangle_int_t *unobscured_bounds)
{
  cairo_region_t *unobscured_region = effective_unobscured_region (self);

  if (unobscured_region)
    {
      cairo_region_get_extents (unobscured_region, unobscured_bounds);
      return TRUE;
    }
  else
    return FALSE;
}

static gboolean
meta_shaped_texture_get_paint_volume (ClutterActor *actor,
                                      ClutterPaintVolume *volume)
{
  MetaShapedTexture *self = META_SHAPED_TEXTURE (actor);
  ClutterActorBox box;
  cairo_rectangle_int_t unobscured_bounds;

  if (!clutter_actor_has_allocation (actor))
    return FALSE;

  clutter_actor_get_allocation_box (actor, &box);

  if (get_unobscured_bounds (self, &unobscured_bounds))
    {
      box.x1 = MAX (unobscured_bounds.x, box.x1);
      box.x2 = MIN (unobscured_bounds.x + unobscured_bounds.width, box.x2);
      box.y1 = MAX (unobscured_bounds.y, box.y1);
      box.y2 = MIN (unobscured_bounds.y + unobscured_bounds.height, box.y2);
    }
  box.x2 = MAX (box.x2, box.x1);
  box.y2 = MAX (box.y2, box.y1);

  clutter_paint_volume_union_box (volume, &box);
  return TRUE;
}

void
meta_shaped_texture_set_create_mipmaps (MetaShapedTexture *stex,
					gboolean           create_mipmaps)
{
  MetaShapedTexturePrivate *priv;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  priv = stex->priv;

  create_mipmaps = create_mipmaps != FALSE;

  if (create_mipmaps != priv->create_mipmaps)
    {
      CoglTexture *base_texture;
      priv->create_mipmaps = create_mipmaps;
      base_texture = create_mipmaps ? priv->texture : NULL;
      meta_texture_tower_set_base_texture (priv->paint_tower, base_texture);
    }
}

void
meta_shaped_texture_set_mask_texture (MetaShapedTexture *stex,
                                      CoglTexture       *mask_texture)
{
  MetaShapedTexturePrivate *priv;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  priv = stex->priv;

  g_clear_pointer (&priv->mask_texture, cogl_object_unref);
  g_clear_pointer (&priv->saved_mask_surface, cairo_surface_destroy);

  if (mask_texture != NULL)
    {
      priv->mask_texture = mask_texture;
      cogl_object_ref (priv->mask_texture);
    }

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stex));
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

static void
meta_texture_backing_store_redraw_mask (MetaTextureBackingStore *backing_store)
{
  CoglError *error = NULL;

  if (!cogl_texture_set_data (backing_store->mask_texture, COGL_PIXEL_FORMAT_A_8,
                              cairo_image_surface_get_stride (backing_store->mask_surface),
                              cairo_image_surface_get_data (backing_store->mask_surface), 0,
                              &error))
    {

      g_warning ("Failed to update backing mask texture");
      g_clear_pointer (&error, cogl_error_free);
    }
}

static gboolean
meta_texture_backing_store_shrink (MetaTextureBackingStore     *backing_store,
                                   const cairo_rectangle_int_t *area)
{
  cairo_t *cr;

  cairo_region_subtract_rectangle (backing_store->region, area);

  /* If the client has finally redrawn the entire surface, we can
   * ditch our snapshot
   */
  if (cairo_region_is_empty (backing_store->region))
    return FALSE;

  cr = cairo_create (backing_store->mask_surface);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  gdk_cairo_region (cr, backing_store->region);
  cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
  cairo_fill (cr);
  cairo_destroy (cr);

  meta_texture_backing_store_redraw_mask (backing_store);

  return TRUE;
}

static void
shrink_backing_region (MetaShapedTexture           *stex,
                       const cairo_rectangle_int_t *area)
{
  MetaShapedTexturePrivate *priv = stex->priv;
  gboolean still_backing_texture;

  if (!priv->backing_store)
    return;

  still_backing_texture =
      meta_texture_backing_store_shrink (priv->backing_store, area);

  if (!still_backing_texture)
    disable_backing_store (stex);
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
  MetaShapedTexturePrivate *priv;
  cairo_region_t *unobscured_region;
  const cairo_rectangle_int_t clip = { x, y, width, height };

  priv = stex->priv;

  if (priv->texture == NULL)
    return FALSE;

  shrink_backing_region (stex, &clip);

  meta_texture_tower_update_area (priv->paint_tower, x, y, width, height);

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
  MetaShapedTexturePrivate *priv = stex->priv;

  if (priv->is_y_inverted == is_y_inverted)
    return;

  meta_shaped_texture_reset_pipelines (stex);

  priv->is_y_inverted = is_y_inverted;
}

/**
 * meta_shaped_texture_set_snippet: (skip)
 */
void
meta_shaped_texture_set_snippet (MetaShapedTexture *stex,
                                 CoglSnippet       *snippet)
{
  MetaShapedTexturePrivate *priv = stex->priv;

  if (priv->snippet == snippet)
    return;

  meta_shaped_texture_reset_pipelines (stex);

  g_clear_pointer (&priv->snippet, cogl_object_unref);
  if (snippet)
    priv->snippet = cogl_object_ref (snippet);
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
  return COGL_TEXTURE (stex->priv->texture);
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
  MetaShapedTexturePrivate *priv;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  priv = stex->priv;

  if (priv->opaque_region)
    cairo_region_destroy (priv->opaque_region);

  if (opaque_region)
    priv->opaque_region = cairo_region_reference (opaque_region);
  else
    priv->opaque_region = NULL;
}

cairo_region_t *
meta_shaped_texture_get_opaque_region (MetaShapedTexture *stex)
{
  MetaShapedTexturePrivate *priv = stex->priv;
  return priv->opaque_region;
}

static gboolean
should_get_via_offscreen (MetaShapedTexture *stex)
{
  MetaShapedTexturePrivate *priv = stex->priv;

  if (!cogl_texture_is_get_data_supported (priv->texture))
    return TRUE;

  return FALSE;
}

static cairo_surface_t *
get_image_via_offscreen (MetaShapedTexture      *stex,
                         cairo_rectangle_int_t  *clip,
                         CoglTexture           **texture)
{
  MetaShapedTexturePrivate *priv = stex->priv;
  ClutterBackend *clutter_backend = clutter_get_default_backend ();
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglTexture *image_texture;
  GError *error = NULL;
  CoglOffscreen *offscreen;
  CoglFramebuffer *fb;
  CoglMatrix projection_matrix;
  unsigned int fb_width, fb_height;
  cairo_rectangle_int_t fallback_clip;
  CoglColor clear_color;
  cairo_surface_t *surface;

  if (cogl_has_feature (cogl_context, COGL_FEATURE_ID_TEXTURE_NPOT))
    {
      fb_width = priv->tex_width;
      fb_height = priv->tex_height;
    }
  else
    {
      fb_width = clutter_util_next_p2 (priv->tex_width);
      fb_height = clutter_util_next_p2 (priv->tex_height);
    }

  if (!clip)
    {
      fallback_clip = (cairo_rectangle_int_t) {
        .width = priv->tex_width,
        .height = priv->tex_height,
      };
      clip = &fallback_clip;
    }

  image_texture =
    COGL_TEXTURE (cogl_texture_2d_new_with_size (cogl_context,
                                                 fb_width, fb_height));
  cogl_primitive_texture_set_auto_mipmap (COGL_PRIMITIVE_TEXTURE (image_texture),
                                          FALSE);
  if (!cogl_texture_allocate (COGL_TEXTURE (image_texture), &error))
    {
      g_error_free (error);
      cogl_object_unref (image_texture);
      return FALSE;
    }

  if (fb_width != priv->tex_width || fb_height != priv->tex_height)
    {
      CoglSubTexture *sub_texture;

      sub_texture = cogl_sub_texture_new (cogl_context,
                                          image_texture,
                                          0, 0,
                                          priv->tex_width, priv->tex_height);
      cogl_object_unref (image_texture);
      image_texture = COGL_TEXTURE (sub_texture);
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
                     1.0 / (priv->tex_width / 2.0),
                     -1.0 / (priv->tex_height / 2.0), 0);
  cogl_matrix_translate (&projection_matrix,
                         -(priv->tex_width / 2.0),
                         -(priv->tex_height / 2.0), 0);

  cogl_framebuffer_set_projection_matrix (fb, &projection_matrix);

  cogl_color_init_from_4ub (&clear_color, 0, 0, 0, 0);
  cogl_framebuffer_clear (fb, COGL_BUFFER_BIT_COLOR, &clear_color);

  do_paint (stex, fb, priv->texture, NULL);

  cogl_framebuffer_pop_matrix (fb);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        clip->width, clip->height);
  cogl_framebuffer_read_pixels (fb,
                                clip->x, clip->y,
                                clip->width, clip->height,
                                CLUTTER_CAIRO_FORMAT_ARGB32,
                                cairo_image_surface_get_data (surface));
  cairo_surface_mark_dirty (surface);

  if (texture)
    {
      *texture = cogl_object_ref (image_texture);

      if (G_UNLIKELY (meta_debug_show_backing_store))
        {
          cairo_t *cr;

          cr = cairo_create (surface);
          cairo_set_source_rgba (cr, 1.0, 0.0, 0.0, 0.75);
          cairo_paint (cr);
          cairo_destroy (cr);
        }

      cogl_texture_set_data (*texture, CLUTTER_CAIRO_FORMAT_ARGB32,
                             cairo_image_surface_get_stride (surface),
                             cairo_image_surface_get_data (surface), 0, NULL);
    }

  cogl_object_unref (fb);


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
  MetaShapedTexturePrivate *priv = stex->priv;
  cairo_rectangle_int_t *transformed_clip = NULL;
  CoglTexture *texture, *mask_texture;
  cairo_surface_t *surface;

  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), NULL);

  texture = COGL_TEXTURE (priv->texture);

  if (texture == NULL)
    return NULL;

  if (priv->tex_width == 0 || priv->tex_height == 0)
    return NULL;

  if (clip != NULL)
    {
      double tex_scale;
      cairo_rectangle_int_t tex_rect;

      transformed_clip = alloca (sizeof (cairo_rectangle_int_t));
      *transformed_clip = *clip;

      clutter_actor_get_scale (CLUTTER_ACTOR (stex), &tex_scale, NULL);
      meta_rectangle_scale_double (transformed_clip, 1.0 / tex_scale,
                                   META_ROUNDING_STRATEGY_GROW);

      tex_rect = (cairo_rectangle_int_t) {
        .width = priv->tex_width,
        .height = priv->tex_height,
      };

      if (!meta_rectangle_intersect (&tex_rect, transformed_clip,
                                     transformed_clip))
        return NULL;
    }

  if (should_get_via_offscreen (stex))
    return get_image_via_offscreen (stex, transformed_clip, NULL);

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

  mask_texture = priv->mask_texture;
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

static void
meta_texture_backing_store_free (MetaTextureBackingStore *backing_store)
{
  g_clear_pointer (&backing_store->texture, cogl_object_unref);
  g_clear_pointer (&backing_store->mask_texture, cogl_object_unref);
  g_clear_pointer (&backing_store->mask_surface, cairo_surface_destroy);
  g_clear_pointer (&backing_store->region, cairo_region_destroy);

  g_slice_free (MetaTextureBackingStore, backing_store);
}

static MetaTextureBackingStore *
meta_texture_backing_store_new (CoglTexture *texture)
{
  MetaTextureBackingStore *backing_store = NULL;
  ClutterBackend *backend = clutter_get_default_backend ();
  CoglContext *context = clutter_backend_get_cogl_context (backend);
  CoglTexture *mask_texture = NULL;
  guchar *mask_data;
  int width, height, stride;
  cairo_surface_t *surface;
  cairo_region_t *region;
  cairo_rectangle_int_t backing_rectangle;

  width = cogl_texture_get_width (texture);
  height = cogl_texture_get_height (texture);
  stride = cairo_format_stride_for_width (CAIRO_FORMAT_A8, width);

  /* we start off by only letting the backing texture through, and none of the real texture */
  backing_rectangle.x = 0;
  backing_rectangle.y = 0;
  backing_rectangle.width = width;
  backing_rectangle.height = height;

  region = cairo_region_create_rectangle (&backing_rectangle);

  /* initialize mask to transparent, so the entire backing store shows through
   * up front
   */
  mask_data = g_malloc0 (stride * height);
  surface = cairo_image_surface_create_for_data (mask_data,
                                                 CAIRO_FORMAT_A8,
                                                 width,
                                                 height,
                                                 stride);

  if (meta_texture_rectangle_check (texture))
    {
      mask_texture = COGL_TEXTURE (cogl_texture_rectangle_new_with_size (context,
                                                                         width,
                                                                         height));
      cogl_texture_set_components (mask_texture, COGL_TEXTURE_COMPONENTS_A);
      cogl_texture_set_region (mask_texture,
                               0, 0,
                               0, 0,
                               width, height,
                               width, height,
                               COGL_PIXEL_FORMAT_A_8,
                               stride, mask_data);
    }
  else
    {
      CoglError *error = NULL;

      mask_texture = COGL_TEXTURE (cogl_texture_2d_new_from_data (context, width, height,
                                                                  COGL_PIXEL_FORMAT_A_8,
                                                                  stride, mask_data, &error));

      if (error)
        {
          g_warning ("Failed to allocate mask texture: %s", error->message);
          cogl_error_free (error);
        }
    }

  if (mask_texture)
    {
      backing_store = g_slice_new0 (MetaTextureBackingStore);
      backing_store->texture = cogl_object_ref (texture);
      backing_store->mask_texture = mask_texture;
      backing_store->mask_surface = surface;
      backing_store->region = region;
    }

  return backing_store;
}

static void
enable_backing_store (MetaShapedTexture *stex,
                      CoglTexture       *texture)
{
  MetaShapedTexturePrivate *priv = stex->priv;

  g_clear_pointer (&priv->backing_store, meta_texture_backing_store_free);

  priv->backing_store = meta_texture_backing_store_new (texture);

  meta_shaped_texture_reset_pipelines (stex);
}

static void
disable_backing_store (MetaShapedTexture *stex)
{
  MetaShapedTexturePrivate *priv = stex->priv;

  g_clear_pointer (&priv->backing_store, meta_texture_backing_store_free);

  meta_shaped_texture_reset_pipelines (stex);
}

void
meta_shaped_texture_save (MetaShapedTexture *stex)
{

  CoglTexture *texture, *mask_texture;
  MetaShapedTexturePrivate *priv = stex->priv;
  cairo_surface_t *surface;

  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  texture = COGL_TEXTURE (priv->texture);

  if (texture == NULL)
    return;

  g_clear_pointer (&priv->saved_base_surface, cairo_surface_destroy);
  g_clear_pointer (&priv->saved_mask_surface, cairo_surface_destroy);
  g_clear_pointer (&priv->backing_store, meta_texture_backing_store_free);

  if (should_get_via_offscreen (stex))
    {
      CoglTexture *backing_texture;

      meta_shaped_texture_reset_pipelines (stex);

      surface = get_image_via_offscreen (stex, NULL, &backing_texture);

      enable_backing_store (stex, backing_texture);
      cogl_object_unref (backing_texture);
    }
  else
    {
      surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                            cogl_texture_get_width (texture),
                                            cogl_texture_get_height (texture));

      cogl_texture_get_data (texture, CLUTTER_CAIRO_FORMAT_ARGB32,
                             cairo_image_surface_get_stride (surface),
                             cairo_image_surface_get_data (surface));
    }

  priv->saved_base_surface = surface;

  mask_texture = stex->priv->mask_texture;
  if (mask_texture != NULL)
    {
      cairo_surface_t *mask_surface;

      mask_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                                 cogl_texture_get_width (mask_texture),
                                                 cogl_texture_get_height (mask_texture));

      cogl_texture_get_data (mask_texture, CLUTTER_CAIRO_FORMAT_ARGB32,
                             cairo_image_surface_get_stride (mask_surface),
                             cairo_image_surface_get_data (mask_surface));

      cairo_surface_mark_dirty (mask_surface);

      priv->saved_mask_surface = mask_surface;
    }
}

void
meta_shaped_texture_restore (MetaShapedTexture *stex)
{
  MetaShapedTexturePrivate *priv = stex->priv;
  CoglTexture *texture;
  CoglError *error = NULL;

  texture = meta_shaped_texture_get_texture (stex);

  if (texture == NULL)
    return;

  if (priv->mask_texture)
    {
      if (!cogl_texture_set_data (priv->mask_texture, CLUTTER_CAIRO_FORMAT_ARGB32,
                                  cairo_image_surface_get_stride (priv->saved_mask_surface),
                                  cairo_image_surface_get_data (priv->saved_mask_surface), 0,
                                  &error))
        {
          g_warning ("Failed to restore mask texture");
          g_clear_pointer (&error, cogl_error_free);
        }
      g_clear_pointer (&priv->saved_mask_surface, cairo_surface_destroy);
    }

  /* if the main texture doesn't support direct writes, then
   * write to the local backing texture instead, and blend old
   * versus new at paint time.
   */
  if (priv->backing_store)
    {
      meta_texture_backing_store_redraw_mask (priv->backing_store);
      texture = priv->backing_store->texture;
    }

  if (!cogl_texture_set_data (texture, CLUTTER_CAIRO_FORMAT_ARGB32,
                              cairo_image_surface_get_stride (priv->saved_base_surface),
                              cairo_image_surface_get_data (priv->saved_base_surface), 0,
                              &error))
    {
      g_warning ("Failed to restore texture");
      g_clear_pointer (&error, cogl_error_free);
    }
  g_clear_pointer (&priv->saved_base_surface, cairo_surface_destroy);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stex));
}

void
meta_shaped_texture_set_fallback_size (MetaShapedTexture *self,
                                       guint              fallback_width,
                                       guint              fallback_height)
{
  MetaShapedTexturePrivate *priv = self->priv;

  priv->fallback_width = fallback_width;
  priv->fallback_height = fallback_height;
}

static void
meta_shaped_texture_cull_out (MetaCullable   *cullable,
                              cairo_region_t *unobscured_region,
                              cairo_region_t *clip_region)
{
  MetaShapedTexture *self = META_SHAPED_TEXTURE (cullable);
  MetaShapedTexturePrivate *priv = self->priv;

  set_unobscured_region (self, unobscured_region);
  set_clip_region (self, clip_region);

  if (clutter_actor_get_paint_opacity (CLUTTER_ACTOR (self)) == 0xff)
    {
      if (priv->opaque_region)
        {
          if (unobscured_region)
            cairo_region_subtract (unobscured_region, priv->opaque_region);
          if (clip_region)
            cairo_region_subtract (clip_region, priv->opaque_region);
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
