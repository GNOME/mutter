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

  /* Returns the GType of the driver-specific CoglTexture2D subclass */
  GType (* texture_2d_get_type) (CoglTextureDriver *driver);

  /* Returns TRUE if the driver can support creating a 2D texture with
  * the given geometry and specified internal format.
  */
  gboolean (* texture_2d_can_create) (CoglTextureDriver *driver,
                                      CoglContext       *ctx,
                                      int                width,
                                      int                height,
                                      CoglPixelFormat    internal_format);

  /* The driver may impose constraints on what formats can be used to store
   * texture data read from textures.
   */
  CoglPixelFormat (* find_best_get_data_format) (CoglTextureDriver *driver,
                                                 CoglContext       *context,
                                                 CoglPixelFormat    format);
};

CoglDriver * cogl_texture_driver_get_driver (CoglTextureDriver *tex_driver);
