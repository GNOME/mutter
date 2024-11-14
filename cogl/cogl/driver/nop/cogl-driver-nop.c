/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Intel Corporation.
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

#include "config.h"

#include <string.h>

#include "cogl/cogl-private.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-feature-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/driver/nop/cogl-texture-2d-nop-private.h"

#define COGL_TYPE_NOP_FRAMEBUFFER (cogl_nop_framebuffer_get_type ())
G_DECLARE_FINAL_TYPE (CoglNopFramebuffer, cogl_nop_framebuffer,
                      COGL, NOP_FRAMEBUFFER_DRIVER,
                      CoglFramebufferDriver)


struct _CoglNopFramebuffer
{
  CoglFramebufferDriver parent;
};

G_DEFINE_FINAL_TYPE (CoglNopFramebuffer, cogl_nop_framebuffer,
                     COGL_TYPE_FRAMEBUFFER_DRIVER)


static void
cogl_nop_framebuffer_init (CoglNopFramebuffer *nop_framebuffer)
{
}

static void
cogl_nop_framebuffer_class_init (CoglNopFramebufferClass *klass)
{
}

static gboolean
_cogl_driver_update_features (CoglContext *ctx,
                              GError **error)
{
  memset (ctx->private_features, 0, sizeof (ctx->private_features));

  return TRUE;
}

static const char *
_cogl_driver_nop_get_renderer (CoglContext *context)
{
  return "NOP";
}

static CoglFramebufferDriver *
_cogl_driver_nop_create_framebuffer_driver (CoglContext                        *context,
                                            CoglFramebuffer                    *framebuffer,
                                            const CoglFramebufferDriverConfig  *driver_config,
                                            GError                            **error)
{
  return g_object_new (COGL_TYPE_NOP_FRAMEBUFFER,
                       "framebuffer", framebuffer,
                       NULL);
}

const CoglDriverVtable
_cogl_driver_nop =
  {
    NULL,
    NULL,
    _cogl_driver_nop_get_renderer,
    NULL,
    NULL, /* get_graphics_reset_status */
    NULL, /* pixel_format_to_gl */
    NULL, /* _cogl_driver_get_read_pixels_format */
    _cogl_driver_update_features,
    _cogl_driver_nop_create_framebuffer_driver,
    NULL,
    _cogl_texture_2d_nop_free,
    _cogl_texture_2d_nop_can_create,
    _cogl_texture_2d_nop_init,
    _cogl_texture_2d_nop_allocate,
    _cogl_texture_2d_nop_copy_from_framebuffer,
    _cogl_texture_2d_nop_get_gl_handle,
    _cogl_texture_2d_nop_generate_mipmap,
    _cogl_texture_2d_nop_copy_from_bitmap,
    NULL, /* texture_2d_is_get_data_supported */
    NULL, /* texture_2d_get_data */
    NULL,
    NULL,
  };
