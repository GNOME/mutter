/*
 * Authored By Neil Roberts  <neil@linux.intel.com>
 * and Jasper St. Pierre <jstpierre@mecheye.net>
 *
 * Copyright (C) 2008 Intel Corporation
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2021 Canonical Ltd.
 * Copyright (C) 2022 Neil Moore
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
 * MetaShapedTexture:
 *
 * A ClutterContent which draws a shaped texture
 *
 * A MetaShapedTexture draws a #CoglTexture (often provided from a client
 * surface) in such a way that it matches any required transformations that
 * give its final shape, such as a #MetaMonitorTransform, y-invertedness, or a
 * crop-and-scale operation.
 */

#include "config.h"

#include "backends/meta-monitor-transform.h"
#include "compositor/meta-multi-texture-format-private.h"
#include "compositor/meta-shaped-texture-private.h"
#include "core/boxes-private.h"

#include <math.h>

#include "cogl/cogl.h"
#include "compositor/clutter-utils.h"
#include "compositor/meta-texture-mipmap.h"
#include "compositor/region-utils.h"
#include "core/boxes-private.h"
#include "meta/meta-shaped-texture.h"

static void meta_shaped_texture_dispose  (GObject    *object);

static void clutter_content_iface_init (ClutterContentInterface *iface);

enum
{
  SIZE_CHANGED,

  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

static CoglPipelineKey opaque_overlay_pipeline_key =
  "meta-shaped-texture-opaque-pipeline-key";
static CoglPipelineKey blended_overlay_pipeline_key =
  "meta-shaped-texture-blended-pipeline-key";

struct _MetaShapedTexture
{
  GObject parent;

  MetaMultiTexture *texture;
  CoglTexture *mask_texture;
  CoglSnippet *snippet;

  CoglPipeline *base_pipeline;
  CoglPipeline *combined_pipeline;
  CoglPipeline *unmasked_pipeline;
  CoglPipeline *unmasked_tower_pipeline;
  CoglPipeline *masked_pipeline;
  CoglPipeline *masked_tower_pipeline;
  CoglPipeline *unblended_pipeline;
  CoglPipeline *unblended_tower_pipeline;

  MetaTextureMipmap *texture_mipmap;

  gboolean is_y_inverted;

  /* The region containing only fully opaque pixels */
  cairo_region_t *opaque_region;

  /* MetaCullable regions, see that documentation for more details */
  cairo_region_t *clip_region;

  gboolean size_invalid;
  MetaMonitorTransform transform;
  gboolean has_viewport_src_rect;
  graphene_rect_t viewport_src_rect;
  gboolean has_viewport_dst_size;
  int viewport_dst_width;
  int viewport_dst_height;

  MetaMultiTextureFormat tex_format;
  int tex_width, tex_height;
  int fallback_width, fallback_height;
  int dst_width, dst_height;

  int buffer_scale;

  guint create_mipmaps : 1;
};

G_DEFINE_TYPE_WITH_CODE (MetaShapedTexture, meta_shaped_texture, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTENT,
                                                clutter_content_iface_init));

static void
meta_shaped_texture_class_init (MetaShapedTextureClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = meta_shaped_texture_dispose;

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
  stex->texture_mipmap = meta_texture_mipmap_new ();
  stex->buffer_scale = 1;
  stex->texture = NULL;
  stex->mask_texture = NULL;
  stex->create_mipmaps = TRUE;
  stex->is_y_inverted = TRUE;
  stex->transform = META_MONITOR_TRANSFORM_NORMAL;
}

static void
update_size (MetaShapedTexture *stex)
{
  int buffer_scale = stex->buffer_scale;
  int dst_width;
  int dst_height;

  if (stex->has_viewport_dst_size)
    {
      dst_width = stex->viewport_dst_width;
      dst_height = stex->viewport_dst_height;
    }
  else if (stex->has_viewport_src_rect)
    {
      dst_width = stex->viewport_src_rect.size.width;
      dst_height = stex->viewport_src_rect.size.height;
    }
  else
    {
      if (meta_monitor_transform_is_rotated (stex->transform))
        {
          if (stex->texture)
            {
              dst_width = stex->tex_height / buffer_scale;
              dst_height = stex->tex_width / buffer_scale;
            }
          else
            {
              dst_width = stex->fallback_height / buffer_scale;
              dst_height = stex->fallback_width / buffer_scale;
            }
        }
      else
        {
          if (stex->texture)
            {
              dst_width = stex->tex_width / buffer_scale;
              dst_height = stex->tex_height / buffer_scale;
            }
          else
            {
              dst_width = stex->fallback_width / buffer_scale;
              dst_height = stex->fallback_height / buffer_scale;
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
      clutter_content_invalidate_size (CLUTTER_CONTENT (stex));
      g_signal_emit (stex, signals[SIZE_CHANGED], 0);
    }
}

void
meta_shaped_texture_ensure_size_valid (MetaShapedTexture *stex)
{
  if (stex->size_invalid)
    update_size (stex);
}

void
meta_shaped_texture_set_clip_region (MetaShapedTexture *stex,
                                     cairo_region_t    *clip_region)
{
  g_clear_pointer (&stex->clip_region, cairo_region_destroy);
  if (clip_region)
    stex->clip_region = cairo_region_reference (clip_region);
}

static void
meta_shaped_texture_reset_pipelines (MetaShapedTexture *stex)
{
  g_clear_pointer (&stex->base_pipeline, cogl_object_unref);
  g_clear_pointer (&stex->combined_pipeline, cogl_object_unref);
  g_clear_pointer (&stex->unmasked_pipeline, cogl_object_unref);
  g_clear_pointer (&stex->unmasked_tower_pipeline, cogl_object_unref);
  g_clear_pointer (&stex->masked_pipeline, cogl_object_unref);
  g_clear_pointer (&stex->masked_tower_pipeline, cogl_object_unref);
  g_clear_pointer (&stex->unblended_pipeline, cogl_object_unref);
  g_clear_pointer (&stex->unblended_tower_pipeline, cogl_object_unref);
}

static void
meta_shaped_texture_dispose (GObject *object)
{
  MetaShapedTexture *stex = (MetaShapedTexture *) object;

  g_clear_pointer (&stex->texture_mipmap, meta_texture_mipmap_free);

  g_clear_pointer (&stex->texture, cogl_object_unref);

  meta_shaped_texture_set_mask_texture (stex, NULL);
  meta_shaped_texture_reset_pipelines (stex);

  g_clear_pointer (&stex->opaque_region, cairo_region_destroy);
  g_clear_pointer (&stex->clip_region, cairo_region_destroy);

  g_clear_pointer (&stex->snippet, cogl_object_unref);

  G_OBJECT_CLASS (meta_shaped_texture_parent_class)->dispose (object);
}

static CoglPipeline *
get_base_pipeline (MetaShapedTexture *stex,
                   CoglContext       *ctx)
{
  CoglPipeline *pipeline;
  graphene_matrix_t matrix;
  int i, n_planes;

  if (stex->base_pipeline)
    return stex->base_pipeline;

  pipeline = cogl_pipeline_new (ctx);

  /* We'll add as many layers as there are planes in the multi texture,
   * plus an extra one for the mask */
  n_planes = meta_multi_texture_get_n_planes (stex->texture);
  for (i = 0; i < (n_planes + 1); i++)
    {
      cogl_pipeline_set_layer_wrap_mode_s (pipeline, i,
                                           COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
      cogl_pipeline_set_layer_wrap_mode_t (pipeline, i,
                                           COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
    }

  graphene_matrix_init_identity (&matrix);

  if (stex->has_viewport_src_rect)
    {
      float scaled_tex_width = stex->tex_width / (float) stex->buffer_scale;
      float scaled_tex_height = stex->tex_height / (float) stex->buffer_scale;
      graphene_point3d_t p;

      graphene_point3d_init (&p,
                             stex->viewport_src_rect.origin.x /
                             stex->viewport_src_rect.size.width,
                             stex->viewport_src_rect.origin.y /
                             stex->viewport_src_rect.size.height,
                             0);
      graphene_matrix_translate (&matrix, &p);

      if (meta_monitor_transform_is_rotated (stex->transform))
        {
          graphene_matrix_scale (&matrix,
                                 stex->viewport_src_rect.size.width /
                                 scaled_tex_height,
                                 stex->viewport_src_rect.size.height /
                                 scaled_tex_width,
                                 1);
        }
      else
        {
          graphene_matrix_scale (&matrix,
                                 stex->viewport_src_rect.size.width /
                                 scaled_tex_width,
                                 stex->viewport_src_rect.size.height /
                                 scaled_tex_height,
                                 1);
        }
    }

  meta_monitor_transform_transform_matrix (stex->transform,
                                           &matrix);

  cogl_pipeline_set_layer_matrix (pipeline, 1, &matrix);

  if (!stex->is_y_inverted)
    {
      graphene_matrix_translate (&matrix, &GRAPHENE_POINT3D_INIT (0, -1, 0));
      graphene_matrix_scale (&matrix, 1, -1, 1);
    }

  for (i = 0; i < n_planes; i++)
    cogl_pipeline_set_layer_matrix (pipeline, i, &matrix);

  stex->base_pipeline = pipeline;

  return pipeline;
}

static CoglPipeline *
get_combined_pipeline (MetaShapedTexture *stex,
                       CoglContext       *ctx)
{
  MetaMultiTextureFormat format;
  CoglPipeline *pipeline;
  CoglSnippet *fragment_globals_snippet;
  CoglSnippet *fragment_snippet;
  int i, n_planes;

  if (stex->combined_pipeline)
    return stex->combined_pipeline;

  pipeline = cogl_pipeline_copy (get_base_pipeline (stex, ctx));
  format = meta_multi_texture_get_format (stex->texture);
  n_planes = meta_multi_texture_get_n_planes (stex->texture);

  for (i = 0; i < n_planes; i++)
    {
      cogl_pipeline_set_layer_combine (pipeline, i,
                                       "RGBA = REPLACE(TEXTURE)", NULL);
    }

  meta_multi_texture_format_get_snippets (format,
                                          &fragment_globals_snippet,
                                          &fragment_snippet);
  cogl_pipeline_add_snippet (pipeline, fragment_globals_snippet);
  cogl_pipeline_add_snippet (pipeline, fragment_snippet);

  cogl_clear_object (&fragment_globals_snippet);
  cogl_clear_object (&fragment_snippet);

  stex->combined_pipeline = pipeline;

  return pipeline;
}

static CoglPipeline *
get_unmasked_pipeline (MetaShapedTexture *stex,
                       CoglContext       *ctx,
                       MetaMultiTexture  *tex)
{
  if (stex->texture == tex)
    {
      CoglPipeline *pipeline;

      if (stex->unmasked_pipeline)
        return stex->unmasked_pipeline;

      pipeline = cogl_pipeline_copy (get_combined_pipeline (stex, ctx));
      if (stex->snippet)
        cogl_pipeline_add_layer_snippet (pipeline, 0, stex->snippet);

      stex->unmasked_pipeline = pipeline;
      return pipeline;
    }
  else
    {
      CoglPipeline *pipeline;

      if (stex->unmasked_tower_pipeline)
        return stex->unmasked_tower_pipeline;

      pipeline = cogl_pipeline_copy (get_base_pipeline (stex, ctx));
      stex->unmasked_tower_pipeline = pipeline;
      return pipeline;
    }
}

static CoglPipeline *
get_masked_pipeline (MetaShapedTexture *stex,
                     CoglContext       *ctx,
                     MetaMultiTexture  *tex)
{
  g_assert (meta_multi_texture_get_n_planes (stex->texture) == 1);

  if (stex->texture == tex)
    {
      CoglPipeline *pipeline;

      if (stex->masked_pipeline)
        return stex->masked_pipeline;

      pipeline = cogl_pipeline_copy (get_base_pipeline (stex, ctx));
      cogl_pipeline_set_layer_combine (pipeline, 1,
                                       "RGBA = MODULATE (PREVIOUS, TEXTURE[A])",
                                       NULL);
      if (stex->snippet)
        cogl_pipeline_add_layer_snippet (pipeline, 0, stex->snippet);

      stex->masked_pipeline = pipeline;
      return pipeline;
    }
  else
    {
      CoglPipeline *pipeline;

      if (stex->masked_tower_pipeline)
        return stex->masked_tower_pipeline;

      pipeline = cogl_pipeline_copy (get_base_pipeline (stex, ctx));
      cogl_pipeline_set_layer_combine (pipeline, 1,
                                       "RGBA = MODULATE (PREVIOUS, TEXTURE[A])",
                                       NULL);

      stex->masked_tower_pipeline = pipeline;
      return pipeline;
    }
}

static CoglPipeline *
get_unblended_pipeline (MetaShapedTexture *stex,
                        CoglContext       *ctx,
                        MetaMultiTexture  *tex)
{
  if (stex->texture == tex)
    {
      CoglPipeline *pipeline;

      if (stex->unblended_pipeline)
        return stex->unblended_pipeline;

      pipeline = cogl_pipeline_copy (get_combined_pipeline (stex, ctx));
      cogl_pipeline_set_layer_combine (pipeline, 0,
                                       "RGBA = REPLACE (TEXTURE)",
                                       NULL);
      if (stex->snippet)
        cogl_pipeline_add_layer_snippet (pipeline, 0, stex->snippet);

      stex->unblended_pipeline = pipeline;
      return pipeline;
    }
  else
    {
      CoglPipeline *pipeline;

      if (stex->unblended_tower_pipeline)
        return stex->unblended_tower_pipeline;

      pipeline = cogl_pipeline_copy (get_base_pipeline (stex, ctx));
      cogl_pipeline_set_layer_combine (pipeline, 0,
                                       "RGBA = REPLACE (TEXTURE)",
                                       NULL);

      stex->unblended_tower_pipeline = pipeline;
      return pipeline;
    }
}

static CoglPipeline *
get_opaque_overlay_pipeline (CoglContext *ctx)
{
  CoglPipeline *pipeline;

  pipeline = cogl_context_get_named_pipeline (ctx,
                                              &opaque_overlay_pipeline_key);
  if (!pipeline)
    {
      pipeline = cogl_pipeline_new (ctx);
      cogl_pipeline_set_color4ub (pipeline, 0x00, 0x33, 0x00, 0x33);

      cogl_context_set_named_pipeline (ctx,
                                       &opaque_overlay_pipeline_key,
                                       pipeline);
    }

  return pipeline;
}

static CoglPipeline *
get_blended_overlay_pipeline (CoglContext *ctx)
{
  CoglPipeline *pipeline;

  pipeline = cogl_context_get_named_pipeline (ctx,
                                              &blended_overlay_pipeline_key);
  if (!pipeline)
    {
      pipeline = cogl_pipeline_new (ctx);
      cogl_pipeline_set_color4ub (pipeline, 0x33, 0x00, 0x33, 0x33);

      cogl_context_set_named_pipeline (ctx,
                                       &blended_overlay_pipeline_key,
                                       pipeline);
    }

  return pipeline;
}

static void
paint_clipped_rectangle_node (MetaShapedTexture *stex,
                              ClutterPaintNode  *root_node,
                              CoglPipeline      *pipeline,
                              MtkRectangle      *rect,
                              ClutterActorBox   *alloc)
{
  g_autoptr (ClutterPaintNode) node = NULL;
  float ratio_h, ratio_v;
  float x1, y1, x2, y2;
  float coords[8];
  float alloc_width;
  float alloc_height;

  ratio_h = clutter_actor_box_get_width (alloc) / (float) stex->dst_width;
  ratio_v = clutter_actor_box_get_height (alloc) / (float) stex->dst_height;

  x1 = alloc->x1 + rect->x * ratio_h;
  y1 = alloc->y1 + rect->y * ratio_v;
  x2 = alloc->x1 + (rect->x + rect->width) * ratio_h;
  y2 = alloc->y1 + (rect->y + rect->height) * ratio_v;

  alloc_width = alloc->x2 - alloc->x1;
  alloc_height = alloc->y2 - alloc->y1;

  coords[0] = rect->x / alloc_width * ratio_h;
  coords[1] = rect->y / alloc_height * ratio_v;
  coords[2] = (rect->x + rect->width) / alloc_width * ratio_h;
  coords[3] = (rect->y + rect->height) / alloc_height * ratio_v;

  coords[4] = coords[0];
  coords[5] = coords[1];
  coords[6] = coords[2];
  coords[7] = coords[3];

  node = clutter_pipeline_node_new (pipeline);
  clutter_paint_node_set_static_name (node, "MetaShapedTexture (clipped)");
  clutter_paint_node_add_child (root_node, node);

  clutter_paint_node_add_multitexture_rectangle (node,
                                                 &(ClutterActorBox) {
                                                   .x1 = x1,
                                                   .y1 = y1,
                                                   .x2 = x2,
                                                   .y2 = y2,
                                                 },
                                                 coords, 8);
}

static void
set_multi_texture (MetaShapedTexture *stex,
                   MetaMultiTexture  *multi_tex)
{
  MetaMultiTextureFormat format;
  int width, height;

  g_clear_object (&stex->texture);

  if (multi_tex != NULL)
    {
      stex->texture = g_object_ref (multi_tex);
      format = meta_multi_texture_get_format (multi_tex);
      width = meta_multi_texture_get_width (multi_tex);
      height = meta_multi_texture_get_height (multi_tex);
    }
  else
    {
      format = META_MULTI_TEXTURE_FORMAT_INVALID;
      width = 0;
      height = 0;
    }

  if (stex->tex_width != width ||
      stex->tex_height != height ||
      stex->tex_format != format)
    {
      stex->tex_format = format;
      stex->tex_width = width;
      stex->tex_height = height;
      meta_shaped_texture_reset_pipelines (stex);
      update_size (stex);
    }

  meta_texture_mipmap_set_base_texture (stex->texture_mipmap, stex->texture);
  meta_texture_mipmap_invalidate (stex->texture_mipmap);
}

static inline void
flip_ints (int *x,
           int *y)
{
  int tmp;

  tmp = *x;
  *x = *y;
  *y = tmp;
}

static void
do_paint_content (MetaShapedTexture   *stex,
                  ClutterPaintNode    *root_node,
                  ClutterPaintContext *paint_context,
                  ClutterActorBox     *alloc,
                  uint8_t              opacity)
{
  int dst_width, dst_height;
  MtkRectangle content_rect;
  gboolean use_opaque_region;
  cairo_region_t *blended_tex_region;
  CoglContext *ctx;
  CoglPipelineFilter min_filter, mag_filter;
  MetaTransforms transforms;
  MetaMultiTexture *paint_tex = stex->texture;
  CoglFramebuffer *framebuffer;
  int sample_width, sample_height;
  int texture_width, texture_height;
  gboolean debug_paint_opaque_region;
  int n_planes;

  meta_shaped_texture_ensure_size_valid (stex);

  dst_width = stex->dst_width;
  dst_height = stex->dst_height;

  if (dst_width == 0 || dst_height == 0) /* no contents yet */
    return;

  texture_width = meta_multi_texture_get_width (stex->texture);
  texture_height = meta_multi_texture_get_height (stex->texture);

  content_rect = (MtkRectangle) {
    .x = 0,
    .y = 0,
    .width = dst_width,
    .height = dst_height,
  };

  debug_paint_opaque_region =
    meta_get_debug_paint_flags () & META_DEBUG_PAINT_OPAQUE_REGION;

  /* Use nearest-pixel interpolation if the texture is unscaled. This
   * improves performance, especially with software rendering.
   */

  framebuffer = clutter_paint_node_get_framebuffer (root_node);
  if (!framebuffer)
    framebuffer = clutter_paint_context_get_framebuffer (paint_context);

  if (stex->has_viewport_src_rect)
    {
      sample_width = stex->viewport_src_rect.size.width * stex->buffer_scale;
      sample_height = stex->viewport_src_rect.size.height * stex->buffer_scale;
    }
  else
    {
      sample_width = texture_width;
      sample_height = texture_height;
    }
  if (meta_monitor_transform_is_rotated (stex->transform))
    flip_ints (&sample_width, &sample_height);

  if (meta_actor_painting_untransformed (framebuffer,
                                         dst_width, dst_height,
                                         sample_width, sample_height,
                                         &transforms))
    {
      min_filter = COGL_PIPELINE_FILTER_NEAREST;
      mag_filter = COGL_PIPELINE_FILTER_NEAREST;
    }
  else
    {
      min_filter = COGL_PIPELINE_FILTER_LINEAR;
      mag_filter = COGL_PIPELINE_FILTER_LINEAR;

      /* If we're painting a texture below half its native resolution
       * then mipmapping is required to avoid aliasing. If it's above
       * half then sticking with COGL_PIPELINE_FILTER_LINEAR will look
       * and perform better.
       */
      if (stex->create_mipmaps &&
          transforms.x_scale < 0.5 &&
          transforms.y_scale < 0.5 &&
          texture_width >= 8 &&
          texture_height >= 8)
        {
          paint_tex = meta_texture_mipmap_get_paint_texture (stex->texture_mipmap);
          min_filter = COGL_PIPELINE_FILTER_LINEAR_MIPMAP_NEAREST;
        }
    }

  ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());

  use_opaque_region = stex->opaque_region && opacity == 255;

  if (use_opaque_region)
    {
      if (stex->clip_region)
        blended_tex_region = cairo_region_copy (stex->clip_region);
      else
        blended_tex_region = cairo_region_create_rectangle (&content_rect);

      cairo_region_subtract (blended_tex_region, stex->opaque_region);
    }
  else
    {
      if (stex->clip_region)
        blended_tex_region = cairo_region_reference (stex->clip_region);
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

  n_planes = meta_multi_texture_get_n_planes (paint_tex);

  /* First, paint the unblended parts, which are part of the opaque region. */
  if (use_opaque_region)
    {
      cairo_region_t *region;
      int n_rects;
      int i;

      if (stex->clip_region)
        {
          region = cairo_region_copy (stex->clip_region);
          cairo_region_intersect (region, stex->opaque_region);
        }
      else
        {
          region = cairo_region_reference (stex->opaque_region);
        }

      if (!cairo_region_is_empty (region))
        {
          CoglPipeline *opaque_pipeline;

          opaque_pipeline = get_unblended_pipeline (stex, ctx, paint_tex);

          for (i = 0; i < n_planes; i++)
            {
              CoglTexture *plane = meta_multi_texture_get_plane (paint_tex, i);

              cogl_pipeline_set_layer_texture (opaque_pipeline, i, plane);
              cogl_pipeline_set_layer_filters (opaque_pipeline, i,
                                               min_filter, mag_filter);
            }

          n_rects = cairo_region_num_rectangles (region);
          for (i = 0; i < n_rects; i++)
            {
              MtkRectangle rect;
              cairo_region_get_rectangle (region, i, &rect);
              paint_clipped_rectangle_node (stex, root_node,
                                            opaque_pipeline,
                                            &rect, alloc);

              if (G_UNLIKELY (debug_paint_opaque_region))
                {
                  CoglPipeline *opaque_overlay_pipeline;

                  opaque_overlay_pipeline = get_opaque_overlay_pipeline (ctx);
                  paint_clipped_rectangle_node (stex, root_node,
                                                opaque_overlay_pipeline,
                                                &rect, alloc);
                }
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
      CoglColor color;
      int i;

      if (stex->mask_texture == NULL)
        {
          blended_pipeline = get_unmasked_pipeline (stex, ctx, paint_tex);
        }
      else
        {
          blended_pipeline = get_masked_pipeline (stex, ctx, paint_tex);
          cogl_pipeline_set_layer_texture (blended_pipeline, n_planes, stex->mask_texture);
          cogl_pipeline_set_layer_filters (blended_pipeline, n_planes, min_filter, mag_filter);
        }

      for (i = 0; i < n_planes; i++)
        {
          CoglTexture *plane = meta_multi_texture_get_plane (paint_tex, i);

          cogl_pipeline_set_layer_texture (blended_pipeline, i, plane);
          cogl_pipeline_set_layer_filters (blended_pipeline, i, min_filter, mag_filter);
        }

      cogl_color_init_from_4ub (&color, opacity, opacity, opacity, opacity);
      cogl_pipeline_set_color (blended_pipeline, &color);

      if (blended_tex_region)
        {
          /* 1) blended_tex_region is not empty. Paint the rectangles. */
          int i;
          int n_rects = cairo_region_num_rectangles (blended_tex_region);

          for (i = 0; i < n_rects; i++)
            {
              MtkRectangle rect;
              cairo_region_get_rectangle (blended_tex_region, i, &rect);

              if (!mtk_rectangle_intersect (&content_rect, &rect, &rect))
                continue;

              paint_clipped_rectangle_node (stex, root_node,
                                            blended_pipeline,
                                            &rect, alloc);

              if (G_UNLIKELY (debug_paint_opaque_region))
                {
                  CoglPipeline *blended_overlay_pipeline;

                  blended_overlay_pipeline = get_blended_overlay_pipeline (ctx);
                  paint_clipped_rectangle_node (stex, root_node,
                                                blended_overlay_pipeline,
                                                &rect, alloc);
                }
            }
        }
      else
        {
          g_autoptr (ClutterPaintNode) node = NULL;

          node = clutter_pipeline_node_new (blended_pipeline);
          clutter_paint_node_set_static_name (node, "MetaShapedTexture (unclipped)");
          clutter_paint_node_add_child (root_node, node);

          /* 3) blended_tex_region is NULL. Do a full paint. */
          clutter_paint_node_add_rectangle (node, alloc);

          if (G_UNLIKELY (debug_paint_opaque_region))
            {
              CoglPipeline *blended_overlay_pipeline;
              g_autoptr (ClutterPaintNode) node_overlay = NULL;

              blended_overlay_pipeline = get_blended_overlay_pipeline (ctx);

              node_overlay = clutter_pipeline_node_new (blended_overlay_pipeline);
              clutter_paint_node_set_static_name (node_overlay,
                                                  "MetaShapedTexture (unclipped overlay)");
              clutter_paint_node_add_child (root_node, node_overlay);
              clutter_paint_node_add_rectangle (node_overlay, alloc);
            }
        }
    }

  g_clear_pointer (&blended_tex_region, cairo_region_destroy);
}

static void
meta_shaped_texture_paint_content (ClutterContent      *content,
                                   ClutterActor        *actor,
                                   ClutterPaintNode    *root_node,
                                   ClutterPaintContext *paint_context)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (content);
  ClutterActorBox alloc;
  uint8_t opacity;

  if (stex->clip_region && cairo_region_is_empty (stex->clip_region))
    return;

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
  if (stex->texture == NULL)
    return;

  opacity = clutter_actor_get_paint_opacity (actor);
  clutter_actor_get_content_box (actor, &alloc);

  do_paint_content (stex, root_node, paint_context, &alloc, opacity);
}

static gboolean
meta_shaped_texture_get_preferred_size (ClutterContent *content,
                                        float          *width,
                                        float          *height)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (content);

  meta_shaped_texture_ensure_size_valid (stex);

  if (width)
    *width = stex->dst_width;

  if (height)
    *height = stex->dst_height;

  return TRUE;
}

static void
clutter_content_iface_init (ClutterContentInterface *iface)
{
  iface->paint_content = meta_shaped_texture_paint_content;
  iface->get_preferred_size = meta_shaped_texture_get_preferred_size;
}

void
meta_shaped_texture_set_create_mipmaps (MetaShapedTexture *stex,
                                        gboolean           create_mipmaps)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  create_mipmaps = create_mipmaps != FALSE;

  if (create_mipmaps != stex->create_mipmaps)
    {
      stex->create_mipmaps = create_mipmaps;

      if (!stex->create_mipmaps)
        meta_texture_mipmap_clear (stex->texture_mipmap);
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

  clutter_content_invalidate (CLUTTER_CONTENT (stex));
}

/**
 * meta_shaped_texture_update_area:
 * @stex: #MetaShapedTexture
 * @x: the x coordinate of the damaged area
 * @y: the y coordinate of the damaged area
 * @width: the width of the damaged area
 * @height: the height of the damaged area
 * @clip: (out): the resulting clip region
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
                                 int                height,
                                 MtkRectangle      *clip)
{
  MetaMonitorTransform inverted_transform;
  MtkRectangle buffer_rect;
  int scaled_and_transformed_width;
  int scaled_and_transformed_height;

  if (stex->texture == NULL)
    return FALSE;

  /* Pad the actor clip to ensure that pixels affected by linear scaling are accounted for */
  *clip = (MtkRectangle) {
    .x = x - 1,
    .y = y - 1,
    .width = width + 2,
    .height = height + 2
  };

  buffer_rect = (MtkRectangle) {
    .x = 0,
    .y = 0,
    .width = stex->tex_width,
    .height = stex->tex_height,
  };

  mtk_rectangle_intersect (&buffer_rect, clip, clip);

  meta_rectangle_scale_double (clip,
                               1.0 / stex->buffer_scale,
                               MTK_ROUNDING_STRATEGY_GROW,
                               clip);

  if (meta_monitor_transform_is_rotated (stex->transform))
    {
      scaled_and_transformed_width = stex->tex_height / stex->buffer_scale;
      scaled_and_transformed_height = stex->tex_width / stex->buffer_scale;
    }
  else
    {
      scaled_and_transformed_width = stex->tex_width / stex->buffer_scale;
      scaled_and_transformed_height = stex->tex_height / stex->buffer_scale;
    }
  inverted_transform = meta_monitor_transform_invert (stex->transform);
  meta_rectangle_transform (clip,
                            inverted_transform,
                            scaled_and_transformed_width,
                            scaled_and_transformed_height,
                            clip);

  if (stex->has_viewport_src_rect || stex->has_viewport_dst_size)
    {
      graphene_rect_t viewport;
      graphene_rect_t inverted_viewport;
      float dst_width;
      float dst_height;
      int inverted_dst_width;
      int inverted_dst_height;

      if (stex->has_viewport_src_rect)
        {
          viewport = stex->viewport_src_rect;
        }
      else
        {
          viewport = (graphene_rect_t) {
            .origin.x = 0,
            .origin.y = 0,
            .size.width = scaled_and_transformed_width,
            .size.height = scaled_and_transformed_height,
          };
        }

      if (stex->has_viewport_dst_size)
        {
          dst_width = (float) stex->viewport_dst_width;
          dst_height = (float) stex->viewport_dst_height;
        }
      else
        {
          dst_width = (float) viewport.size.width;
          dst_height = (float) viewport.size.height;
        }

      inverted_viewport = (graphene_rect_t) {
        .origin.x = -(viewport.origin.x * (dst_width / viewport.size.width)),
        .origin.y = -(viewport.origin.y * (dst_height / viewport.size.height)),
        .size.width = dst_width,
        .size.height = dst_height
      };
      inverted_dst_width = ceilf (viewport.size.width);
      inverted_dst_height = ceilf (viewport.size.height);

      meta_rectangle_crop_and_scale (clip,
                                     &inverted_viewport,
                                     inverted_dst_width,
                                     inverted_dst_height,
                                     clip);
    }

  meta_texture_mipmap_invalidate (stex->texture_mipmap);

  return TRUE;
}

/**
 * meta_shaped_texture_set_texture:
 * @stex: The #MetaShapedTexture
 * @pixmap: The #MetaMultiTexture to display
 */
void
meta_shaped_texture_set_texture (MetaShapedTexture *stex,
                                 MetaMultiTexture  *texture)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  if (stex->texture == texture)
    return;

  set_multi_texture (stex, texture);
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
MetaMultiTexture *
meta_shaped_texture_get_texture (MetaShapedTexture *stex)
{
  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), NULL);
  return stex->texture;
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
  g_clear_pointer (&stex->opaque_region, cairo_region_destroy);
  if (opaque_region)
    stex->opaque_region = cairo_region_reference (opaque_region);
}

cairo_region_t *
meta_shaped_texture_get_opaque_region (MetaShapedTexture *stex)
{
  return stex->opaque_region;
}

gboolean
meta_shaped_texture_has_alpha (MetaShapedTexture *stex)
{
  MetaMultiTexture *multi_texture;
  CoglTexture *cogl_texture;

  multi_texture = stex->texture;
  if (!multi_texture)
    return TRUE;

  if (!meta_multi_texture_is_simple (multi_texture))
    return FALSE;

  cogl_texture = meta_multi_texture_get_plane (multi_texture, 0);
  switch (cogl_texture_get_components (cogl_texture))
    {
    case COGL_TEXTURE_COMPONENTS_A:
    case COGL_TEXTURE_COMPONENTS_RGBA:
      return TRUE;
    case COGL_TEXTURE_COMPONENTS_RG:
    case COGL_TEXTURE_COMPONENTS_RGB:
    case COGL_TEXTURE_COMPONENTS_DEPTH:
      return FALSE;
    }

  g_warn_if_reached ();
  return FALSE;
}

gboolean
meta_shaped_texture_is_opaque (MetaShapedTexture *stex)
{
  MetaMultiTexture *multi_texture;
  MtkRectangle opaque_rect;

  multi_texture = stex->texture;
  if (!multi_texture)
    return TRUE;

  if (!meta_shaped_texture_has_alpha (stex))
    return TRUE;

  if (!stex->opaque_region)
    return FALSE;

  if (cairo_region_num_rectangles (stex->opaque_region) != 1)
    return FALSE;

  cairo_region_get_extents (stex->opaque_region, &opaque_rect);

  meta_shaped_texture_ensure_size_valid (stex);

  return mtk_rectangle_equal (&opaque_rect,
                              &(MtkRectangle) {
                               .width = stex->dst_width,
                               .height = stex->dst_height
                              });
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
 * meta_shaped_texture_set_viewport_src_rect:
 * @stex: A #MetaShapedTexture
 * @src_rect: The viewport source rectangle
 *
 * Sets the viewport area that can be used to crop the original texture. The
 * cropped result can then be optionally scaled afterwards using
 * meta_shaped_texture_set_viewport_dst_size() as part of a crop-and-scale
 * operation.
 *
 * Note that the viewport's geometry should be provided in the coordinate space
 * of the texture received by the client, which might've been scaled as noted by
 * meta_shaped_texture_set_buffer_scale().
 *
 * %NULL is an invalid value for @src_rect. Use
 * meta_shaped_texture_reset_viewport_src_rect() if you want to remove the
 * cropping source rectangle.
 */
void
meta_shaped_texture_set_viewport_src_rect (MetaShapedTexture *stex,
                                           graphene_rect_t   *src_rect)
{
  if (!stex->has_viewport_src_rect ||
      !G_APPROX_VALUE (stex->viewport_src_rect.origin.x,
                       src_rect->origin.x, FLT_EPSILON) ||
      !G_APPROX_VALUE (stex->viewport_src_rect.origin.y,
                       src_rect->origin.y, FLT_EPSILON) ||
      !G_APPROX_VALUE (stex->viewport_src_rect.size.width,
                       src_rect->size.width, FLT_EPSILON) ||
      !G_APPROX_VALUE (stex->viewport_src_rect.size.height,
                       src_rect->size.height, FLT_EPSILON))
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

/**
 * meta_shaped_texture_set_viewport_dst_size:
 * @stex: #MetaShapedTexture
 * @dst_width: The final viewport width (> 0)
 * @dst_height: The final viewport height (> 0)
 *
 * Sets a viewport size on @stex of the given @width and @height, which may
 * lead to scaling the texture. If you need to have cropping, use
 * meta_shaped_texture_set_viewport_src_rect() first, after which the scaling
 * stemming from this method will be applied.
 *
 * If you no longer want to have any scaling, use
 * meta_shaped_texture_reset_viewport_dst_size() to clear the current
 * parameters.
 */
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

gboolean
meta_shaped_texture_should_get_via_offscreen (MetaShapedTexture *stex)
{
  CoglTexture *cogl_texture;

  if (stex->mask_texture != NULL)
    return TRUE;

  if (meta_multi_texture_get_n_planes (stex->texture) > 1)
    return FALSE;

  cogl_texture = meta_multi_texture_get_plane (stex->texture, 0);
  if (!cogl_texture_is_get_data_supported (cogl_texture))
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

/**
 * meta_shaped_texture_get_image:
 * @stex: A #MetaShapedTexture
 * @clip: (nullable): A clipping rectangle, to help prevent extra processing.
 * In the case that the clipping rectangle is partially or fully
 * outside the bounds of the texture, the rectangle will be clipped.
 *
 * Flattens the two layers of the shaped texture into one ARGB32
 * image by alpha blending the two images, and returns the flattened
 * image.
 *
 * Returns: (nullable) (transfer full): a new cairo surface to be freed with
 * cairo_surface_destroy().
 */
cairo_surface_t *
meta_shaped_texture_get_image (MetaShapedTexture *stex,
                               MtkRectangle      *clip)
{
  MtkRectangle *image_clip = NULL;
  CoglTexture *texture;
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());
  cairo_surface_t *surface;

  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), NULL);

  if (stex->texture == NULL)
    return NULL;

  if (meta_shaped_texture_should_get_via_offscreen (stex))
    return NULL;

  meta_shaped_texture_ensure_size_valid (stex);

  if (stex->dst_width == 0 || stex->dst_height == 0)
    return NULL;

  if (clip != NULL)
    {
      MtkRectangle dst_rect;

      image_clip = alloca (sizeof (MtkRectangle));
      dst_rect = (MtkRectangle) {
        .width = stex->dst_width,
        .height = stex->dst_height,
      };

      if (!mtk_rectangle_intersect (&dst_rect, clip,
                                    image_clip))
        return NULL;

      *image_clip = (MtkRectangle) {
        .x = image_clip->x * stex->buffer_scale,
        .y = image_clip->y * stex->buffer_scale,
        .width = image_clip->width * stex->buffer_scale,
        .height = image_clip->height * stex->buffer_scale,
      };
    }

  /* We know that we only have 1 plane at this point */
  texture = meta_multi_texture_get_plane (stex->texture, 0);

  if (image_clip)
    texture = COGL_TEXTURE (cogl_sub_texture_new (cogl_context,
                                                  texture,
                                                  image_clip->x,
                                                  image_clip->y,
                                                  image_clip->width,
                                                  image_clip->height));

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        cogl_texture_get_width (texture),
                                        cogl_texture_get_height (texture));

  cogl_texture_get_data (texture, CLUTTER_CAIRO_FORMAT_ARGB32,
                         cairo_image_surface_get_stride (surface),
                         cairo_image_surface_get_data (surface));

  cairo_surface_mark_dirty (surface);

  if (image_clip)
    cogl_object_unref (texture);

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

MetaShapedTexture *
meta_shaped_texture_new (void)
{
  return g_object_new (META_TYPE_SHAPED_TEXTURE, NULL);
}

/**
 * meta_shaped_texture_set_buffer_scale:
 * @stex: A #MetaShapedTexture
 * @buffer_scale: The scale that should be applied to coorsinate space
 *
 * Instructs @stex to interpret the geometry of the input texture by scaling it
 * with @buffer_scale. This means that the #CoglTexture that is provided by a
 * client is already scaled by that factor.
 */
void
meta_shaped_texture_set_buffer_scale (MetaShapedTexture *stex,
                                      int                buffer_scale)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  if (buffer_scale == stex->buffer_scale)
    return;

  stex->buffer_scale = buffer_scale;

  invalidate_size (stex);
}

int
meta_shaped_texture_get_buffer_scale (MetaShapedTexture *stex)
{
  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), 1.0);

  return stex->buffer_scale;
}

/**
 * meta_shaped_texture_get_width:
 * @stex: A #MetaShapedTexture
 *
 * Returns: The final width of @stex after its shaping operations are applied.
 */
int
meta_shaped_texture_get_width (MetaShapedTexture *stex)
{
  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), 0);

  meta_shaped_texture_ensure_size_valid (stex);

  return stex->dst_width;
}

/**
 * meta_shaped_texture_get_height:
 * @stex: A #MetaShapedTexture
 *
 * Returns: The final height of @stex after its shaping operations are applied.
 */
int
meta_shaped_texture_get_height (MetaShapedTexture *stex)
{
  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), 0);

  meta_shaped_texture_ensure_size_valid (stex);

  return stex->dst_height;
}

static graphene_size_t
get_unscaled_size (MetaShapedTexture *stex)
{
  graphene_size_t buffer_size;

  if (stex->has_viewport_src_rect)
    {
      graphene_size_scale (&stex->viewport_src_rect.size,
                           stex->buffer_scale,
                           &buffer_size);
    }
  else
    {
      buffer_size = (graphene_size_t) {
        .width = stex->tex_width,
        .height = stex->tex_height,
      };
    }

  if (meta_monitor_transform_is_rotated (stex->transform))
    {
      return (graphene_size_t) {
        .width = buffer_size.height,
        .height = buffer_size.width,
      };
    }
  else
    {
      return buffer_size;
    }
}

/**
 * meta_shaped_texture_get_unscaled_width:
 * @stex: A #MetaShapedTexture
 *
 * Returns: The unscaled width of @stex after its shaping operations are applied.
 */
float
meta_shaped_texture_get_unscaled_width (MetaShapedTexture *stex)
{
  graphene_size_t unscaled_size;

  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), 0);

  unscaled_size = get_unscaled_size (stex);

  return unscaled_size.width;
}

/**
 * meta_shaped_texture_get_unscaled_height:
 * @stex: A #MetaShapedTexture
 *
 * Returns: The unscaled height of @stex after its shaping operations are applied.
 */
float
meta_shaped_texture_get_unscaled_height (MetaShapedTexture *stex)
{
  graphene_size_t unscaled_size;

  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), 0);

  unscaled_size = get_unscaled_size (stex);

  return unscaled_size.height;
}
