/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
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

#include <glib-object.h>

#include "cogl/cogl-pixel-format.h"
#include "cogl/cogl-types.h"

typedef struct _CoglDriver CoglDriver;

G_DECLARE_DERIVABLE_TYPE (CoglTextureDriver,
                          cogl_texture_driver,
                          COGL,
                          TEXTURE_DRIVER,
                          GObject)

#define COGL_TYPE_TEXTURE_DRIVER (cogl_texture_driver_get_type ())

struct _CoglTextureDriverClass
{
  GObjectClass parent_class;

  /* Destroys any driver specific resources associated with the given
  * 2D texture. */
  void (* texture_2d_free) (CoglTextureDriver *driver,
                            CoglTexture2D     *tex_2d);

  /* Returns TRUE if the driver can support creating a 2D texture with
  * the given geometry and specified internal format.
  */
  gboolean (* texture_2d_can_create) (CoglTextureDriver *driver,
                                      CoglContext       *ctx,
                                      int                width,
                                      int                height,
                                      CoglPixelFormat    internal_format);

  /* Allocates (uninitialized) storage for the given texture according
  * to the configured size and format of the texture */
  gboolean (* texture_2d_allocate) (CoglTextureDriver *driver,
                                    CoglTexture       *tex,
                                    GError           **error);

  /* Initialize the specified region of storage of the given texture
  * with the contents of the specified framebuffer region
  */
  void (* texture_2d_copy_from_framebuffer) (CoglTextureDriver *driver,
                                             CoglTexture2D     *tex_2d,
                                             int                src_x,
                                             int                src_y,
                                             int                width,
                                             int                height,
                                             CoglFramebuffer   *src_fb,
                                             int                dst_x,
                                             int                dst_y,
                                             int                level);

  /* Update all mipmap levels > 0 */
  void (* texture_2d_generate_mipmap) (CoglTextureDriver *driver,
                                       CoglTexture2D     *tex_2d);

  /* Initialize the specified region of storage of the given texture
  * with the contents of the specified bitmap region
  *
  * Since this may need to create the underlying storage first
  * it may throw a NO_MEMORY error.
  */
  gboolean (* texture_2d_copy_from_bitmap) (CoglTextureDriver *driver,
                                            CoglTexture2D     *tex_2d,
                                            int                src_x,
                                            int                src_y,
                                            int                width,
                                            int                height,
                                            CoglBitmap        *bitmap,
                                            int                dst_x,
                                            int                dst_y,
                                            int                level,
                                            GError           **error);

  gboolean (* texture_2d_is_get_data_supported) (CoglTextureDriver *driver,
                                                 CoglTexture2D     *tex_2d);

  /* Reads back the full contents of the given texture and write it to
  * @data in the given @format and with the given @rowstride.
  *
  * This is optional
  */
  void (* texture_2d_get_data) (CoglTextureDriver *driver,
                                CoglTexture2D     *tex_2d,
                                CoglPixelFormat    format,
                                int                rowstride,
                                uint8_t           *data);
};

CoglDriver * cogl_texture_driver_get_driver (CoglTextureDriver *tex_driver);
