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
  uint8_t *pixel_lums;
  uint pixel_lums_height;
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
  uint small_line_size, two_lines_size, n_two_lines, n_points, n_lines;
  uint pixel_count = 0;
  guchar *pixels;
  uint8_t *pixel_lums;

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

  /* To save memory, we only store 3/4 of the image and not the full RGB data,
   * but only the luminance of every pixel. Also, the size of the cached data is
   * limited by MAX_CACHED_SIZE.
   * In particular, we store the value of every second pixel for all even rows,
   * and the value of every pixel for all uneven rows. This allows calculating the
   * acutance for 1/4 of all pixels, which is still enough to not miss any small
   * patterns in the image.
   *
   * For example, the cached data of a 10x6 image looks like this:
   *  ----------
   * | x x x x x|
   * |xxxxxxxxxx|
   * | x x x x x|
   * |xxxxxxxxxx|
   * | x x x x x|
   * |          |
   *  ----------
   */

  small_line_size = floor (image_width / 2);
  two_lines_size = small_line_size + image_width;
  n_two_lines = floor ((MAX_CACHED_SIZE * 1000 - small_line_size) / two_lines_size);

  /* We always append another every-second-pixel line to the end */
  if (n_two_lines * 2 + 1 > image_height)
    n_two_lines = floor ((image_height - 1) / 2);

  n_points =  n_two_lines * two_lines_size + small_line_size;
  n_lines = n_two_lines * 2 + 1;

  pixel_lums = g_malloc (sizeof(uint8_t) * n_points);

  for (uint y = 0; y < n_lines; y++)
    for (uint x = 0; x < image_width; x++)
      {
        /* Skip every second (even) column if the row number is even */
        if (y % 2 != 1 && x % 2 != 1)
          continue;

        uint i = y * rowstride + x * n_channels;

        uint8_t r = pixels[i];
        uint8_t g = pixels[i + 1];
        uint8_t b = pixels[i + 2];

        /* The weight of the different colors is taken from elementary's wingpanel,
         * see meta_background_image_get_color_info().
         */
        uint8_t pixel = (0.3 * r + 0.59 * g + 0.11 * b) ;

        pixel_lums[pixel_count] = pixel;
        pixel_count++;
      }

  image->pixel_lums = pixel_lums;
  image->pixel_lums_height = n_lines;

  g_task_return_pointer (task, pixbuf, (GDestroyNotify) g_object_unref);
}

static void
file_loaded (GObject      *source_object,
             GAsyncResult *result,
             gpointer      user_data)
{
  MetaBackgroundImage *image = META_BACKGROUND_IMAGE (source_object);
  GError *error = NULL;
  CoglError *catch_error = NULL;
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
      cogl_error_free (catch_error);
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
  if (image->pixel_lums)
    g_free (image->pixel_lums);

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
 *
 * Gets color information about a specified area of a background image.
 * Calculates the mean luminance, variance of the luminance and the mean
 * acutance of the area.
 * This only works if the requested area is inside the cached part of the
 * image, the size of this part is limited to MAX_CACHED_SIZE.
 *
 * Return value: %TRUE if the calculation was successful, %FALSE if the area
 *  is not completely cached or if the given input was invalid.
 **/
gboolean
meta_background_image_get_color_info (MetaBackgroundImage   *image,
                                      cairo_rectangle_int_t *image_area,
                                      float                 *mean_luminance,
                                      float                 *luminance_variance,
                                      float                 *mean_acutance)
{
  uint texture_width;
  unsigned long int acutance_mean = 0, luminance_mean = 0, luminance_mean_squares = 0;
  uint8_t pixel = 0;
  uint x = 0, y = 0, luminance_values_count = 0, acutance_values_count = 0;

  g_return_val_if_fail (META_IS_BACKGROUND_IMAGE (image), FALSE);

  texture_width = cogl_texture_get_width (image->texture);

  uint small_line_size = floor (texture_width / 2);
  uint two_lines_size = small_line_size + texture_width;

  if (image_area->width == 0 ||
      image_area->x + image_area->width > texture_width ||
      image_area->x < 0)
    return FALSE;

  if (image_area->height == 0 ||
      image_area->y + image_area->height > image->pixel_lums_height ||
      image_area->y < 0)
    return FALSE;

  /* Code to calculate luminance and acutance is from elementary's wingpanel,
   * see wingpanel-interface/Utils.vala - get_background_color_information():
   * https://github.com/elementary/wingpanel/blob/master/wingpanel-interface/Utils.vala
   */
  for (y = image_area->y; y < image_area->y + image_area->height; y++)
    for (x = image_area->x; x < image_area->x + image_area->width; x++)
      {
        /* For uneven row numbers we have every pixel stored, for the others every second pixel */
        if (y % 2 == 1 || x % 2 == 1)
          {
            uint i = floor (y / 2) * two_lines_size;
            i += (y % 2 == 1) ? small_line_size + x :
                                floor (x / 2);

            pixel = image->pixel_lums[i];
            luminance_mean += pixel;
            luminance_mean_squares += pixel * pixel;
            luminance_values_count++;

            /* Only use uneven rows where we store every pixel for calculating acutance.
             * Also skip the edge rows and colums since we look at the four pixels adjacent
             * to the current pixel.
             */
            if (y % 2 == 1 && x % 2 == 1 &&
                y > image_area->y && y < image_area->y + image_area->height - 1 &&
                x > image_area->x && x < image_area->x + image_area->width - 1)
              {
                uint i_above = i - x - small_line_size + (int) (x / 2);
                uint i_below = i - x + texture_width + (int) (x / 2);

                float acutance =
                  (pixel * 4) -
                  (
                    image->pixel_lums[i - 1] +
                    image->pixel_lums[i + 1] +
                    image->pixel_lums[i_above] +
                    image->pixel_lums[i_below]
                  );

                acutance_mean += abs (acutance);
                acutance_values_count++;
              }
          }
      }

  *mean_luminance = (float) luminance_mean / luminance_values_count;
  *luminance_variance = ((float) luminance_mean_squares / luminance_values_count) - (*mean_luminance * *mean_luminance);
  if (acutance_values_count > 0)
    *mean_acutance = (float) acutance_mean / acutance_values_count;

  return TRUE;
}
