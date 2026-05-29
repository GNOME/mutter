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
 */

#pragma once

#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-texture-2d.h"

struct _CoglTexture2D
{
  CoglTexture parent_instance;

  CoglPixelFormat internal_format;

  gboolean auto_mipmap;
  gboolean mipmaps_dirty;
  gboolean is_get_data_supported;
};

struct _CoglTexture2DClass
{
  CoglTextureClass parent_class;

  void (* copy_from_framebuffer) (CoglTexture2D   *tex_2d,
                                  int              src_x,
                                  int              src_y,
                                  int              width,
                                  int              height,
                                  CoglFramebuffer *src_fb,
                                  int              dst_x,
                                  int              dst_y,
                                  int              level);

  void (* generate_mipmap) (CoglTexture2D *tex_2d);

  gboolean (* copy_from_bitmap) (CoglTexture2D *tex_2d,
                                 int            src_x,
                                 int            src_y,
                                 int            width,
                                 int            height,
                                 CoglBitmap    *bitmap,
                                 int            dst_x,
                                 int            dst_y,
                                 int            level,
                                 GError       **error);
};

CoglTexture *
_cogl_texture_2d_create_base (CoglContext *ctx,
                              int width,
                              int height,
                              CoglPixelFormat internal_format,
                              CoglTextureLoader *loader);

/*
 * _cogl_texture_2d_externally_modified:
 * @texture: A #CoglTexture2D object
 *
 * This should be called whenever the texture is modified other than
 * by using cogl_texture_set_region. It will cause the mipmaps to be
 * invalidated
 */
void
_cogl_texture_2d_externally_modified (CoglTexture *texture);

/*
 * _cogl_texture_2d_copy_from_framebuffer:
 * @texture: A #CoglTexture2D pointer
 * @src_x: X-position to within the framebuffer to read from
 * @src_y: Y-position to within the framebuffer to read from
 * @width: width of the rectangle to copy
 * @height: height of the rectangle to copy
 * @src_fb: A source #CoglFramebuffer to copy from
 * @dst_x: X-position to store the image within the texture
 * @dst_y: Y-position to store the image within the texture
 * @level: The mipmap level of @texture to copy too
 *
 * This copies a portion of the given @src_fb into the
 * texture.
 */
void
_cogl_texture_2d_copy_from_framebuffer (CoglTexture2D *texture,
                                        int src_x,
                                        int src_y,
                                        int width,
                                        int height,
                                        CoglFramebuffer *src_fb,
                                        int dst_x,
                                        int dst_y,
                                        int level);
