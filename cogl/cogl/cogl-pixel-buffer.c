/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010 Intel Corporation.
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
 *   Damien Lespiau <damien.lespiau@intel.com>
 *   Robert Bragg <robert@linux.intel.com>
 */

/* For an overview of the functionality implemented here, please see
 * cogl-buffer-array.h, which contains the gtk-doc section overview for the
 * Pixel Buffers API.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "cogl/cogl-private.h"
#include "cogl/cogl-util.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-pixel-buffer-private.h"
#include "cogl/cogl-pixel-buffer.h"

G_DEFINE_FINAL_TYPE (CoglPixelBuffer, cogl_pixel_buffer, COGL_TYPE_BUFFER)

static void
cogl_pixel_buffer_class_init (CoglPixelBufferClass *klass)
{
}

static void
cogl_pixel_buffer_init (CoglPixelBuffer *buffer)
{
}

static CoglPixelBuffer *
_cogl_pixel_buffer_new (CoglContext *context,
                        size_t       size,
                        const void  *data,
                        GError     **error)
{
  CoglPixelBuffer *pixel_buffer;

  pixel_buffer = g_object_new (COGL_TYPE_PIXEL_BUFFER,
                               "context", context,
                               "size", (uint64_t) size,
                               "default-target", COGL_BUFFER_BIND_TARGET_PIXEL_UNPACK,
                               "update-hint", COGL_BUFFER_UPDATE_HINT_STATIC,
                               NULL);

  if (data)
    {
      if (!_cogl_buffer_set_data (COGL_BUFFER (pixel_buffer),
                                  0,
                                  data,
                                  size,
                                  error))
        {
          g_object_unref (pixel_buffer);
          return NULL;
        }
    }

  return pixel_buffer;
}

CoglPixelBuffer *
cogl_pixel_buffer_new (CoglContext *context,
                       size_t       size,
                       const void  *data)
{
  GError *ignore_error = NULL;
  CoglPixelBuffer *buffer =
    _cogl_pixel_buffer_new (context, size, data, &ignore_error);

  g_clear_error (&ignore_error);
  return buffer;
}
