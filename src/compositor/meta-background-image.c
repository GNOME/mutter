/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright 2014 Red Hat, Inc.
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
 * SECTION:meta-background-image
 * @title: MetaBackgroundImage
 * @short_description: objects holding images loaded from files, used for backgrounds
 */

#include "config.h"

#include "meta/meta-background-image.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>

#include "clutter/clutter.h"
#include "compositor/cogl-utils.h"

enum
{
  LOADED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define STATS_TILE_SIZE 16

typedef struct {
  guint32 luminance_sum_squares;
  guint32 acutance_sum_squares;
  guint16 luminance_sum;
  guint16 acutance_sum;
} BackgroundStatsTile;

/**
 * MetaBackgroundImageCache:
 *
 * #MetaBackgroundImageCache caches loading of textures for backgrounds; there's actually
 * nothing background specific about it, other than it is tuned to work well for
 * large images as typically are used for backgrounds.
 */
struct _MetaBackgroundImageCache
{
  GObject parent_instance;

  GHashTable *images;
};

/**
 * MetaBackgroundImage:
 *
 * #MetaBackgroundImage is an object that represents a loaded or loading background image.
 */
struct _MetaBackgroundImage
{
  GObject parent_instance;
  GFile *file;
  MetaBackgroundImageCache *cache;
  gboolean in_cache;
  gboolean loaded;
  CoglTexture *texture;

  BackgroundStatsTile *stats;
  uint n_stats_tiles;
};

G_DEFINE_TYPE (MetaBackgroundImageCache, meta_background_image_cache, G_TYPE_OBJECT);

static void
meta_background_image_cache_init (MetaBackgroundImageCache *cache)
{
  cache->images = g_hash_table_new (g_file_hash, (GEqualFunc) g_file_equal);
}

static void
meta_background_image_cache_finalize (GObject *object)
{
  MetaBackgroundImageCache *cache = META_BACKGROUND_IMAGE_CACHE (object);
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, cache->images);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      MetaBackgroundImage *image = value;
      image->in_cache = FALSE;
    }

  g_hash_table_destroy (cache->images);

  G_OBJECT_CLASS (meta_background_image_cache_parent_class)->finalize (object);
}

static void
meta_background_image_cache_class_init (MetaBackgroundImageCacheClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_background_image_cache_finalize;
}

/**
 * meta_background_image_cache_get_default:
 *
 * Return value: (transfer none): the global singleton background cache
 */
MetaBackgroundImageCache *
meta_background_image_cache_get_default (void)
{
  static MetaBackgroundImageCache *cache;

  if (cache == NULL)
    cache = g_object_new (META_TYPE_BACKGROUND_IMAGE_CACHE, NULL);

  return cache;
}

/* The maximum size of the image-part we cache in kilobytes starting from the upper left corner.
 * We use it to calculate luminance and acutance values for requested areas of the image.
 */
#define MAX_CACHED_SIZE 1000

static void
load_file (GTask               *task,
           MetaBackgroundImage *image,
           gpointer             task_data,
           GCancellable        *cancellable)
{
  GError *error = NULL;
  GdkPixbuf *pixbuf;
  GFileInputStream *stream;
  uint image_width, image_height, n_channels, rowstride;
  guchar *pixels;
  guint tiles_rowstride, n_tiles;
  g_autofree guchar *luminance_rows = NULL;

  stream = g_file_read (image->file, NULL, &error);
  if (stream == NULL)
    {
      g_task_return_error (task, error);
      return;
    }

  pixbuf = gdk_pixbuf_new_from_stream (G_INPUT_STREAM (stream), NULL, &error);
  g_object_unref (stream);

  if (pixbuf == NULL)
    {
      g_task_return_error (task, error);
      return;
    }

  image_width = gdk_pixbuf_get_width (pixbuf);
  image_height = gdk_pixbuf_get_height (pixbuf);
  n_channels = gdk_pixbuf_get_n_channels (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  pixels = gdk_pixbuf_get_pixels (pixbuf);

  /* We now calculate the values needed for statistics in tiles of the size
   * STATS_TILE_SIZE.
   */

  tiles_rowstride = (image_width + STATS_TILE_SIZE - 1) / STATS_TILE_SIZE;
  n_tiles = tiles_rowstride * (image_height + STATS_TILE_SIZE - 1) / STATS_TILE_SIZE;

  if (n_tiles * sizeof (BackgroundStatsTile) > MAX_CACHED_SIZE * 1000)
    n_tiles = (MAX_CACHED_SIZE * 1000) / sizeof (BackgroundStatsTile);

  image->stats = g_new0 (BackgroundStatsTile, n_tiles);
  image->n_stats_tiles = n_tiles;

  /* Cached luminance of current and last row */
  luminance_rows = g_malloc (image_width * 2);

  for (uint y = 0; y < image_height; y++)
    for (uint x = 0; x < image_width; x++)
      {
        BackgroundStatsTile *tile, *tile_left, *tile_above;
        uint i = y * rowstride + x * n_channels;
        uint i_tile = x / STATS_TILE_SIZE + y / STATS_TILE_SIZE * tiles_rowstride;

        if (i_tile > n_tiles)
          break;

        uint8_t r = pixels[i];
        uint8_t g = pixels[i + 1];
        uint8_t b = pixels[i + 2];

        /* Calculate and cache luminance */
        uint8_t luminance = (0.299 * r + 0.587 * g + 0.114 * b);
        luminance_rows[x + (y % 2) * image_width] = luminance;

        tile = &image->stats[i_tile];
        tile_left = &image->stats[(x - 1) / STATS_TILE_SIZE + y / STATS_TILE_SIZE * tiles_rowstride];
        tile_above = &image->stats[x / STATS_TILE_SIZE + (y - 1) / STATS_TILE_SIZE * tiles_rowstride];

        tile->luminance_sum += luminance;
        tile->luminance_sum_squares += (guint32) luminance * luminance;

        /* Calculate actuance towards the left and top pixel */
        if (x > 0 && x < image_width - 1 &&
            y > 0 && y < image_height - 1)
          {
            uint8_t acutance_left = ABS (luminance - luminance_rows[x - 1 + (y % 2) * image_width]);
            uint8_t acutance_top = ABS (luminance - luminance_rows[x + ((y - 1) % 2) * image_width]);

            tile->acutance_sum += acutance_left / 4 + acutance_top / 4;

            tile->acutance_sum_squares += ((guint32) acutance_left * acutance_left) / 4;
            tile->acutance_sum_squares += ((guint32) acutance_top * acutance_top) / 4;
          }

        /* Update acutance of the tile left of the current pixel */
        if (y > 0 && y < image_height - 1 && x > 1)
          {
            uint8_t acutance_right = ABS (luminance_rows[x - 1 + (y % 2) * image_width] - luminance);

            tile_left->acutance_sum += acutance_right / 4;
            tile_left->acutance_sum_squares += ((guint32) acutance_right * acutance_right) / 4;
          }

        /* Update acutance of the tile above the current pixel */
        if (x > 0 && x < image_width - 1 && y > 1)
          {
            uint8_t acutance_bottom = ABS (luminance_rows[x + ((y - 1) % 2) * image_width] - luminance);

            tile_above->acutance_sum += acutance_bottom / 4;
            tile_above->acutance_sum_squares += ((guint32) acutance_bottom * acutance_bottom) / 4;
          }
     }

  g_task_return_pointer (task, pixbuf, (GDestroyNotify) g_object_unref);
}

static void
file_loaded (GObject      *source_object,
             GAsyncResult *result,
             gpointer      user_data)
{
  MetaBackgroundImage *image = META_BACKGROUND_IMAGE (source_object);
  GError *error = NULL;
  GError *catch_error = NULL;
  GTask *task;
  CoglTexture *texture;
  GdkPixbuf *pixbuf, *rotated;
  int width, height, row_stride;
  guchar *pixels;
  gboolean has_alpha;

  task = G_TASK (result);
  pixbuf = g_task_propagate_pointer (task, &error);

  if (pixbuf == NULL)
    {
      char *uri = g_file_get_uri (image->file);
      g_warning ("Failed to load background '%s': %s",
                 uri, error->message);
      g_clear_error (&error);
      g_free (uri);
      goto out;
    }

  rotated = gdk_pixbuf_apply_embedded_orientation (pixbuf);
  if (rotated != NULL)
    {
      g_object_unref (pixbuf);
      pixbuf = rotated;
    }

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  row_stride = gdk_pixbuf_get_rowstride (pixbuf);
  pixels = gdk_pixbuf_get_pixels (pixbuf);
  has_alpha = gdk_pixbuf_get_has_alpha (pixbuf);

  texture = meta_create_texture (width, height,
                                 has_alpha ? COGL_TEXTURE_COMPONENTS_RGBA : COGL_TEXTURE_COMPONENTS_RGB,
                                 META_TEXTURE_ALLOW_SLICING);

  if (!cogl_texture_set_data (texture,
                              has_alpha ? COGL_PIXEL_FORMAT_RGBA_8888 : COGL_PIXEL_FORMAT_RGB_888,
                              row_stride,
                              pixels, 0,
                              &catch_error))
    {
      g_warning ("Failed to create texture for background");
      g_error_free (catch_error);
      cogl_object_unref (texture);
    }

  image->texture = texture;

out:
  if (pixbuf != NULL)
    g_object_unref (pixbuf);

  image->loaded = TRUE;
  g_signal_emit (image, signals[LOADED], 0);
}

/**
 * meta_background_image_cache_load:
 * @cache: a #MetaBackgroundImageCache
 * @file: #GFile to load
 *
 * Loads an image to use as a background, or returns a reference to an
 * image that is already in the process of loading or loaded. In either
 * case, what is returned is a #MetaBackgroundImage which can be derefenced
 * to get a #CoglTexture. If meta_background_image_is_loaded() returns %TRUE,
 * the background is loaded, otherwise the MetaBackgroundImage::loaded
 * signal will be emitted exactly once. The 'loaded' state means that the
 * loading process finished, whether it succeeded or failed.
 *
 * Return value: (transfer full): a #MetaBackgroundImage to dereference to get the loaded texture
 */
MetaBackgroundImage *
meta_background_image_cache_load (MetaBackgroundImageCache *cache,
                                  GFile                    *file)
{
  MetaBackgroundImage *image;
  GTask *task;

  g_return_val_if_fail (META_IS_BACKGROUND_IMAGE_CACHE (cache), NULL);
  g_return_val_if_fail (file != NULL, NULL);

  image = g_hash_table_lookup (cache->images, file);
  if (image != NULL)
    return g_object_ref (image);

  image = g_object_new (META_TYPE_BACKGROUND_IMAGE, NULL);
  image->cache = cache;
  image->in_cache = TRUE;
  image->file = g_object_ref (file);
  g_hash_table_insert (cache->images, image->file, image);

  task = g_task_new (image, NULL, file_loaded, NULL);

  g_task_run_in_thread (task, (GTaskThreadFunc) load_file);
  g_object_unref (task);

  return image;
}

/**
 * meta_background_image_cache_purge:
 * @cache: a #MetaBackgroundImageCache
 * @file: file to remove from the cache
 *
 * Remove an entry from the cache; this would be used if monitoring
 * showed that the file changed.
 */
void
meta_background_image_cache_purge (MetaBackgroundImageCache *cache,
                                   GFile                    *file)
{
  MetaBackgroundImage *image;

  g_return_if_fail (META_IS_BACKGROUND_IMAGE_CACHE (cache));
  g_return_if_fail (file != NULL);

  image = g_hash_table_lookup (cache->images, file);
  if (image == NULL)
    return;

  g_hash_table_remove (cache->images, image->file);
  image->in_cache = FALSE;
}

G_DEFINE_TYPE (MetaBackgroundImage, meta_background_image, G_TYPE_OBJECT);

static void
meta_background_image_init (MetaBackgroundImage *image)
{
}

static void
meta_background_image_finalize (GObject *object)
{
  MetaBackgroundImage *image = META_BACKGROUND_IMAGE (object);

  if (image->in_cache)
    g_hash_table_remove (image->cache->images, image->file);

  if (image->texture)
    cogl_object_unref (image->texture);
  if (image->file)
    g_object_unref (image->file);
  g_clear_pointer (&image->stats, g_free);

  G_OBJECT_CLASS (meta_background_image_parent_class)->finalize (object);
}

static void
meta_background_image_class_init (MetaBackgroundImageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_background_image_finalize;

  signals[LOADED] =
    g_signal_new ("loaded",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

/**
 * meta_background_image_is_loaded:
 * @image: a #MetaBackgroundImage
 *
 * Return value: %TRUE if loading has already completed, %FALSE otherwise
 */
gboolean
meta_background_image_is_loaded (MetaBackgroundImage *image)
{
  g_return_val_if_fail (META_IS_BACKGROUND_IMAGE (image), FALSE);

  return image->loaded;
}

/**
 * meta_background_image_get_success:
 * @image: a #MetaBackgroundImage
 *
 * This function is a convenience function for checking for success,
 * without having to call meta_background_image_get_texture() and
 * handle the return of a Cogl type.
 *
 * Return value: %TRUE if loading completed successfully, otherwise %FALSE
 */
gboolean
meta_background_image_get_success (MetaBackgroundImage *image)
{
  g_return_val_if_fail (META_IS_BACKGROUND_IMAGE (image), FALSE);

  return image->texture != NULL;
}

/**
 * meta_background_image_get_texture:
 * @image: a #MetaBackgroundImage
 *
 * Return value: (transfer none): a #CoglTexture if loading succeeded; if
 *  loading failed or has not yet finished, %NULL.
 */
CoglTexture *
meta_background_image_get_texture (MetaBackgroundImage *image)
{
  g_return_val_if_fail (META_IS_BACKGROUND_IMAGE (image), NULL);

  return image->texture;
}

/**
 * meta_background_image_get_color_info:
 * @image: A #MetaBackgroundImage
 * @image_area: The area of the image to analyze as #cairo_rectangle_int_t
 * @mean_luminance: (out): The mean luminance as #float
 * @luminance_variance: (out): Variance of the luminance as #float
 * @mean_acutance: (out): The mean acutance as #float
 * @acutance_variance: (out): Variance of the acutance as #float
 *
 * Gets color information about a specified area of a background image.
 * Calculates the mean luminance, variance of the luminance, the mean
 * acutance and the variance of the acutance of the area.
 * This only works if the requested area is inside the cached part of the
 * image, the size of this part is limited by MAX_CACHED_SIZE.
 *
 * Return value: %TRUE if the calculation was successful, %FALSE if the area
 *  is not completely cached or if the given input was invalid.
 **/
gboolean
meta_background_image_get_color_info (MetaBackgroundImage   *image,
                                      cairo_rectangle_int_t *image_area,
                                      float                 *mean_luminance,
                                      float                 *luminance_variance,
                                      float                 *mean_acutance,
                                      float                 *acutance_variance)
{
  uint texture_width, texture_height;
  guint tiles_rowstride;
  cairo_rectangle_int_t tile_area;
  guint64 acutance_sum = 0, luminance_sum = 0, luminance_sum_squares = 0, acutance_sum_squares = 0;
  guint x = 0, y = 0, values_count = 0, acutance_values_count = 0;
  guint result_width, result_height;

  g_return_val_if_fail (META_IS_BACKGROUND_IMAGE (image), FALSE);

  texture_width = cogl_texture_get_width (image->texture);
  texture_height = cogl_texture_get_height (image->texture);

  if (image_area->width == 0 ||
      image_area->x + image_area->width > texture_width ||
      image_area->x < 0)
    return FALSE;

  if (image_area->height == 0 ||
      image_area->y + image_area->height > texture_height ||
      image_area->y < 0)
    return FALSE;

  tiles_rowstride = (texture_width + STATS_TILE_SIZE - 1) / STATS_TILE_SIZE;

  tile_area.x = image_area->x / STATS_TILE_SIZE;
  tile_area.y = image_area->y / STATS_TILE_SIZE;
  tile_area.width = (image_area->width + STATS_TILE_SIZE - 1) / STATS_TILE_SIZE;
  tile_area.height = (image_area->height + STATS_TILE_SIZE - 1) / STATS_TILE_SIZE;

  if (tile_area.x + tile_area.width + (tile_area.y + tile_area.height) * tiles_rowstride > image->n_stats_tiles)
    return FALSE;

  for (y = tile_area.y; y < tile_area.y + tile_area.height; y++)
    for (x = tile_area.x; x < tile_area.x + tile_area.width; x++)
      {
        BackgroundStatsTile *tile = &image->stats[x + y * tiles_rowstride];
        guint tile_width = MIN (texture_width - x * STATS_TILE_SIZE, STATS_TILE_SIZE);
        guint tile_height = MIN (texture_height - y * STATS_TILE_SIZE, STATS_TILE_SIZE);

        luminance_sum += tile->luminance_sum;
        luminance_sum_squares += tile->luminance_sum_squares;
        acutance_sum += tile->acutance_sum;
        acutance_sum_squares += tile->acutance_sum_squares;
        values_count += tile_width * tile_height;
      }

  acutance_values_count = values_count;
  result_width = tile_area.width * STATS_TILE_SIZE;
  result_height = tile_area.height * STATS_TILE_SIZE;

  if (image_area->x == 0)
    acutance_values_count -= result_height;

  if (image_area->x + image_area->width == texture_width)
    acutance_values_count -= result_height;

  if (image_area->y == 0)
    acutance_values_count -= result_width;

  if (image_area->y + image_area->height == texture_height)
    acutance_values_count -= result_width;

  *mean_luminance = (double) luminance_sum / values_count;
  *luminance_variance = ((double) luminance_sum_squares / values_count) - (*mean_luminance * *mean_luminance);
  *mean_acutance = (double) acutance_sum / acutance_values_count;
  *acutance_variance = ((double) acutance_sum_squares / acutance_values_count) - (*mean_acutance * *mean_acutance);

  return TRUE;
}
