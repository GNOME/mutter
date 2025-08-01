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

#include "config.h"

#include "meta-background-image-private.h"

#include <glycin.h>
#include <gio/gio.h>

#ifdef HAVE_MALLOC_TRIM
#include <malloc.h>
#endif

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
 * Caches loading of textures for backgrounds.
 *
 * There's actually nothing background specific about it, other than it is tuned
 * to work well for large images as typically are used for backgrounds.
 */
struct _MetaBackgroundImageCache
{
  GObject parent_instance;

  GHashTable *images;
};

/**
 * MetaBackgroundImage:
 *
 * Represents a loaded or loading background image.
 */
struct _MetaBackgroundImage
{
  GObject parent_instance;
  GFile *file;
  MetaBackgroundImageCache *cache;
  gboolean in_cache;
  gboolean loaded;
  CoglTexture *texture;
  gboolean has_cicp;
  guint8 color_primaries;
  guint8 transfer_characteristics;
  guint8 matrix_coefficients;
  guint8 video_full_range_flag;
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

static CoglPixelFormat
gly_memory_format_to_cogl (GlyMemoryFormat format)
{
  switch ((guint) format)
    {
    case GLY_MEMORY_B8G8R8A8_PREMULTIPLIED: return COGL_PIXEL_FORMAT_BGRA_8888_PRE;
    case GLY_MEMORY_A8R8G8B8_PREMULTIPLIED: return COGL_PIXEL_FORMAT_ARGB_8888_PRE;
    case GLY_MEMORY_R8G8B8A8_PREMULTIPLIED: return COGL_PIXEL_FORMAT_RGBA_8888_PRE;
    case GLY_MEMORY_B8G8R8A8: return COGL_PIXEL_FORMAT_BGRA_8888;
    case GLY_MEMORY_A8R8G8B8: return COGL_PIXEL_FORMAT_ARGB_8888;
    case GLY_MEMORY_R8G8B8A8: return COGL_PIXEL_FORMAT_RGBA_8888;
    case GLY_MEMORY_A8B8G8R8: return COGL_PIXEL_FORMAT_ABGR_8888;
    case GLY_MEMORY_R8G8B8: return COGL_PIXEL_FORMAT_RGB_888;
    case GLY_MEMORY_B8G8R8: return COGL_PIXEL_FORMAT_BGR_888;
    case GLY_MEMORY_R16G16B16A16_PREMULTIPLIED: return COGL_PIXEL_FORMAT_RGBA_16161616_PRE;
    case GLY_MEMORY_R16G16B16A16: return COGL_PIXEL_FORMAT_RGBA_16161616;
    case GLY_MEMORY_R16G16B16A16_FLOAT: return COGL_PIXEL_FORMAT_RGBA_FP_16161616;
    case GLY_MEMORY_R32G32B32A32_FLOAT_PREMULTIPLIED: return COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE;
    case GLY_MEMORY_R32G32B32A32_FLOAT: return COGL_PIXEL_FORMAT_RGBA_FP_32323232;
    default:
      g_assert_not_reached ();
    }
}

static GlyMemoryFormatSelection
glycin_supported_memory_formats (void)
{
  return GLY_MEMORY_SELECTION_B8G8R8A8_PREMULTIPLIED |
         GLY_MEMORY_SELECTION_A8R8G8B8_PREMULTIPLIED |
         GLY_MEMORY_SELECTION_R8G8B8A8_PREMULTIPLIED |
         GLY_MEMORY_SELECTION_B8G8R8A8 |
         GLY_MEMORY_SELECTION_A8R8G8B8 |
         GLY_MEMORY_SELECTION_R8G8B8A8 |
         GLY_MEMORY_SELECTION_A8B8G8R8 |
         GLY_MEMORY_SELECTION_R8G8B8 |
         GLY_MEMORY_SELECTION_B8G8R8 |
         GLY_MEMORY_SELECTION_R16G16B16A16_PREMULTIPLIED |
         GLY_MEMORY_SELECTION_R16G16B16A16 |
         GLY_MEMORY_SELECTION_R16G16B16A16_FLOAT |
         GLY_MEMORY_SELECTION_R32G32B32A32_FLOAT_PREMULTIPLIED |
         GLY_MEMORY_SELECTION_R32G32B32_FLOAT;
}

static void
load_file (GTask               *task,
           MetaBackgroundImage *source,
           gpointer             task_data,
           GCancellable        *cancellable)
{
  g_autoptr (GFileInputStream) stream = NULL;
  g_autoptr (GlyLoader) loader = NULL;
  g_autoptr (GlyImage) image = NULL;
  GlyFrame *frame;
  GError *error = NULL;

  stream = g_file_read (source->file, NULL, &error);
  if (stream == NULL)
    {
      g_task_return_error (task, error);
      return;
    }

  loader = gly_loader_new_for_stream (G_INPUT_STREAM (stream));

  gly_loader_set_accepted_memory_formats (loader, glycin_supported_memory_formats ());

  image = gly_loader_load (loader, &error);
  if (!image)
    {
      g_task_return_error (task, error);
      return;
    }

  frame = gly_image_next_frame (image, &error);
  if (!frame)
    {
      g_task_return_error (task, error);
      return;
    }

  g_task_return_pointer (task, frame, (GDestroyNotify) g_object_unref);
}

static void
file_loaded (GObject      *source_object,
             GAsyncResult *result,
             gpointer      user_data)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  CoglContext *ctx = clutter_backend_get_cogl_context (backend);
  MetaBackgroundImage *image = META_BACKGROUND_IMAGE (source_object);
  g_autoptr (GError) error = NULL;
  g_autoptr (GError) local_error = NULL;
  GTask *task;
  CoglTexture *texture;
  g_autoptr (GlyFrame) frame = NULL;
  int width, height, row_stride;
  GlyMemoryFormat format;
  g_autoptr (GlyCicp) cicp = NULL;
  GBytes *bytes;

  task = G_TASK (result);
  frame = g_task_propagate_pointer (task, &error);

  if (frame == NULL)
    {
      char *uri = g_file_get_uri (image->file);
      g_warning ("Failed to load background '%s': %s",
                 uri, error->message);
      g_free (uri);
      goto out;
    }

  width = gly_frame_get_width (frame);
  height = gly_frame_get_height (frame);
  row_stride = gly_frame_get_stride (frame);
  bytes = gly_frame_get_buf_bytes (frame);
  format = gly_frame_get_memory_format (frame);
  cicp = gly_frame_get_color_cicp (frame);

  texture = meta_create_texture (width, height, ctx,
                                 gly_memory_format_has_alpha (format)
                                   ? COGL_TEXTURE_COMPONENTS_RGBA
                                   : COGL_TEXTURE_COMPONENTS_RGB,
                                 META_TEXTURE_ALLOW_SLICING);

  if (!cogl_texture_set_data (texture,
                              gly_memory_format_to_cogl (format),
                              row_stride,
                              g_bytes_get_data (bytes, NULL), 0,
                              &local_error))
    {
      g_warning ("Failed to create texture for background: %s",
                 local_error->message);
      g_clear_object (&texture);
    }

  image->texture = texture;

  if (cicp)
    {
      image->has_cicp = TRUE;
      image->color_primaries = cicp->color_primaries;
      image->transfer_characteristics = cicp->transfer_characteristics;
      image->matrix_coefficients = cicp->matrix_coefficients;
      image->video_full_range_flag = cicp->video_full_range_flag;
    }
  else
    {
      image->has_cicp = FALSE;
    }

out:
  image->loaded = TRUE;
  g_signal_emit (image, signals[LOADED], 0);
}

/**
 * meta_background_image_cache_load:
 * @cache: a #MetaBackgroundImageCache
 * @file: #GFile to load
 *
 * Loads an image to use as a background, or returns a reference to an
 * image that is already in the process of loading or loaded.
 *
 * In either case, what is returned is a [class@Meta.BackgroundImage] which can be dereferenced
 * to get a [class@Cogl.Texture]. If [method@Meta.BackgroundImage.is_loaded] returns %TRUE,
 * the background is loaded, otherwise the [signal@Meta.BackgroundImage::loaded]
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
    g_object_unref (image->texture);
  if (image->file)
    g_object_unref (image->file);

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

static gboolean
cicp_primaries_to_clutter (guint8              color_primaries,
                           ClutterColorimetry *colorimetry)
{
  static ClutterPrimaries p3_primaries = {
    .r_x = 0.68f,   .r_y = 0.32f,
    .g_x = 0.265f,  .g_y = 0.69f,
    .b_x = 0.15f,   .b_y = 0.06f,
    .w_x = 0.3127f, .w_y = 0.329f,
  };
  static ClutterPrimaries pal_primaries = {
    .r_x = 0.64f,   .r_y = 0.33f,
    .g_x = 0.29f,   .g_y = 0.60f,
    .b_x = 0.15f,   .b_y = 0.06f,
    .w_x = 0.3127f, .w_y = 0.329f,
  };

  switch (color_primaries)
    {
    case 1:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_SRGB;
      return TRUE;
    case 5:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_PRIMARIES;
      colorimetry->primaries = &pal_primaries;
      return TRUE;
    case 6:
    case 7:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_NTSC;
      return TRUE;
    case 9:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_BT2020;
      return TRUE;
    case 12:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_PRIMARIES;
      colorimetry->primaries = &p3_primaries;
      return TRUE;
    default:
      g_warning ("Unhandled cicp color primaries: %u", color_primaries);
      return FALSE;
    }
}

static gboolean
cicp_tf_to_clutter (guint8       transfer_characteristics,
                    ClutterEOTF *eotf)
{
  switch (transfer_characteristics)
    {
    case 1:
    case 6:
    case 14:
    case 15:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_BT709;
      return TRUE;
    case 4:
      eotf->type = CLUTTER_EOTF_TYPE_GAMMA;
      eotf->gamma_exp = 2.2f;
      return TRUE;
    case 5:
      eotf->type = CLUTTER_EOTF_TYPE_GAMMA;
      eotf->gamma_exp = 2.8f;
      return TRUE;
    case 8:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_LINEAR;
      return TRUE;
    case 13:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_SRGB;
      return TRUE;
    case 16:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_PQ;
      return TRUE;
    case 18: /* hlg */
      break;
    default:
      g_warning ("Unhandled cicp transfer characteristics: %u", transfer_characteristics);
      return FALSE;
    }
}

static ClutterColorState *
clutter_color_state_new_from_cicp (ClutterContext *ctx,
                                   guint8          color_primaries,
                                   guint8          transfer_characteristics,
                                   guint8          matrix_coefficients,
                                   guint8          video_full_range_flag)
{
  ClutterColorimetry colorimetry;
  ClutterEOTF eotf;
  ClutterLuminance lum;

  if (!cicp_primaries_to_clutter (color_primaries, &colorimetry))
    return NULL;

  if (!cicp_tf_to_clutter (transfer_characteristics, &eotf))
    return NULL;

  if (matrix_coefficients != 0)
    {
      g_warning ("Unhandled cicp matrix coefficients: %u", matrix_coefficients);
      return NULL;
    }

  lum.type = CLUTTER_LUMINANCE_TYPE_DERIVED;

  return clutter_color_state_params_new_from_primitives (NULL,
                                                         colorimetry,
                                                         eotf,
                                                         lum);
}

ClutterColorState *
meta_background_image_get_color_state (MetaBackgroundImage *image,
                                       ClutterContext      *ctx)
{
  g_return_val_if_fail (META_IS_BACKGROUND_IMAGE (image), NULL);

  if (image->has_cicp)
    return clutter_color_state_new_from_cicp (ctx,
                                              image->color_primaries,
                                              image->transfer_characteristics,
                                              image->matrix_coefficients,
                                              image->video_full_range_flag);

  return NULL;
}
