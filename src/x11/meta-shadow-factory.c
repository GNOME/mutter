/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright 2010 Red Hat, Inc.
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

#include "config.h"

#include <math.h>
#include <string.h>

#include "x11/meta-shadow-factory.h"

#include "compositor/cogl-utils.h"
#include "meta/util.h"

/* This file implements blurring the shape of a window to produce a
 * shadow texture. The details are discussed below; a quick summary
 * of the optimizations we use:
 *
 * - If the window shape is along the lines of a rounded rectangle -
 *   a rectangular center portion with stuff at the corners - then
 *   the blur of this - the shadow - can also be represented as a
 *   9-sliced texture and the same texture can be used for different
 *   size.
 *
 * - We use the fact that a Gaussian blur is separable to do a
 *   2D blur as 1D blur of the rows followed by a 1D blur of the
 *   columns.
 *
 * - For better cache efficiency, we blur rows, transpose the image
 *   in blocks, blur rows again, and then transpose back.
 *
 * - We approximate the 1D gaussian blur as 3 successive box filters.
 */

typedef struct _MetaShadowCacheKey  MetaShadowCacheKey;
typedef struct _MetaShadowClassInfo MetaShadowClassInfo;

struct _MetaShadowCacheKey
{
  MetaWindowShape *shape;
  int radius;
  int top_fade;
};

struct _MetaShadow
{
  int ref_count;

  MetaShadowFactory *factory;
  MetaShadowCacheKey key;
  CoglTexture *texture;
  CoglPipeline *pipeline;

  /* The outer order is the distance the shadow extends outside the window
   * shape; the inner border is the unscaled portion inside the window
   * shape */
  int outer_border_top;
  int inner_border_top;
  int outer_border_right;
  int inner_border_right;
  int outer_border_bottom;
  int inner_border_bottom;
  int outer_border_left;
  int inner_border_left;

  guint scale_width : 1;
  guint scale_height : 1;
};

struct _MetaShadowClassInfo
{
  const char *name; /* const so we can reuse for static definitions */
  MetaShadowParams focused;
  MetaShadowParams unfocused;
};

struct _MetaShadowFactory
{
  GObject parent_instance;

  /* MetaShadowCacheKey => MetaShadow; the shadows are not referenced
   * by the factory, they are simply removed from the table when freed */
  GHashTable *shadows;

  /* class name => MetaShadowClassInfo */
  GHashTable *shadow_classes;
};

/* The first element in this array also defines the default parameters
 * for newly created classes */
MetaShadowClassInfo default_shadow_classes[] = {
  { "normal",       { 10, -1, 0, 3, 128 }, { 8, -1, 0, 2, 64 } },
  { "dialog",       { 10, -1, 0, 3, 128 }, { 8, -1, 0, 2, 64 } },
  { "modal_dialog", { 10, -1, 0, 3, 128 }, { 8, -1, 0, 2, 64 } },
  { "utility",      { 10, -1, 0, 3, 128 }, { 8, -1, 0, 2, 64 } },
  { "border",       { 10, -1, 0, 3, 128 }, { 8, -1, 0, 2, 64 } },
  { "menu",         { 10, -1, 0, 3, 128 }, { 8, -1, 0, 2, 64 } },

  { "popup-menu",    { 1, -1, 0, 0, 128 }, { 1, -1, 0, 0, 128 } },
  { "dropdown-menu", { 1, -1, 0, 0, 128 }, { 1, -1, 0, 0, 128 } },

  { "attached",      { 10, -1, 0, 3, 128 }, { 8, -1, 0, 2, 64 } }
};

G_DEFINE_TYPE (MetaShadowFactory, meta_shadow_factory, G_TYPE_OBJECT);

static guint
meta_shadow_cache_key_hash (gconstpointer val)
{
  const MetaShadowCacheKey *key = val;

  return 59 * key->radius + 67 * key->top_fade + 73 * meta_window_shape_hash (key->shape);
}

static gboolean
meta_shadow_cache_key_equal (gconstpointer a,
                             gconstpointer b)
{
  const MetaShadowCacheKey *key_a = a;
  const MetaShadowCacheKey *key_b = b;

  return (key_a->radius == key_b->radius && key_a->top_fade == key_b->top_fade &&
          meta_window_shape_equal (key_a->shape, key_b->shape));
}

MetaShadow *
meta_shadow_ref (MetaShadow *shadow)
{
  shadow->ref_count++;

  return shadow;
}

void
meta_shadow_unref (MetaShadow *shadow)
{
  shadow->ref_count--;
  if (shadow->ref_count == 0)
    {
      if (shadow->factory)
        {
          g_hash_table_remove (shadow->factory->shadows,
                               &shadow->key);
        }

      meta_window_shape_unref (shadow->key.shape);
      g_object_unref (shadow->texture);
      g_object_unref (shadow->pipeline);

      g_free (shadow);
    }
}

/**
 * meta_shadow_paint:
 * @window_x: x position of the region to paint a shadow for
 * @window_y: y position of the region to paint a shadow for
 * @window_width: actual width of the region to paint a shadow for
 * @window_height: actual height of the region to paint a shadow for
 * @clip: (nullable): if non-%NULL specifies the visible portion
 *   of the shadow.
 * @clip_strictly: if %TRUE, drawing will be clipped strictly
 *   to @clip, otherwise, it will be only used to optimize
 *   drawing.
 *
 * Paints the shadow at the given position, for the specified actual
 * size of the region.
 *
 * Since a #MetaShadow can be shared between different sizes with
 * the same extracted [struct@Meta.WindowShape] the size needs to be passed in here.
 */
void
meta_shadow_paint (MetaShadow      *shadow,
                   CoglFramebuffer *framebuffer,
                   int              window_x,
                   int              window_y,
                   int              window_width,
                   int              window_height,
                   guint8           opacity,
                   MtkRegion       *clip,
                   gboolean         clip_strictly)
{
  CoglColor color;
  float texture_width = cogl_texture_get_width (shadow->texture);
  float texture_height = cogl_texture_get_height (shadow->texture);
  int i, j;
  float src_x[4];
  float src_y[4];
  int dest_x[4];
  int dest_y[4];
  int n_x, n_y;

  if (clip && mtk_region_is_empty (clip))
    return;

  cogl_color_init_from_4f (&color,
                           opacity / 255.0f, opacity / 255.0f,
                           opacity / 255.0f, opacity / 255.0f);
  cogl_pipeline_set_color (shadow->pipeline, &color);

  if (shadow->scale_width)
    {
      n_x = 3;

      src_x[0] = 0.0;
      src_x[1] = (shadow->inner_border_left + shadow->outer_border_left) / texture_width;
      src_x[2] = (texture_width - (shadow->inner_border_right + shadow->outer_border_right)) / texture_width;
      src_x[3] = 1.0;

      dest_x[0] = window_x - shadow->outer_border_left;
      dest_x[1] = window_x + shadow->inner_border_left;
      dest_x[2] = window_x + window_width - shadow->inner_border_right;
      dest_x[3] = window_x + window_width + shadow->outer_border_right;
    }
  else
    {
      n_x = 1;

      src_x[0] = 0.0;
      src_x[1] = 1.0;

      dest_x[0] = window_x - shadow->outer_border_left;
      dest_x[1] = window_x + window_width + shadow->outer_border_right;
    }

  if (shadow->scale_height)
    {
      n_y = 3;

      src_y[0] = 0.0;
      src_y[1] = (shadow->inner_border_top + shadow->outer_border_top) / texture_height;
      src_y[2] = (texture_height - (shadow->inner_border_bottom + shadow->outer_border_bottom)) / texture_height;
      src_y[3] = 1.0;

      dest_y[0] = window_y - shadow->outer_border_top;
      dest_y[1] = window_y + shadow->inner_border_top;
      dest_y[2] = window_y + window_height - shadow->inner_border_bottom;
      dest_y[3] = window_y + window_height + shadow->outer_border_bottom;
    }
  else
    {
      n_y = 1;

      src_y[0] = 0.0;
      src_y[1] = 1.0;

      dest_y[0] = window_y - shadow->outer_border_top;
      dest_y[1] = window_y + window_height + shadow->outer_border_bottom;
    }

  for (j = 0; j < n_y; j++)
    {
      MtkRectangle dest_rect;
      dest_rect.y = dest_y[j];
      dest_rect.height = dest_y[j + 1] - dest_y[j];

      if (dest_rect.height == 0)
        continue;

      for (i = 0; i < n_x; i++)
        {
          MtkRegionOverlap overlap;

          dest_rect.x = dest_x[i];
          dest_rect.width = dest_x[i + 1] - dest_x[i];

          if (dest_rect.width == 0)
            continue;

          if (clip)
            overlap = mtk_region_contains_rectangle (clip, &dest_rect);
          else
            overlap = MTK_REGION_OVERLAP_IN;

          if (overlap == MTK_REGION_OVERLAP_OUT)
            continue;

          /* There's quite a bit of overhead from allocating a new
           * region in order to find an exact intersection and
           * generating more geometry - we make the assumption that
           * unless we have to clip strictly it will be cheaper to
           * just draw the entire rectangle.
           */
          if (overlap == MTK_REGION_OVERLAP_IN ||
              (overlap == MTK_REGION_OVERLAP_PART && !clip_strictly))
            {
              cogl_framebuffer_draw_textured_rectangle (framebuffer,
                                                        shadow->pipeline,
                                                        dest_x[i], dest_y[j],
                                                        dest_x[i + 1], dest_y[j + 1],
                                                        src_x[i], src_y[j],
                                                        src_x[i + 1], src_y[j + 1]);
            }
          else if (overlap == MTK_REGION_OVERLAP_PART)
            {
              g_autoptr (MtkRegion) intersection = NULL;
              int n_rectangles, k;

              intersection = mtk_region_create_rectangle (&dest_rect);
              mtk_region_intersect (intersection, clip);

              n_rectangles = mtk_region_num_rectangles (intersection);
              for (k = 0; k < n_rectangles; k++)
                {
                  MtkRectangle rect;
                  float src_x1, src_x2, src_y1, src_y2;

                  rect = mtk_region_get_rectangle (intersection, k);

                  /* Separately linear interpolate X and Y coordinates in the source
                   * based on the destination X and Y coordinates */

                  src_x1 = (src_x[i] * (dest_rect.x + dest_rect.width - rect.x) +
                            src_x[i + 1] * (rect.x - dest_rect.x)) / dest_rect.width;
                  src_x2 = (src_x[i] * (dest_rect.x + dest_rect.width - (rect.x + rect.width)) +
                            src_x[i + 1] * (rect.x + rect.width - dest_rect.x)) / dest_rect.width;

                  src_y1 = (src_y[j] * (dest_rect.y + dest_rect.height - rect.y) +
                            src_y[j + 1] * (rect.y - dest_rect.y)) / dest_rect.height;
                  src_y2 = (src_y[j] * (dest_rect.y + dest_rect.height - (rect.y + rect.height)) +
                            src_y[j + 1] * (rect.y + rect.height - dest_rect.y)) / dest_rect.height;

                  cogl_framebuffer_draw_textured_rectangle (framebuffer,
                                                            shadow->pipeline,
                                                            rect.x, rect.y,
                                                            rect.x + rect.width, rect.y + rect.height,
                                                            src_x1, src_y1, src_x2, src_y2);
                }
            }
        }
    }
}

/**
 * meta_shadow_get_bounds:
 * @shadow: a #MetaShadow
 * @window_x: x position of the region to paint a shadow for
 * @window_y: y position of the region to paint a shadow for
 * @window_width: actual width of the region to paint a shadow for
 * @window_height: actual height of the region to paint a shadow for
 *
 * Computes the bounds of the pixels that will be affected by
 * meta_shadow_paint()
 */
void
meta_shadow_get_bounds  (MetaShadow   *shadow,
                         int           window_x,
                         int           window_y,
                         int           window_width,
                         int           window_height,
                         MtkRectangle *bounds)
{
  bounds->x = window_x - shadow->outer_border_left;
  bounds->y = window_y - shadow->outer_border_top;
  bounds->width = window_width + shadow->outer_border_left + shadow->outer_border_right;
  bounds->height = window_height + shadow->outer_border_top + shadow->outer_border_bottom;
}

static void
meta_shadow_class_info_free (MetaShadowClassInfo *class_info)
{
  g_free ((char *)class_info->name);
  g_free (class_info);
}

static void
meta_shadow_factory_init (MetaShadowFactory *factory)
{
  guint i;

  factory->shadows = g_hash_table_new (meta_shadow_cache_key_hash,
                                       meta_shadow_cache_key_equal);

  factory->shadow_classes = g_hash_table_new_full (g_str_hash,
                                                   g_str_equal,
                                                   NULL,
                                                   (GDestroyNotify)meta_shadow_class_info_free);

  for (i = 0; i < G_N_ELEMENTS (default_shadow_classes); i++)
    {
      MetaShadowClassInfo *class_info = g_new0 (MetaShadowClassInfo, 1);

      *class_info = default_shadow_classes[i];
      class_info->name = g_strdup (class_info->name);

      g_hash_table_insert (factory->shadow_classes,
                           (char *)class_info->name, class_info);
    }
}

static void
meta_shadow_factory_finalize (GObject *object)
{
  MetaShadowFactory *factory = META_SHADOW_FACTORY (object);
  GHashTableIter iter;
  gpointer key, value;

  /* Detach from the shadows in the table so we won't try to
   * remove them when they're freed. */
  g_hash_table_iter_init (&iter, factory->shadows);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      MetaShadow *shadow = key;
      shadow->factory = NULL;
    }

  g_hash_table_destroy (factory->shadows);
  g_hash_table_destroy (factory->shadow_classes);

  G_OBJECT_CLASS (meta_shadow_factory_parent_class)->finalize (object);
}

static void
meta_shadow_factory_class_init (MetaShadowFactoryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_shadow_factory_finalize;
}

/**
 * meta_shadow_factory_get_default:
 *
 * Return value: (transfer none): the global singleton shadow factory
 */
MetaShadowFactory *
meta_shadow_factory_get_default (void)
{
  static MetaShadowFactory *factory;

  if (factory == NULL)
    factory = g_object_new (META_TYPE_SHADOW_FACTORY, NULL);

  return factory;
}

/* We emulate a 1D Gaussian blur by using 3 consecutive box blurs;
 * this produces a result that's within 3% of the original and can be
 * implemented much faster for large filter sizes because of the
 * efficiency of implementation of a box blur. Idea and formula
 * for choosing the box blur size come from:
 *
 * http://www.w3.org/TR/SVG/filters.html#feGaussianBlurElement
 *
 * The 2D blur is then done by blurring the rows, flipping the
 * image and blurring the columns. (This is possible because the
 * Gaussian kernel is separable - it's the product of a horizontal
 * blur and a vertical blur.)
 */
static int
get_box_filter_size (int radius)
{
  return (int)(0.5 + radius * (0.75 * sqrt(2*M_PI)));
}

/* The "spread" of the filter is the number of pixels from an original
 * pixel that it's blurred image extends. (A no-op blur that doesn't
 * blur would have a spread of 0.) See comment in blur_rows() for why the
 * odd and even cases are different
 */
static int
get_shadow_spread (int radius)
{
  int d;

  if (radius == 0)
    return 0;

  d = get_box_filter_size (radius);

  if (d % 2 == 1)
    return 3 * (d / 2);
  else
    return 3 * (d / 2) - 1;
}

/* This applies a single box blur pass to a horizontal range of pixels;
 * since the box blur has the same weight for all pixels, we can
 * implement an efficient sliding window algorithm where we add
 * in pixels coming into the window from the right and remove
 * them when they leave the windw to the left.
 *
 * d is the filter width; for even d shift indicates how the blurred
 * result is aligned with the original - does ' x ' go to ' yy' (shift=1)
 * or 'yy ' (shift=-1)
 */
static void
blur_xspan (guchar *row,
            guchar *tmp_buffer,
            int     row_width,
            int     x0,
            int     x1,
            int     d,
            int     shift)
{
  int offset;
  int sum = 0;
  int i;

  if (d % 2 == 1)
    offset = d / 2;
  else
    offset = (d - shift) / 2;

  /* All the conditionals in here look slow, but the branches will
   * be well predicted and there are enough different possibilities
   * that trying to write this as a series of unconditional loops
   * is hard and not an obvious win. The main slow down here seems
   * to be the integer division per pixel; one possible optimization
   * would be to accumulate into two 16-bit integer buffers and
   * only divide down after all three passes. (SSE parallel implementation
   * of the divide step is possible.)
   */
  for (i = x0 - d + offset; i < x1 + offset; i++)
    {
      if (i >= 0 && i < row_width)
        sum += row[i];

      if (i >= x0 + offset)
        {
          if (i >= d)
            sum -= row[i - d];

          tmp_buffer[i - offset] = (sum + d / 2) / d;
        }
    }

  memcpy (row + x0, tmp_buffer + x0, x1 - x0);
}

static void
blur_rows (MtkRegion *convolve_region,
           int        x_offset,
           int        y_offset,
           guchar    *buffer,
           int        buffer_width,
           int        buffer_height,
           int        d)
{
  int i, j;
  int n_rectangles;
  guchar *tmp_buffer;

  tmp_buffer = g_malloc (buffer_width);

  n_rectangles = mtk_region_num_rectangles (convolve_region);
  for (i = 0; i < n_rectangles; i++)
    {
      MtkRectangle rect;

      rect = mtk_region_get_rectangle (convolve_region, i);

      for (j = y_offset + rect.y; j < y_offset + rect.y + rect.height; j++)
        {
          guchar *row = buffer + j * buffer_width;
          int x0 = x_offset + rect.x;
          int x1 = x0 + rect.width;

          /* We want to produce a symmetric blur that spreads a pixel
           * equally far to the left and right. If d is odd that happens
           * naturally, but for d even, we approximate by using a blur
           * on either side and then a centered blur of size d + 1.
           * (technique also from the SVG specification)
           */
          if (d % 2 == 1)
            {
              blur_xspan (row, tmp_buffer, buffer_width, x0, x1, d, 0);
              blur_xspan (row, tmp_buffer, buffer_width, x0, x1, d, 0);
              blur_xspan (row, tmp_buffer, buffer_width, x0, x1, d, 0);
            }
          else
            {
              blur_xspan (row, tmp_buffer, buffer_width, x0, x1, d, 1);
              blur_xspan (row, tmp_buffer, buffer_width, x0, x1, d, -1);
              blur_xspan (row, tmp_buffer, buffer_width, x0, x1, d + 1, 0);
            }
        }
    }

  g_free (tmp_buffer);
}

static void
fade_bytes (guchar *bytes,
            int     width,
            int     distance,
            int     total)
{
  guint32 multiplier = (distance * 0x10000 + 0x8000) / total;
  int i;

  for (i = 0; i < width; i++)
    bytes[i] = (bytes[i] * multiplier) >> 16;
}

/* Swaps width and height. Either swaps in-place and returns the original
 * buffer or allocates a new buffer, frees the original buffer and returns
 * the new buffer.
 */
static guchar *
flip_buffer (guchar *buffer,
             int     width,
             int     height)
{
  /* Working in blocks increases cache efficiency, compared to reading
   * or writing an entire column at once */
#define BLOCK_SIZE 16

  if (width == height)
    {
      int i0, j0;

      for (j0 = 0; j0 < height; j0 += BLOCK_SIZE)
        for (i0 = 0; i0 <= j0; i0 += BLOCK_SIZE)
          {
            int max_j = MIN(j0 + BLOCK_SIZE, height);
            int max_i = MIN(i0 + BLOCK_SIZE, width);
            int i, j;

            if (i0 == j0)
              {
                for (j = j0; j < max_j; j++)
                  for (i = i0; i < j; i++)
                    {
                      guchar tmp = buffer[j * width + i];
                      buffer[j * width + i] = buffer[i * width + j];
                      buffer[i * width + j] = tmp;
                    }
              }
            else
              {
                for (j = j0; j < max_j; j++)
                  for (i = i0; i < max_i; i++)
                    {
                      guchar tmp = buffer[j * width + i];
                      buffer[j * width + i] = buffer[i * width + j];
                      buffer[i * width + j] = tmp;
                    }
              }
          }

      return buffer;
    }
  else
    {
      guchar *new_buffer = g_malloc (height * width);
      int i0, j0;

      for (i0 = 0; i0 < width; i0 += BLOCK_SIZE)
        for (j0 = 0; j0 < height; j0 += BLOCK_SIZE)
          {
            int max_j = MIN(j0 + BLOCK_SIZE, height);
            int max_i = MIN(i0 + BLOCK_SIZE, width);
            int i, j;

            for (i = i0; i < max_i; i++)
              for (j = j0; j < max_j; j++)
                new_buffer[i * height + j] = buffer[j * width + i];
          }

      g_free (buffer);

      return new_buffer;
    }
#undef BLOCK_SIZE
}

static void
add_expanded_rect (MtkRegionBuilder *builder,
                   int               x,
                   int               y,
                   int               width,
                   int               height,
                   int               x_amount,
                   int               y_amount,
                   gboolean          flip)
{
  if (flip)
    mtk_region_builder_add_rectangle (builder,
                                      y - y_amount, x - x_amount,
                                      height + 2 * y_amount, width + 2 * x_amount);
  else
    mtk_region_builder_add_rectangle (builder,
                                      x - x_amount, y - y_amount,
                                      width + 2 * x_amount, height + 2 * y_amount);
}

static MtkRegion *
expand_region (MtkRegion *region,
               int        x_amount,
               int        y_amount,
               gboolean   flip)
{
  MtkRegionBuilder builder;
  int n;
  int i;

  mtk_region_builder_init (&builder);

  n = mtk_region_num_rectangles (region);
  for (i = 0; i < n; i++)
    {
      MtkRectangle rect;

      rect = mtk_region_get_rectangle (region, i);
      add_expanded_rect (&builder,
                         rect.x, rect.y, rect.width, rect.height,
                         x_amount, y_amount, flip);
    }

  return mtk_region_builder_finish (&builder);
}

/* This computes a (clipped version) of the inverse of the region
 * and expands it by the given amount */
static MtkRegion *
expand_region_inverse (MtkRegion *region,
                       int        x_amount,
                       int        y_amount,
                       gboolean   flip)
{
  MtkRegionBuilder builder;
  MtkRegionIterator iter;
  MtkRectangle extents;
  int last_x;

  mtk_region_builder_init (&builder);

  extents = mtk_region_get_extents (region);
  add_expanded_rect (&builder,
                     extents.x, extents.y - 1, extents.width, 1,
                     x_amount, y_amount, flip);
  add_expanded_rect (&builder,
                     extents.x - 1, extents.y, 1, extents.height,
                     x_amount, y_amount, flip);
  add_expanded_rect (&builder,
                     extents.x + extents.width, extents.y, 1, extents.height,
                     x_amount, y_amount, flip);
  add_expanded_rect (&builder,
                     extents.x, extents.y + extents.height, extents.width, 1,
                     x_amount, y_amount, flip);

  last_x = extents.x;
  for (mtk_region_iterator_init (&iter, region);
       !mtk_region_iterator_at_end (&iter);
       mtk_region_iterator_next (&iter))
    {
      if (iter.rectangle.x > last_x)
        add_expanded_rect (&builder,
                           last_x, iter.rectangle.y,
                           iter.rectangle.x - last_x, iter.rectangle.height,
                           x_amount, y_amount, flip);

      if (iter.line_end)
        {
          if (extents.x + extents.width > iter.rectangle.x + iter.rectangle.width)
            add_expanded_rect (&builder,
                               iter.rectangle.x + iter.rectangle.width, iter.rectangle.y,
                               (extents.x + extents.width) - (iter.rectangle.x + iter.rectangle.width), iter.rectangle.height,
                               x_amount, y_amount, flip);
          last_x = extents.x;
        }
      else
        {
          last_x = iter.rectangle.x + iter.rectangle.width;
        }
    }

  return mtk_region_builder_finish (&builder);
}

/**
 * make_border_region:
 * @region: a #MtkRegion
 * @x_amount: distance from the border to extend horizontally
 * @y_amount: distance from the border to extend vertically
 * @flip: if true, the result is computed with x and y interchanged
 *
 * Computes the "border region" of a given region, which is roughly
 * speaking the set of points near the boundary of the region.  If we
 * define the operation of growing a region as computing the set of
 * points within a given manhattan distance of the region, then the
 * border is 'grow(region) intersect grow(inverse(region))'.
 *
 * If we create an image by filling the region with a solid color,
 * the border is the region affected by blurring the region.
 *
 * Return value: a new region which is the border of the given region
 */
static MtkRegion *
make_border_region (MtkRegion *region,
                    int        x_amount,
                    int        y_amount,
                    gboolean   flip)
{
  g_autoptr (MtkRegion) border_region = NULL;
  g_autoptr (MtkRegion) inverse_region = NULL;

  border_region = expand_region (region, x_amount, y_amount, flip);
  inverse_region = expand_region_inverse (region, x_amount, y_amount, flip);
  mtk_region_intersect (border_region, inverse_region);

  return g_steal_pointer (&border_region);
}

static void
make_shadow (MetaShadow  *shadow,
             CoglContext *cogl_context,
             MtkRegion   *region)
{
  GError *error = NULL;
  int d = get_box_filter_size (shadow->key.radius);
  int spread = get_shadow_spread (shadow->key.radius);
  MtkRectangle extents;
  g_autoptr (MtkRegion) row_convolve_region = NULL;
  g_autoptr (MtkRegion) column_convolve_region = NULL;
  guchar *buffer;
  int buffer_width;
  int buffer_height;
  int x_offset;
  int y_offset;
  int n_rectangles, j, k;

  extents = mtk_region_get_extents (region);

  /* In the case where top_fade >= 0 and the portion above the top
   * edge of the shape will be cropped, it seems like we could create
   * a smaller buffer and omit the top portion, but actually, in our
   * multi-pass blur algorithm, the blur into the area above the window
   * in the first pass will contribute back to the final pixel values
   * for the top pixels, so we create a buffer as if we weren't cropping
   * and only crop when creating the CoglTexture.
   */

  buffer_width = extents.width + 2 * spread;
  buffer_height = extents.height + 2 * spread;

  /* Round up so we have aligned rows/columns */
  buffer_width = (buffer_width + 3) & ~3;
  buffer_height = (buffer_height + 3) & ~3;

  /* Square buffer allows in-place swaps, which are roughly 70% faster, but we
   * don't want to over-allocate too much memory.
   */
  if (buffer_height < buffer_width && buffer_height > (3 * buffer_width) / 4)
    buffer_height = buffer_width;
  if (buffer_width < buffer_height && buffer_width > (3 * buffer_height) / 4)
    buffer_width = buffer_height;

  buffer = g_malloc0 (buffer_width * buffer_height);

  /* Blurring with multiple box-blur passes is fast, but (especially for
   * large shadow sizes) we can improve efficiency by restricting the blur
   * to the region that actually needs to be blurred.
   */
  row_convolve_region = make_border_region (region, spread, spread, FALSE);
  column_convolve_region = make_border_region (region, 0, spread, TRUE);

  /* Offsets between coordinates of the regions and coordinates in the buffer */
  x_offset = spread;
  y_offset = spread;

  /* Step 1: unblurred image */
  n_rectangles = mtk_region_num_rectangles (region);
  for (k = 0; k < n_rectangles; k++)
    {
      MtkRectangle rect;

      rect = mtk_region_get_rectangle (region, k);
      for (j = y_offset + rect.y; j < y_offset + rect.y + rect.height; j++)
        memset (buffer + buffer_width * j + x_offset + rect.x, 255, rect.width);
    }

  /* Step 2: swap rows and columns */
  buffer = flip_buffer (buffer, buffer_width, buffer_height);

  /* Step 3: blur rows (really columns) */
  blur_rows (column_convolve_region, y_offset, x_offset,
             buffer, buffer_height, buffer_width,
             d);

  /* Step 4: swap rows and columns */
  buffer = flip_buffer (buffer, buffer_height, buffer_width);

  /* Step 5: blur rows */
  blur_rows (row_convolve_region, x_offset, y_offset,
             buffer, buffer_width, buffer_height,
             d);

  /* Step 6: fade out the top, if applicable */
  if (shadow->key.top_fade >= 0)
    {
      for (j = y_offset; j < y_offset + MIN (shadow->key.top_fade, extents.height + shadow->outer_border_bottom); j++)
        fade_bytes(buffer + j * buffer_width, buffer_width, j - y_offset, shadow->key.top_fade);
    }

  /* We offset the passed in pixels to crop off the extra area we allocated at the top
   * in the case of top_fade >= 0. We also account for padding at the left for symmetry
   * though that doesn't currently occur.
   */
  shadow->texture = cogl_texture_2d_new_from_data (cogl_context,
                                                   shadow->outer_border_left + extents.width + shadow->outer_border_right,
                                                   shadow->outer_border_top + extents.height + shadow->outer_border_bottom,
                                                   COGL_PIXEL_FORMAT_A_8,
                                                   buffer_width,
                                                   (buffer +
                                                    (y_offset - shadow->outer_border_top) * buffer_width +
                                                    (x_offset - shadow->outer_border_left)),
                                                   &error);

  if (error)
    {
      g_warning ("Failed to allocate shadow texture: %s", error->message);
      g_error_free (error);
    }

  g_free (buffer);

  shadow->pipeline = meta_create_texture_pipeline (cogl_context, shadow->texture);
  cogl_pipeline_set_static_name (shadow->pipeline, "MetaShadowFactory");
}

static MetaShadowParams *
get_shadow_params (MetaShadowFactory *factory,
                   const char        *class_name,
                   gboolean           focused,
                   gboolean           create)
{
  MetaShadowClassInfo *class_info = g_hash_table_lookup (factory->shadow_classes,
                                                         class_name);
  if (class_info == NULL)
    {
      if (create)
        {
          class_info = g_new0 (MetaShadowClassInfo, 1);
          *class_info = default_shadow_classes[0];
          class_info->name = g_strdup (class_info->name);

          g_hash_table_insert (factory->shadow_classes,
                               (char *)class_info->name, class_info);
        }
      else
        {
          class_info = &default_shadow_classes[0];
        }
    }

  if (focused)
    return &class_info->focused;
  else
    return &class_info->unfocused;
}

/**
 * meta_shadow_factory_get_shadow:
 * @factory: a #MetaShadowFactory
 * @shape: the size-invariant shape of the window's region
 * @width: the actual width of the window's region
 * @height: the actual height of the window's region
 * @class_name: name of the class of window shadows
 * @focused: whether the shadow is for a focused window
 *
 * Gets the appropriate shadow object for drawing shadows for the
 * specified window shape. The region that we are shadowing is specified
 * as a combination of a size-invariant extracted shape and the size.
 * In some cases, the same shadow object can be shared between sizes;
 * in other cases a different shadow object is used for each size.
 *
 * Return value: (transfer full): a newly referenced #MetaShadow; unref with
 *  meta_shadow_unref()
 */
MetaShadow *
meta_shadow_factory_get_shadow (MetaShadowFactory *factory,
                                MetaWindowShape   *shape,
                                int                width,
                                int                height,
                                const char        *class_name,
                                gboolean           focused,
                                CoglContext       *cogl_context)
{
  MetaShadowParams *params;
  MetaShadowCacheKey key;
  MetaShadow *shadow;
  g_autoptr (MtkRegion) region = NULL;
  int spread;
  int shape_border_top, shape_border_right, shape_border_bottom, shape_border_left;
  int inner_border_top, inner_border_right, inner_border_bottom, inner_border_left;
  int outer_border_top, outer_border_right, outer_border_bottom, outer_border_left;
  gboolean scale_width, scale_height;
  gboolean cacheable;
  int center_width, center_height;

  g_return_val_if_fail (META_IS_SHADOW_FACTORY (factory), NULL);
  g_return_val_if_fail (shape != NULL, NULL);

  /* Using a single shadow texture for different window sizes only works
   * when there is a central scaled area that is greater than twice
   * the spread of the gaussian blur we are applying to get to the
   * shadow image.
   *                         *********          ***********
   *  /----------\         *###########*      *#############*
   *  |          |   =>   **#*********#** => **#***********#**
   *  |          |        **#**     **#**    **#**       **#**
   *  |          |        **#*********#**    **#***********#**
   *  \----------/         *###########*      *#############*
   *                         **********         ************
   *   Original                Blur            Stretched Blur
   *
   * For smaller sizes, we create a separate shadow image for each size;
   * since we assume that there will be little reuse, we don't try to
   * cache such images but just recreate them. (Since the current cache
   * policy is to only keep around referenced shadows, there wouldn't
   * be any harm in caching them, it would just make the book-keeping
   * a bit tricker.)
   *
   * In the case where we are fading a the top, that also has to fit
   * within the top unscaled border.
   */

  params = get_shadow_params (factory, class_name, focused, FALSE);

  spread = get_shadow_spread (params->radius);
  meta_window_shape_get_borders (shape,
                                 &shape_border_top,
                                 &shape_border_right,
                                 &shape_border_bottom,
                                 &shape_border_left);

  inner_border_top = MAX (shape_border_top + spread, params->top_fade);
  outer_border_top = params->top_fade >= 0 ? 0 : spread;
  inner_border_right = shape_border_right + spread;
  outer_border_right = spread;
  inner_border_bottom = shape_border_bottom + spread;
  outer_border_bottom = spread;
  inner_border_left = shape_border_left + spread;
  outer_border_left = spread;

  scale_width = inner_border_left + inner_border_right <= width;
  scale_height = inner_border_top + inner_border_bottom <= height;
  cacheable = scale_width && scale_height;

  if (cacheable)
    {
      key.shape = shape;
      key.radius = params->radius;
      key.top_fade = params->top_fade;

      shadow = g_hash_table_lookup (factory->shadows, &key);
      if (shadow)
        return meta_shadow_ref (shadow);
    }

  shadow = g_new0 (MetaShadow, 1);

  shadow->ref_count = 1;
  shadow->factory = factory;
  shadow->key.shape = meta_window_shape_ref (shape);
  shadow->key.radius = params->radius;
  shadow->key.top_fade = params->top_fade;

  shadow->outer_border_top = outer_border_top;
  shadow->inner_border_top = inner_border_top;
  shadow->outer_border_right = outer_border_right;
  shadow->inner_border_right = inner_border_right;
  shadow->outer_border_bottom = outer_border_bottom;
  shadow->inner_border_bottom = inner_border_bottom;
  shadow->outer_border_left = outer_border_left;
  shadow->inner_border_left = inner_border_left;

  shadow->scale_width = scale_width;
  if (scale_width)
    center_width = inner_border_left + inner_border_right - (shape_border_left + shape_border_right);
  else
    center_width = width - (shape_border_left + shape_border_right);

  shadow->scale_height = scale_height;
  if (scale_height)
    center_height = inner_border_top + inner_border_bottom - (shape_border_top + shape_border_bottom);
  else
    center_height = height - (shape_border_top + shape_border_bottom);

  g_assert (center_width >= 0 && center_height >= 0);

  region = meta_window_shape_to_region (shape, center_width, center_height);
  make_shadow (shadow, cogl_context, region);

  if (cacheable)
    g_hash_table_insert (factory->shadows, &shadow->key, shadow);

  return shadow;
}

/**
 * meta_shadow_factory_get_params:
 * @factory: a #MetaShadowFactory
 * @class_name: name of the class of shadow to get the params for
 * @focused: whether the shadow is for a focused window
 * @params: (out caller-allocates): location to store the current parameter values
 *
 * Gets the shadow parameters for a particular class of shadows
 * for either the focused or unfocused state. If the class name
 * does not name an existing class, default values will be returned
 * without printing an error.
 */
void
meta_shadow_factory_get_params (MetaShadowFactory *factory,
                                const char        *class_name,
                                gboolean           focused,
                                MetaShadowParams  *params)
{
  MetaShadowParams *stored_params;

  g_return_if_fail (META_IS_SHADOW_FACTORY (factory));
  g_return_if_fail (class_name != NULL);

  stored_params = get_shadow_params (factory, class_name, focused, FALSE);

  if (params)
    *params = *stored_params;
}

G_DEFINE_BOXED_TYPE (MetaShadow, meta_shadow,
                     meta_shadow_ref, meta_shadow_unref)
