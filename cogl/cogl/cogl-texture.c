/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 *
 * Authors:
 *  Matthew Allum  <mallum@openedhand.com>
 *  Neil Roberts   <neil@linux.intel.com>
 *  Robert Bragg   <robert@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-util.h"
#include "cogl/cogl-bitmap.h"
#include "cogl/cogl-bitmap-private.h"
#include "cogl/cogl-buffer-private.h"
#include "cogl/cogl-enum-types.h"
#include "cogl/cogl-pixel-buffer-private.h"
#include "cogl/cogl-private.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-texture-driver.h"
#include "cogl/cogl-texture-2d-sliced-private.h"
#include "cogl/cogl-texture-2d-private.h"
#include "cogl/cogl-sub-texture-private.h"
#include "cogl/cogl-atlas-texture-private.h"
#include "cogl/cogl-pipeline.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-offscreen-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-sub-texture.h"
#include "cogl/driver/gl/cogl-texture-driver-gl-private.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

typedef struct _CoglTexturePrivate
{
  CoglContext *context;
  CoglTextureDriver *tex_driver;
  CoglTextureLoader *loader;
  GList *framebuffers;
  int max_level_set;
  int max_level_requested;
  int width;
  int height;
  gboolean allocated;

  /*
   * Internal format
   */
  CoglTextureComponents components;
  unsigned int premultiplied : 1;
} CoglTexturePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (CoglTexture, cogl_texture, G_TYPE_OBJECT)

enum
{
  PROP_0,

  PROP_CONTEXT,
  PROP_TEXTURE_DRIVER,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_LOADER,
  PROP_FORMAT,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

static void
cogl_texture_loader_free (CoglTextureLoader *loader)
{
  switch (loader->src_type)
    {
    case COGL_TEXTURE_SOURCE_TYPE_SIZE:
    case COGL_TEXTURE_SOURCE_TYPE_EGL_IMAGE:
    case COGL_TEXTURE_SOURCE_TYPE_EGL_IMAGE_EXTERNAL:
      break;
    case COGL_TEXTURE_SOURCE_TYPE_BITMAP:
      g_clear_object (&loader->src.bitmap.bitmap);
      break;
    }
  g_free (loader);
}

static void
cogl_texture_dispose (GObject *object)
{
  CoglTexture *texture = COGL_TEXTURE (object);
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);

  g_clear_pointer (&priv->loader, cogl_texture_loader_free);
  g_clear_object (&priv->tex_driver);

  G_OBJECT_CLASS (cogl_texture_parent_class)->dispose (object);
}

static void
cogl_texture_set_property (GObject      *gobject,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  CoglTexture *texture = COGL_TEXTURE (gobject);
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      priv->context = g_value_get_object (value);
      break;

    case PROP_TEXTURE_DRIVER:
      priv->tex_driver = g_value_get_object (value);
      break;

    case PROP_WIDTH:
      priv->width = g_value_get_int (value);
      break;

    case PROP_HEIGHT:
      priv->height = g_value_get_int (value);
      break;

    case PROP_LOADER:
      priv->loader = g_value_get_pointer (value);
      break;

    case PROP_FORMAT:
      _cogl_texture_set_internal_format (texture, g_value_get_enum (value));
      /* Although we want to initialize texture::components according
      * to the source format, we always want the internal layout to
      * be considered premultiplied by default.
      *
      * NB: this ->premultiplied state is user configurable so to avoid
      * awkward documentation, setting this to 'true' does not depend on
      * ->components having an alpha component (we will simply ignore the
      * premultiplied status later if there is no alpha component).
      * This way we don't have to worry about updating the
      * ->premultiplied state in _set_components().  Similarly we don't
      * have to worry about updating the ->components state in
      * _set_premultiplied().
      */
      priv->premultiplied = TRUE;
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
cogl_texture_class_init (CoglTextureClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = cogl_texture_dispose;
  gobject_class->set_property = cogl_texture_set_property;

  obj_props[PROP_CONTEXT] =
    g_param_spec_object ("context", NULL, NULL,
                         COGL_TYPE_CONTEXT,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_TEXTURE_DRIVER] =
    g_param_spec_object ("texture-driver", NULL, NULL,
                         COGL_TYPE_TEXTURE_DRIVER,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_WIDTH] =
    g_param_spec_int ("width", NULL, NULL,
                      -1, G_MAXINT,
                      -1,
                      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_STATIC_STRINGS);
  obj_props[PROP_HEIGHT] =
    g_param_spec_int ("height", NULL, NULL,
                      -1, G_MAXINT,
                      -1,
                      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_STATIC_STRINGS);
  obj_props[PROP_LOADER] =
    g_param_spec_pointer ("loader", NULL, NULL,
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);
  obj_props[PROP_FORMAT] =
    g_param_spec_enum ("format", NULL, NULL,
                       COGL_TYPE_PIXEL_FORMAT,
                       COGL_PIXEL_FORMAT_ANY,
                       G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     obj_props);
}

static void
cogl_texture_init (CoglTexture *texture)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);

  priv->max_level_set = 0;
  priv->max_level_requested = 1000; /* OpenGL default GL_TEXTURE_MAX_LEVEL */
  priv->allocated = FALSE;
  priv->framebuffers = NULL;
}

uint32_t
cogl_texture_error_quark (void)
{
  return g_quark_from_static_string ("cogl-texture-error-quark");
}

CoglTextureLoader *
cogl_texture_loader_new (CoglTextureSourceType src_type)
{
  CoglTextureLoader *loader;

  loader = g_new0 (CoglTextureLoader, 1);
  loader->src_type = src_type;

  return loader;
}

gboolean
_cogl_texture_needs_premult_conversion (CoglPixelFormat src_format,
                                        CoglPixelFormat dst_format)
{
  return ((src_format & dst_format & COGL_A_BIT) &&
          src_format != COGL_PIXEL_FORMAT_A_8 &&
          dst_format != COGL_PIXEL_FORMAT_A_8 &&
          (src_format & COGL_PREMULT_BIT) !=
          (dst_format & COGL_PREMULT_BIT));
}

gboolean
cogl_texture_is_get_data_supported (CoglTexture *texture)
{
  if (COGL_TEXTURE_GET_CLASS (texture)->is_get_data_supported)
    return COGL_TEXTURE_GET_CLASS (texture)->is_get_data_supported (texture);
  else
    return TRUE;
}

unsigned int
cogl_texture_get_width (CoglTexture *texture)
{
  CoglTexturePrivate *priv;

  g_return_val_if_fail (COGL_IS_TEXTURE (texture), 0);

  priv = cogl_texture_get_instance_private (texture);
  return priv->width;
}

unsigned int
cogl_texture_get_height (CoglTexture *texture)
{
  CoglTexturePrivate *priv;

  g_return_val_if_fail (COGL_IS_TEXTURE (texture), 0);

  priv = cogl_texture_get_instance_private (texture);
  return priv->height;
}

CoglPixelFormat
cogl_texture_get_format (CoglTexture *texture)
{
  if (!cogl_texture_is_allocated (texture))
    cogl_texture_allocate (texture, NULL);

  return COGL_TEXTURE_GET_CLASS (texture)->get_format (texture);
}

static inline unsigned int
_cogl_util_fls (unsigned int n)
{
   return n == 0 ? 0 : sizeof (unsigned int) * 8 - __builtin_clz (n);
}

int
_cogl_texture_get_n_levels (CoglTexture *texture)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);

  int width = cogl_texture_get_width (texture);
  int height = cogl_texture_get_height (texture);
  int max_dimension = MAX (width, height);
  int n_levels = _cogl_util_fls (max_dimension);

  return MIN (n_levels, priv->max_level_requested + 1);
}

void
cogl_texture_set_max_level (CoglTexture *texture,
                            int          max_level)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);

  priv->max_level_requested = max_level;
}

void
_cogl_texture_get_level_size (CoglTexture *texture,
                              int level,
                              int *width,
                              int *height,
                              int *depth)
{
  int current_width = cogl_texture_get_width (texture);
  int current_height = cogl_texture_get_height (texture);
  int current_depth = 0;
  int i;

  /* NB: The OpenGL spec (like D3D) uses a floor() convention to
   * round down the size of a mipmap level when dividing the size
   * of the previous level results in a fraction...
   */
  for (i = 0; i < level; i++)
    {
      current_width = MAX (1, current_width >> 1);
      current_height = MAX (1, current_height >> 1);
      current_depth = MAX (1, current_depth >> 1);
    }

  if (width)
    *width = current_width;
  if (height)
    *height = current_height;
  if (depth)
    *depth = current_depth;
}

gboolean
cogl_texture_is_sliced (CoglTexture *texture)
{
  g_return_val_if_fail (COGL_IS_TEXTURE (texture), FALSE);

  if (!cogl_texture_is_allocated (texture))
    cogl_texture_allocate (texture, NULL);

  return COGL_TEXTURE_GET_CLASS (texture)->is_sliced (texture);
}

/* If this returns FALSE, that implies _foreach_sub_texture_in_region
 * will be needed to iterate over multiple sub textures for regions whose
 * texture coordinates extend out of the range [0,1]
 */
gboolean
_cogl_texture_can_hardware_repeat (CoglTexture *texture)
{
  if (!cogl_texture_is_allocated (texture))
    cogl_texture_allocate (texture, NULL);
  return COGL_TEXTURE_GET_CLASS (texture)->can_hardware_repeat (texture);
}

gboolean
cogl_texture_get_gl_texture (CoglTexture *texture,
			     GLuint *out_gl_handle,
			     GLenum *out_gl_target)
{
  g_return_val_if_fail (COGL_IS_TEXTURE (texture), FALSE);

  if (!cogl_texture_is_allocated (texture))
    cogl_texture_allocate (texture, NULL);

  return COGL_TEXTURE_GET_CLASS (texture)->get_gl_texture (texture,
                                                           out_gl_handle,
                                                           out_gl_target);
}

void
_cogl_texture_pre_paint (CoglTexture *texture, CoglTexturePrePaintFlags flags)
{
  /* Assert that the storage for the texture exists already if we're
   * about to reference it for painting.
   *
   * Note: we abort on error here since it's a bit late to do anything
   * about it if we fail to allocate the texture and the app could
   * have explicitly allocated the texture earlier to handle problems
   * gracefully.
   *
   * XXX: Maybe it could even be considered a programmer error if the
   * texture hasn't been allocated by this point since it implies we
   * are about to paint with undefined texture contents?
   */
  cogl_texture_allocate (texture, NULL);

  COGL_TEXTURE_GET_CLASS (texture)->pre_paint (texture,
                                               flags);
}

gboolean
_cogl_texture_set_region_from_bitmap (CoglTexture *texture,
                                      int src_x,
                                      int src_y,
                                      int width,
                                      int height,
                                      CoglBitmap *bmp,
                                      int dst_x,
                                      int dst_y,
                                      int level,
                                      GError **error)
{
  g_return_val_if_fail (cogl_bitmap_get_width (bmp) - src_x >= width, FALSE);
  g_return_val_if_fail (cogl_bitmap_get_height (bmp) - src_y >= height, FALSE);
  g_return_val_if_fail (width > 0, FALSE);
  g_return_val_if_fail (height > 0, FALSE);

  /* Assert that the storage for this texture has been allocated */
  if (!cogl_texture_allocate (texture, error))
    return FALSE;

  /* Note that we don't prepare the bitmap for upload here because
     some backends may be internally using a different format for the
     actual GL texture than that reported by
     cogl_texture_get_format. For example the atlas textures are
     always stored in an RGBA texture even if the texture format is
     advertised as RGB. */

  return COGL_TEXTURE_GET_CLASS (texture)->set_region (texture,
                                                       src_x, src_y,
                                                       dst_x, dst_y,
                                                       width, height,
                                                       level,
                                                       bmp,
                                                       error);
}

gboolean
cogl_texture_set_region_from_bitmap (CoglTexture *texture,
                                     int src_x,
                                     int src_y,
                                     int dst_x,
                                     int dst_y,
                                     unsigned int dst_width,
                                     unsigned int dst_height,
                                     CoglBitmap *bitmap)
{
  GError *ignore_error = NULL;
  gboolean status;

  g_return_val_if_fail (COGL_IS_TEXTURE (texture), FALSE);

  status = _cogl_texture_set_region_from_bitmap (texture,
                                                 src_x, src_y,
                                                 dst_width, dst_height,
                                                 bitmap,
                                                 dst_x, dst_y,
                                                 0, /* level */
                                                 &ignore_error);

  g_clear_error (&ignore_error);
  return status;
}

gboolean
_cogl_texture_set_region (CoglTexture *texture,
                          int width,
                          int height,
                          CoglPixelFormat format,
                          int rowstride,
                          const uint8_t *data,
                          int dst_x,
                          int dst_y,
                          int level,
                          GError **error)
{
  CoglContext *ctx = cogl_texture_get_context (texture);
  CoglBitmap *source_bmp;
  gboolean ret;

  g_return_val_if_fail (format != COGL_PIXEL_FORMAT_ANY, FALSE);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, FALSE);

  /* Rowstride from width if none specified */
  if (rowstride == 0)
    rowstride = cogl_pixel_format_get_bytes_per_pixel (format, 0) * width;

  /* Init source bitmap */
  source_bmp = cogl_bitmap_new_for_data (ctx,
                                         width, height,
                                         format,
                                         rowstride,
                                         (uint8_t *) data);

  ret = _cogl_texture_set_region_from_bitmap (texture,
                                              0, 0,
                                              width, height,
                                              source_bmp,
                                              dst_x, dst_y,
                                              level,
                                              error);

  g_object_unref (source_bmp);

  return ret;
}

gboolean
cogl_texture_set_region (CoglTexture *texture,
			 int src_x,
			 int src_y,
			 int dst_x,
			 int dst_y,
			 unsigned int dst_width,
			 unsigned int dst_height,
			 int width,
			 int height,
			 CoglPixelFormat format,
			 unsigned int rowstride,
			 const uint8_t *data)
{
  GError *ignore_error = NULL;
  const uint8_t *first_pixel;
  int bytes_per_pixel;
  gboolean status;

  g_return_val_if_fail (COGL_IS_TEXTURE (texture), FALSE);
  g_return_val_if_fail (format != COGL_PIXEL_FORMAT_ANY, FALSE);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, FALSE);

  /* Rowstride from width if none specified */
  bytes_per_pixel = cogl_pixel_format_get_bytes_per_pixel (format, 0);
  if (rowstride == 0)
    rowstride = bytes_per_pixel * width;

  first_pixel = data + rowstride * src_y + bytes_per_pixel * src_x;

  status = _cogl_texture_set_region (texture,
                                     dst_width,
                                     dst_height,
                                     format,
                                     rowstride,
                                     first_pixel,
                                     dst_x,
                                     dst_y,
                                     0,
                                     &ignore_error);
  g_clear_error (&ignore_error);
  return status;
}

gboolean
cogl_texture_set_data (CoglTexture *texture,
                       CoglPixelFormat format,
                       int rowstride,
                       const uint8_t *data,
                       int level,
                       GError **error)
{
  int level_width;
  int level_height;

  g_return_val_if_fail (COGL_IS_TEXTURE (texture), FALSE);

  _cogl_texture_get_level_size (texture,
                                level,
                                &level_width,
                                &level_height,
                                NULL);

  return _cogl_texture_set_region (texture,
                                   level_width,
                                   level_height,
                                   format,
                                   rowstride,
                                   data,
                                   0, 0, /* dest x, y */
                                   level,
                                   error);
}

static gboolean
get_texture_bits_via_offscreen (CoglTexture *meta_texture,
                                CoglTexture *sub_texture,
                                int x,
                                int y,
                                int width,
                                int height,
                                uint8_t *dst_bits,
                                unsigned int dst_rowstride,
                                CoglPixelFormat closest_format)
{
  CoglContext *ctx = cogl_texture_get_context (sub_texture);
  CoglOffscreen *offscreen;
  CoglFramebuffer *framebuffer;
  CoglBitmap *bitmap;
  gboolean ret;
  GError *ignore_error = NULL;
  CoglPixelFormat real_format;

  offscreen = _cogl_offscreen_new_with_texture_full
                                      (sub_texture,
                                       COGL_OFFSCREEN_DISABLE_DEPTH_AND_STENCIL,
                                       0);

  framebuffer = COGL_FRAMEBUFFER (offscreen);
  if (!cogl_framebuffer_allocate (framebuffer, &ignore_error))
    {
      g_error_free (ignore_error);
      return FALSE;
    }

  /* Currently the framebuffer's internal format corresponds to the
   * internal format of @sub_texture but in the case of atlas textures
   * it's possible that this format doesn't reflect the correct
   * premultiplied alpha status or what components are valid since
   * atlas textures are always stored in a shared texture with a
   * format of _RGBA_8888.
   *
   * Here we override the internal format to make sure the
   * framebuffer's internal format matches the internal format of the
   * parent meta_texture instead.
   */
  real_format = cogl_texture_get_format (meta_texture);
  _cogl_framebuffer_set_internal_format (framebuffer, real_format);

  bitmap = cogl_bitmap_new_for_data (ctx,
                                     width, height,
                                     closest_format,
                                     dst_rowstride,
                                     dst_bits);
  ret = _cogl_framebuffer_read_pixels_into_bitmap (framebuffer,
                                                   x, y,
                                                   COGL_READ_PIXELS_COLOR_BUFFER,
                                                   bitmap,
                                                   &ignore_error);

  g_clear_error (&ignore_error);

  g_object_unref (bitmap);

  g_object_unref (framebuffer);

  return ret;
}

static gboolean
get_texture_bits_via_copy (CoglTexture *texture,
                           int x,
                           int y,
                           int width,
                           int height,
                           uint8_t *dst_bits,
                           unsigned int dst_rowstride,
                           CoglPixelFormat dst_format)
{
  unsigned int full_rowstride;
  uint8_t *full_bits;
  gboolean ret = TRUE;
  int bpp;
  int full_tex_width, full_tex_height;

  g_return_val_if_fail (dst_format != COGL_PIXEL_FORMAT_ANY, FALSE);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (dst_format) == 1, FALSE);

  full_tex_width = cogl_texture_get_width (texture);
  full_tex_height = cogl_texture_get_height (texture);

  bpp = cogl_pixel_format_get_bytes_per_pixel (dst_format, 0);

  full_rowstride = bpp * full_tex_width;
  full_bits = g_malloc (full_rowstride * full_tex_height);

  if (COGL_TEXTURE_GET_CLASS (texture)->get_data (texture,
                                                  dst_format,
                                                  full_rowstride,
                                                  full_bits))
    {
      uint8_t *dst = dst_bits;
      uint8_t *src = full_bits + x * bpp + y * full_rowstride;
      int i;

      for (i = 0; i < height; i++)
        {
          memcpy (dst, src, bpp * width);
          dst += dst_rowstride;
          src += full_rowstride;
        }
    }
  else
    ret = FALSE;

  g_free (full_bits);

  return ret;
}

typedef struct
{
  CoglTexture *meta_texture;
  int orig_width;
  int orig_height;
  CoglBitmap *target_bmp;
  uint8_t *target_bits;
  gboolean success;
  GError *error;
} CoglTextureGetData;

static void
texture_get_cb (CoglTexture *subtexture,
                const float *subtexture_coords,
                const float *virtual_coords,
                void        *user_data)
{
  CoglTextureGetData *tg_data = user_data;
  CoglTexture *meta_texture = tg_data->meta_texture;
  CoglPixelFormat closest_format = cogl_bitmap_get_format (tg_data->target_bmp);
  /* We already asserted that we have a single plane format */
  int bpp = cogl_pixel_format_get_bytes_per_pixel (closest_format, 0);
  unsigned int rowstride = cogl_bitmap_get_rowstride (tg_data->target_bmp);
  int subtexture_width = cogl_texture_get_width (subtexture);
  int subtexture_height = cogl_texture_get_height (subtexture);

  int x_in_subtexture = (int) (0.5 + subtexture_width * subtexture_coords[0]);
  int y_in_subtexture = (int) (0.5 + subtexture_height * subtexture_coords[1]);
  int width = ((int) (0.5 + subtexture_width * subtexture_coords[2])
               - x_in_subtexture);
  int height = ((int) (0.5 + subtexture_height * subtexture_coords[3])
                - y_in_subtexture);
  int x_in_bitmap = (int) (0.5 + tg_data->orig_width * virtual_coords[0]);
  int y_in_bitmap = (int) (0.5 + tg_data->orig_height * virtual_coords[1]);

  uint8_t *dst_bits;

  if (!tg_data->success)
    return;

  dst_bits = tg_data->target_bits + x_in_bitmap * bpp + y_in_bitmap * rowstride;

  /* If we can read everything as a single slice, then go ahead and do that
   * to avoid allocating an FBO. We'll leave it up to the GL implementation to
   * do glGetTexImage as efficiently as possible. (GLES doesn't have that,
   * so we'll fall through)
   */
  if (x_in_subtexture == 0 && y_in_subtexture == 0 &&
      width == subtexture_width && height == subtexture_height)
    {
      if (COGL_TEXTURE_GET_CLASS (subtexture)->get_data (subtexture,
                                                         closest_format,
                                                         rowstride,
                                                         dst_bits))
        return;
    }

  /* Next best option is a FBO and glReadPixels */
  if (get_texture_bits_via_offscreen (meta_texture,
                                      subtexture,
                                      x_in_subtexture, y_in_subtexture,
                                      width, height,
                                      dst_bits,
                                      rowstride,
                                      closest_format))
    return;

  /* Getting ugly: read the entire texture, copy out the part we want */
  if (get_texture_bits_via_copy (subtexture,
                                 x_in_subtexture, y_in_subtexture,
                                 width, height,
                                 dst_bits,
                                 rowstride,
                                 closest_format))
    return;

  /* No luck, the caller will fall back to the draw-to-backbuffer and
   * read implementation */
  tg_data->success = FALSE;
}

int
cogl_texture_get_data (CoglTexture *texture,
		       CoglPixelFormat format,
		       unsigned int rowstride,
		       uint8_t *data)
{
  CoglContext *ctx;
  CoglDriver *driver;
  CoglTextureDriver *tex_driver;
  CoglTextureDriverGLClass *tex_driver_gl_klass;
  int bpp;
  int byte_size;
  CoglPixelFormat closest_format;
  GLenum closest_gl_format;
  GLenum closest_gl_type;
  CoglBitmap *target_bmp;
  int tex_width;
  int tex_height;
  CoglPixelFormat texture_format;
  GError *ignore_error = NULL;
  CoglTextureGetData tg_data;

  g_return_val_if_fail (COGL_IS_TEXTURE (texture), 0);

  texture_format = cogl_texture_get_format (texture);

  /* Default to internal format if none specified */
  if (format == COGL_PIXEL_FORMAT_ANY)
    format = texture_format;

  /* We only support single plane formats */
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, 0);

  tex_width = cogl_texture_get_width (texture);
  tex_height = cogl_texture_get_height (texture);

  /* Rowstride from texture width if none specified */
  bpp = cogl_pixel_format_get_bytes_per_pixel (format, 0);
  if (rowstride == 0)
    rowstride = tex_width * bpp;

  /* Return byte size if only that requested */
  byte_size = tex_height * rowstride;
  if (data == NULL)
    return byte_size;

  ctx = cogl_texture_get_context (texture);
  driver = cogl_context_get_driver (ctx);
  tex_driver = cogl_texture_get_driver (texture);
  tex_driver_gl_klass = COGL_TEXTURE_DRIVER_GL_GET_CLASS (tex_driver);
  closest_format =
    tex_driver_gl_klass->find_best_gl_get_data_format (COGL_TEXTURE_DRIVER_GL (tex_driver),
                                                       ctx,
                                                       format,
                                                       &closest_gl_format,
                                                       &closest_gl_type);

  /* We can assume that whatever data GL gives us will have the
     premult status of the original texture */
  if (_cogl_pixel_format_can_have_premult (closest_format))
    closest_format = ((closest_format & ~COGL_PREMULT_BIT) |
                      (texture_format & COGL_PREMULT_BIT));

  /* If the application is requesting a conversion from a
   * component-alpha texture and the driver doesn't support them
   * natively then we can only read into an alpha-format buffer. In
   * this case the driver will be faking the alpha textures with a
   * red-component texture and it won't swizzle to the correct format
   * while reading */
  if (!cogl_driver_has_feature (driver, COGL_FEATURE_ID_ALPHA_TEXTURES))
    {
      if (texture_format == COGL_PIXEL_FORMAT_A_8)
        {
          closest_format = COGL_PIXEL_FORMAT_A_8;
        }
      else if (format == COGL_PIXEL_FORMAT_A_8)
        {
          /* If we are converting to a component-alpha texture then we
           * need to read all of the components to a temporary buffer
           * because there is no way to get just the 4th component.
           * Note: it doesn't matter whether the texture is
           * pre-multiplied here because we're only going to look at
           * the alpha component */
          closest_format = COGL_PIXEL_FORMAT_RGBA_8888;
        }
    }

  /* Is the requested format supported? */
  if (closest_format == format)
    /* Target user data directly */
    target_bmp = cogl_bitmap_new_for_data (ctx,
                                           tex_width,
                                           tex_height,
                                           format,
                                           rowstride,
                                           data);
  else
    {
      target_bmp = _cogl_bitmap_new_with_malloc_buffer (ctx,
                                                        tex_width, tex_height,
                                                        closest_format,
                                                        &ignore_error);
      if (!target_bmp)
        {
          g_error_free (ignore_error);
          return 0;
        }
    }

  tg_data.target_bits = _cogl_bitmap_map (target_bmp, COGL_BUFFER_ACCESS_WRITE,
                                          COGL_BUFFER_MAP_HINT_DISCARD,
                                          &ignore_error);
  if (tg_data.target_bits)
    {
      tg_data.meta_texture = texture;
      tg_data.orig_width = tex_width;
      tg_data.orig_height = tex_height;
      tg_data.target_bmp = target_bmp;
      tg_data.error = NULL;
      tg_data.success = TRUE;

      /* If there are any dependent framebuffers on the texture then we
         need to flush their journals so the texture contents will be
         up-to-date */
      _cogl_texture_flush_journal_rendering (texture);

      /* Iterating through the subtextures allows piecing together
       * the data for a sliced texture, and allows us to do the
       * read-from-framebuffer logic here in a simple fashion rather than
       * passing offsets down through the code. */
      cogl_texture_foreach_in_region (texture,
                                      0, 0, 1, 1,
                                      COGL_PIPELINE_WRAP_MODE_REPEAT,
                                      COGL_PIPELINE_WRAP_MODE_REPEAT,
                                      texture_get_cb,
                                      &tg_data);

      _cogl_bitmap_unmap (target_bmp);
    }
  else
    {
      g_error_free (ignore_error);
      tg_data.success = FALSE;
    }

  /* XXX: In some cases this api may fail to read back the texture
   * data; such as for GLES which doesn't support glGetTexImage
   */
  if (!tg_data.success)
    {
      g_object_unref (target_bmp);
      return 0;
    }

  /* Was intermediate used? */
  if (closest_format != format)
    {
      CoglBitmap *new_bmp;
      gboolean result;
      GError *error = NULL;

      /* Convert to requested format directly into the user's buffer */
      new_bmp = cogl_bitmap_new_for_data (ctx,
                                          tex_width, tex_height,
                                          format,
                                          rowstride,
                                          data);
      result = _cogl_bitmap_convert_into_bitmap (target_bmp, new_bmp, &error);

      if (!result)
        {
          g_error_free (error);
          /* Return failure after cleaning up */
          byte_size = 0;
        }

      g_object_unref (new_bmp);
    }

  g_object_unref (target_bmp);

  return byte_size;
}

static void
on_framebuffer_destroy (CoglFramebuffer *framebuffer,
                        CoglTexture     *texture)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);
  priv->framebuffers = g_list_remove (priv->framebuffers, framebuffer);
}

void
_cogl_texture_associate_framebuffer (CoglTexture *texture,
                                     CoglFramebuffer *framebuffer)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);
  /* Note: we don't take a reference on the framebuffer here because
   * that would introduce a circular reference. */
  priv->framebuffers = g_list_prepend (priv->framebuffers, framebuffer);

  g_signal_connect (framebuffer, "destroy",
                    G_CALLBACK (on_framebuffer_destroy),
                    texture);
}

const GList *
_cogl_texture_get_associated_framebuffers (CoglTexture *texture)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);
  return priv->framebuffers;
}

void
_cogl_texture_flush_journal_rendering (CoglTexture *texture)
{
  const GList *l;

  /* It could be that a referenced texture is part of a framebuffer
   * which has an associated journal that must be flushed before it
   * can be sampled from by the current primitive... */
  for (l = _cogl_texture_get_associated_framebuffers (texture); l; l = l->next)
    _cogl_framebuffer_flush_journal (l->data);
}

/* This function lets you define a meta texture as a grid of textures
 * whereby the x and y grid-lines are defined by an array of
 * CoglSpans. With that grid based description this function can then
 * iterate all the cells of the grid that lye within a region
 * specified as virtual, meta-texture, coordinates.  This function can
 * also cope with regions that extend beyond the original meta-texture
 * grid by iterating cells repeatedly according to the wrap_x/y
 * arguments.
 *
 * To differentiate between texture coordinates of a specific, real,
 * slice texture and the texture coordinates of a composite, meta
 * texture, the coordinates of the meta texture are called "virtual"
 * coordinates and the coordinates of spans are called "slice"
 * coordinates.
 *
 * Note: no guarantee is given about the order in which the slices
 * will be visited.
 *
 * Note: The slice coordinates passed to @callback are always
 * normalized coordinates even if the span coordinates aren't
 * normalized.
 */
void
_cogl_texture_spans_foreach_in_region (CoglSpan                    *x_spans,
                                       int                          n_x_spans,
                                       CoglSpan                    *y_spans,
                                       int                          n_y_spans,
                                       CoglTexture                **textures,
                                       float                       *virtual_coords,
                                       float                        x_normalize_factor,
                                       float                        y_normalize_factor,
                                       CoglPipelineWrapMode         wrap_x,
                                       CoglPipelineWrapMode         wrap_y,
                                       CoglTextureForeachCallback   callback,
                                       void                        *user_data)
{
  CoglSpanIter iter_x;
  CoglSpanIter iter_y;
  float slice_coords[4];
  float span_virtual_coords[4];

  /* Iterate the y axis of the virtual rectangle */
  for (_cogl_span_iter_begin (&iter_y,
                              y_spans,
                              n_y_spans,
                              y_normalize_factor,
                              virtual_coords[1],
                              virtual_coords[3],
                              wrap_y);
       !_cogl_span_iter_end (&iter_y);
       _cogl_span_iter_next (&iter_y))
    {
      if (iter_y.flipped)
        {
          slice_coords[1] = iter_y.intersect_end;
          slice_coords[3] = iter_y.intersect_start;
          span_virtual_coords[1] = iter_y.intersect_end;
          span_virtual_coords[3] = iter_y.intersect_start;
        }
      else
        {
          slice_coords[1] = iter_y.intersect_start;
          slice_coords[3] = iter_y.intersect_end;
          span_virtual_coords[1] = iter_y.intersect_start;
          span_virtual_coords[3] = iter_y.intersect_end;
        }

      /* Map the current intersection to normalized slice coordinates */
      slice_coords[1] = (slice_coords[1] - iter_y.pos) / iter_y.span->size;
      slice_coords[3] = (slice_coords[3] - iter_y.pos) / iter_y.span->size;

      /* Iterate the x axis of the virtual rectangle */
      for (_cogl_span_iter_begin (&iter_x,
                                  x_spans,
                                  n_x_spans,
                                  x_normalize_factor,
                                  virtual_coords[0],
                                  virtual_coords[2],
                                  wrap_x);
	   !_cogl_span_iter_end (&iter_x);
	   _cogl_span_iter_next (&iter_x))
        {
          CoglTexture *span_tex;

          if (iter_x.flipped)
            {
              slice_coords[0] = iter_x.intersect_end;
              slice_coords[2] = iter_x.intersect_start;
              span_virtual_coords[0] = iter_x.intersect_end;
              span_virtual_coords[2] = iter_x.intersect_start;
            }
          else
            {
              slice_coords[0] = iter_x.intersect_start;
              slice_coords[2] = iter_x.intersect_end;
              span_virtual_coords[0] = iter_x.intersect_start;
              span_virtual_coords[2] = iter_x.intersect_end;
            }

          /* Map the current intersection to normalized slice coordinates */
          slice_coords[0] = (slice_coords[0] - iter_x.pos) / iter_x.span->size;
          slice_coords[2] = (slice_coords[2] - iter_x.pos) / iter_x.span->size;

	  /* Pluck out the cogl texture for this span */
          span_tex = textures[iter_y.index * n_x_spans + iter_x.index];

          callback (COGL_TEXTURE (span_tex),
                    slice_coords,
                    span_virtual_coords,
                    user_data);
	}
    }
}

void
_cogl_texture_set_allocated (CoglTexture *texture,
                             CoglPixelFormat internal_format,
                             int width,
                             int height)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);
  _cogl_texture_set_internal_format (texture, internal_format);

  priv->width = width;
  priv->height = height;
  priv->allocated = TRUE;

  g_clear_pointer (&priv->loader, cogl_texture_loader_free);
}

gboolean
cogl_texture_is_allocated (CoglTexture *texture)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);
  return priv->allocated;
}

gboolean
cogl_texture_allocate (CoglTexture *texture,
                       GError **error)
{
  CoglDriver *driver;
  CoglTexturePrivate *priv;

  g_return_val_if_fail (COGL_IS_TEXTURE (texture), FALSE);


  priv = cogl_texture_get_instance_private (texture);
  if (cogl_texture_is_allocated (texture))
    return TRUE;

  driver = cogl_context_get_driver (priv->context);

  if (priv->components == COGL_TEXTURE_COMPONENTS_RG &&
      !cogl_driver_has_feature (driver, COGL_FEATURE_ID_TEXTURE_RG))
    g_set_error (error,
                 COGL_TEXTURE_ERROR,
                 COGL_TEXTURE_ERROR_FORMAT,
                 "A red-green texture was requested but the driver "
                 "does not support them");

  priv->allocated = COGL_TEXTURE_GET_CLASS (texture)->allocate (texture, error);

  return priv->allocated;
}

void
_cogl_texture_set_internal_format (CoglTexture *texture,
                                   CoglPixelFormat internal_format)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);
  priv->premultiplied = FALSE;

  if (internal_format == COGL_PIXEL_FORMAT_ANY)
    internal_format = COGL_PIXEL_FORMAT_RGBA_8888_PRE;

  if (internal_format == COGL_PIXEL_FORMAT_A_8)
    {
      priv->components = COGL_TEXTURE_COMPONENTS_A;
      return;
    }
  else if (internal_format == COGL_PIXEL_FORMAT_RG_88)
    {
      priv->components = COGL_TEXTURE_COMPONENTS_RG;
      return;
    }
  else if (internal_format & COGL_DEPTH_BIT)
    {
      priv->components = COGL_TEXTURE_COMPONENTS_DEPTH;
      return;
    }
  else if (internal_format & COGL_A_BIT)
    {
      priv->components = COGL_TEXTURE_COMPONENTS_RGBA;
      if (internal_format & COGL_PREMULT_BIT)
        priv->premultiplied = TRUE;
      return;
    }
  else
    priv->components = COGL_TEXTURE_COMPONENTS_RGB;
}

CoglPixelFormat
_cogl_texture_determine_internal_format (CoglTexture *texture,
                                         CoglPixelFormat src_format)
{
  switch (cogl_texture_get_components (texture))
    {
    case COGL_TEXTURE_COMPONENTS_DEPTH:
      if (src_format & COGL_DEPTH_BIT)
        return src_format;
      else
        {
          CoglContext *ctx = cogl_texture_get_context (texture);
          CoglDriver *driver = cogl_context_get_driver (ctx);

          if (cogl_driver_has_feature (driver,
                  COGL_FEATURE_ID_EXT_PACKED_DEPTH_STENCIL) ||
              cogl_driver_has_feature (driver,
                  COGL_FEATURE_ID_OES_PACKED_DEPTH_STENCIL))
            {
              return COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8;
            }
          else
            return COGL_PIXEL_FORMAT_DEPTH_16;
        }
    case COGL_TEXTURE_COMPONENTS_A:
      return COGL_PIXEL_FORMAT_A_8;
    case COGL_TEXTURE_COMPONENTS_RG:
      return COGL_PIXEL_FORMAT_RG_88;
    case COGL_TEXTURE_COMPONENTS_RGB:
      if (src_format != COGL_PIXEL_FORMAT_ANY &&
          !(src_format & COGL_A_BIT) && !(src_format & COGL_DEPTH_BIT))
        return src_format;
      else
        return COGL_PIXEL_FORMAT_RGB_888;
    case COGL_TEXTURE_COMPONENTS_RGBA:
      {
        CoglPixelFormat format;

        if (src_format != COGL_PIXEL_FORMAT_ANY &&
            (src_format & COGL_A_BIT) && src_format != COGL_PIXEL_FORMAT_A_8)
          format = src_format;
        else
          format = COGL_PIXEL_FORMAT_RGBA_8888;

        if (cogl_texture_get_premultiplied (texture))
          {
            if (_cogl_pixel_format_can_have_premult (format))
              return format |= COGL_PREMULT_BIT;
            else
              return COGL_PIXEL_FORMAT_RGBA_8888_PRE;
          }
        else
          return format & ~COGL_PREMULT_BIT;
      }
    }

  g_return_val_if_reached (COGL_PIXEL_FORMAT_RGBA_8888_PRE);
}

void
cogl_texture_set_components (CoglTexture *texture,
                             CoglTextureComponents components)
{
  CoglTexturePrivate *priv;

  g_return_if_fail (COGL_IS_TEXTURE (texture));
  g_return_if_fail (!cogl_texture_is_allocated (texture));

  priv = cogl_texture_get_instance_private (texture);
  if (priv->components == components)
    return;

  priv->components = components;
}

CoglTextureComponents
cogl_texture_get_components (CoglTexture *texture)
{
  CoglTexturePrivate *priv;

  g_return_val_if_fail (COGL_IS_TEXTURE (texture), 0);

  priv = cogl_texture_get_instance_private (texture);
  return priv->components;
}

void
cogl_texture_set_premultiplied (CoglTexture *texture,
                                gboolean premultiplied)
{
  CoglTexturePrivate *priv;

  g_return_if_fail (COGL_IS_TEXTURE (texture));
  g_return_if_fail (!cogl_texture_is_allocated (texture));

  premultiplied = !!premultiplied;

  priv = cogl_texture_get_instance_private (texture);
  if (priv->premultiplied == premultiplied)
    return;

  priv->premultiplied = premultiplied;
}

gboolean
cogl_texture_get_premultiplied (CoglTexture *texture)
{
  CoglTexturePrivate *priv;

  g_return_val_if_fail (COGL_IS_TEXTURE (texture), FALSE);

  priv = cogl_texture_get_instance_private (texture);
  return priv->premultiplied;
}

void
_cogl_texture_copy_internal_format (CoglTexture *src,
                                    CoglTexture *dest)
{
  cogl_texture_set_components (dest, cogl_texture_get_components (src));
  cogl_texture_set_premultiplied (dest, cogl_texture_get_premultiplied (src));
}

CoglContext *
cogl_texture_get_context (CoglTexture *texture)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);
  return priv->context;
}

CoglTextureLoader *
cogl_texture_get_loader (CoglTexture *texture)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);
  return priv->loader;
}

int
cogl_texture_get_max_level_set (CoglTexture *texture)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);
  return priv->max_level_set;
}

void
cogl_texture_set_max_level_set (CoglTexture *texture,
                                int          max_level_set)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);
  priv->max_level_set = max_level_set;
}


CoglTextureDriver *
cogl_texture_get_driver (CoglTexture *texture)
{
  CoglTexturePrivate *priv =
    cogl_texture_get_instance_private (texture);

  return priv->tex_driver;
}
