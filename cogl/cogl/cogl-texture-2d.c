/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009 Intel Corporation.
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
 *  Neil Roberts   <neil@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-private.h"
#include "cogl/cogl-util.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-texture-2d-private.h"
#include "cogl/cogl-texture-driver.h"
#include "cogl/cogl-context-private.h"

#include <string.h>
#include <math.h>

G_DEFINE_TYPE (CoglTexture2D, cogl_texture_2d, COGL_TYPE_TEXTURE)



void
cogl_texture_2d_set_auto_mipmap (CoglTexture2D *tex,
                                 gboolean       value)
{
  tex->auto_mipmap = value;
}

CoglTexture *
_cogl_texture_2d_create_base (CoglContext *ctx,
                              int width,
                              int height,
                              CoglPixelFormat internal_format,
                              CoglTextureLoader *loader)
{
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglTextureDriver *tex_driver = cogl_driver_create_texture_driver (driver);
  CoglTextureDriverClass *tex_driver_klass =
    COGL_TEXTURE_DRIVER_GET_CLASS (tex_driver);
  GType tex_type = tex_driver_klass->texture_2d_get_type (tex_driver);

  CoglTexture2D *tex_2d = g_object_new (tex_type,
                                        "context", ctx,
                                        "texture-driver", tex_driver,
                                        "width", width,
                                        "height", height,
                                        "loader", loader,
                                        "format", internal_format,
                                        NULL);

  return COGL_TEXTURE (tex_2d);
}

static gboolean
_cogl_texture_2d_allocate (CoglTexture *tex,
                           GError **error)
{
  CoglTextureDriver *tex_driver = cogl_texture_get_driver (tex);
  CoglTextureDriverClass *tex_driver_klass =
    COGL_TEXTURE_DRIVER_GET_CLASS (tex_driver);

  return tex_driver_klass->texture_2d_allocate (tex_driver, tex, error);
}

void
_cogl_texture_2d_copy_from_framebuffer (CoglTexture2D *tex_2d,
                                        int src_x,
                                        int src_y,
                                        int width,
                                        int height,
                                        CoglFramebuffer *src_fb,
                                        int dst_x,
                                        int dst_y,
                                        int level)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglTextureDriver *tex_driver = cogl_texture_get_driver (tex);
  CoglTextureDriverClass *tex_driver_klass =
    COGL_TEXTURE_DRIVER_GET_CLASS (tex_driver);

  /* Assert that the storage for this texture has been allocated */
  cogl_texture_allocate (tex, NULL); /* (abort on error) */

  tex_driver_klass->texture_2d_copy_from_framebuffer (tex_driver,
                                                      tex_2d,
                                                      src_x,
                                                      src_y,
                                                      width,
                                                      height,
                                                      src_fb,
                                                      dst_x,
                                                      dst_y,
                                                      level);

  tex_2d->mipmaps_dirty = TRUE;
}

static gboolean
_cogl_texture_2d_can_hardware_repeat (CoglTexture *tex)
{
  return TRUE;
}

static void
_cogl_texture_2d_transform_coords (CoglTexture *tex,
                                   float       *s,
                                   float       *t)
{
  /* The texture coordinates map directly so we don't need to do
     anything */
}

static CoglTransformResult
_cogl_texture_2d_transform_quad_coords (CoglTexture *tex,
                                        float       *coords)
{
  /* The texture coordinates map directly so we don't need to do
     anything other than check for repeats */

  int i;

  for (i = 0; i < 4; i++)
    if (coords[i] < 0.0f || coords[i] > 1.0f)
      {
        /* Repeat is needed */
        return (_cogl_texture_2d_can_hardware_repeat (tex) ?
                COGL_TRANSFORM_HARDWARE_REPEAT :
                COGL_TRANSFORM_SOFTWARE_REPEAT);
      }

  /* No repeat is needed */
  return COGL_TRANSFORM_NO_REPEAT;
}

static gboolean
_cogl_texture_2d_set_region (CoglTexture *tex,
                             int src_x,
                             int src_y,
                             int dst_x,
                             int dst_y,
                             int width,
                             int height,
                             int level,
                             CoglBitmap *bmp,
                             GError **error)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
  CoglTextureDriver *tex_driver = cogl_texture_get_driver (tex);
  CoglTextureDriverClass *tex_driver_klass =
    COGL_TEXTURE_DRIVER_GET_CLASS (tex_driver);

  if (!tex_driver_klass->texture_2d_copy_from_bitmap (tex_driver,
                                                      tex_2d,
                                                      src_x,
                                                      src_y,
                                                      width,
                                                      height,
                                                      bmp,
                                                      dst_x,
                                                      dst_y,
                                                      level,
                                                      error))
    {
      return FALSE;
    }

  tex_2d->mipmaps_dirty = TRUE;

  return TRUE;
}

static gboolean
_cogl_texture_2d_get_data (CoglTexture *tex,
                           CoglPixelFormat format,
                           int rowstride,
                           uint8_t *data)
{
  CoglTextureDriver *tex_driver = cogl_texture_get_driver (tex);
  CoglTextureDriverClass *tex_driver_klass =
    COGL_TEXTURE_DRIVER_GET_CLASS (tex_driver);

  if (tex_driver_klass->texture_2d_get_data)
    {
      CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
      tex_driver_klass->texture_2d_get_data (tex_driver, tex_2d, format, rowstride, data);
      return TRUE;
    }
  else
    return FALSE;
}

static CoglPixelFormat
_cogl_texture_2d_get_format (CoglTexture *tex)
{
  return COGL_TEXTURE_2D (tex)->internal_format;
}

static void
cogl_texture_2d_foreach_leaf (CoglTexture              *tex,
                              CoglLeafTextureCallback   callback,
                              void                     *user_data)
{
  callback (COGL_TEXTURE_2D (tex), user_data);
}

static void
cogl_texture_2d_class_init (CoglTexture2DClass *klass)
{
  CoglTextureClass *texture_class = COGL_TEXTURE_CLASS (klass);

  texture_class->foreach_leaf_texture = cogl_texture_2d_foreach_leaf;
  texture_class->allocate = _cogl_texture_2d_allocate;
  texture_class->set_region = _cogl_texture_2d_set_region;
  texture_class->get_data = _cogl_texture_2d_get_data;
  texture_class->can_hardware_repeat = _cogl_texture_2d_can_hardware_repeat;
  texture_class->transform_coords = _cogl_texture_2d_transform_coords;
  texture_class->transform_quad_coords = _cogl_texture_2d_transform_quad_coords;
  texture_class->get_format = _cogl_texture_2d_get_format;
}

static void
cogl_texture_2d_init (CoglTexture2D *self)
{
  self->auto_mipmap = TRUE;
  self->mipmaps_dirty = TRUE;
  self->is_get_data_supported = TRUE;
}

CoglTexture *
cogl_texture_2d_new_with_format (CoglContext     *ctx,
                                 int              width,
                                 int              height,
                                 CoglPixelFormat  format)
{
  CoglTextureLoader *loader;

  g_return_val_if_fail (width >= 1, NULL);
  g_return_val_if_fail (height >= 1, NULL);

  loader = cogl_texture_loader_new (COGL_TEXTURE_SOURCE_TYPE_SIZE);
  loader->src.sized.width = width;
  loader->src.sized.height = height;
  loader->src.sized.format = format;

  return _cogl_texture_2d_create_base (ctx, width, height, format, loader);
}

CoglTexture *
cogl_texture_2d_new_with_size (CoglContext *ctx,
                               int width,
                               int height)
{
  CoglTextureLoader *loader;

  g_return_val_if_fail (width >= 1, NULL);
  g_return_val_if_fail (height >= 1, NULL);

  loader = cogl_texture_loader_new (COGL_TEXTURE_SOURCE_TYPE_SIZE);
  loader->src.sized.width = width;
  loader->src.sized.height = height;
  loader->src.sized.format = COGL_PIXEL_FORMAT_ANY;

  return _cogl_texture_2d_create_base (ctx, width, height,
                                       COGL_PIXEL_FORMAT_RGBA_8888_PRE, loader);
}

CoglTexture *
cogl_texture_2d_new_from_bitmap (CoglBitmap *bmp)
{
  CoglTextureLoader *loader;

  g_return_val_if_fail (bmp != NULL, NULL);

  loader = cogl_texture_loader_new (COGL_TEXTURE_SOURCE_TYPE_BITMAP);
  loader->src.bitmap.bitmap = g_object_ref (bmp);

  return  _cogl_texture_2d_create_base (_cogl_bitmap_get_context (bmp),
                                        cogl_bitmap_get_width (bmp),
                                        cogl_bitmap_get_height (bmp),
                                        cogl_bitmap_get_format (bmp),
                                        loader);
}

CoglTexture *
cogl_texture_2d_new_from_data (CoglContext *ctx,
                               int width,
                               int height,
                               CoglPixelFormat format,
                               int rowstride,
                               const uint8_t *data,
                               GError **error)
{
  CoglBitmap *bmp;
  CoglTexture *tex_2d;

  g_return_val_if_fail (format != COGL_PIXEL_FORMAT_ANY, NULL);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, NULL);
  g_return_val_if_fail (data != NULL, NULL);

  /* Rowstride from width if not given */
  if (rowstride == 0)
    rowstride = width * cogl_pixel_format_get_bytes_per_pixel (format, 0);

  /* Wrap the data into a bitmap */
  bmp = cogl_bitmap_new_for_data (ctx,
                                  width, height,
                                  format,
                                  rowstride,
                                  (uint8_t *) data);

  tex_2d = cogl_texture_2d_new_from_bitmap (bmp);

  g_object_unref (bmp);

  if (tex_2d &&
      !cogl_texture_allocate (COGL_TEXTURE (tex_2d), error))
    {
      g_object_unref (tex_2d);
      return NULL;
    }

  return tex_2d;
}

void
_cogl_texture_2d_externally_modified (CoglTexture *texture)
{
  if (!COGL_IS_TEXTURE_2D (texture))
    return;

  COGL_TEXTURE_2D (texture)->mipmaps_dirty = TRUE;
}
