/*
 * Copyright (C) 2020 Red Hat
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
 */

#include "cogl/driver/nop/cogl-driver-nop.h"
#include "cogl/cogl-context-private.h"


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

G_DEFINE_FINAL_TYPE (CoglNopDriver, cogl_nop_driver, COGL_TYPE_DRIVER);

static gboolean
cogl_nop_driver_texture_2d_can_create (CoglDriver      *driver,
                                       CoglContext     *ctx,
                                       int              width,
                                       int              height,
                                       CoglPixelFormat  internal_format)
{
  return TRUE;
}

static gboolean
cogl_nop_driver_texture_2d_allocate (CoglDriver   *driver,
                                     CoglTexture  *tex,
                                     GError      **error)
{
  return TRUE;
}

static void
cogl_nop_driver_texture_2d_copy_from_framebuffer (CoglDriver      *driver,
                                                  CoglTexture2D   *tex_2d,
                                                  int              src_x,
                                                  int              src_y,
                                                  int              width,
                                                  int              height,
                                                  CoglFramebuffer *src_fb,
                                                  int              dst_x,
                                                  int              dst_y,
                                                  int              level)
{
}

static unsigned int
cogl_nop_driver_texture_2d_get_gl_handle (CoglDriver    *driver,
                                          CoglTexture2D *tex_2d)
{
  return 0;
}

static gboolean
cogl_nop_driver_texture_2d_copy_from_bitmap (CoglDriver     *driver,
                                             CoglTexture2D  *tex_2d,
                                             int             src_x,
                                             int             src_y,
                                             int             width,
                                             int             height,
                                             CoglBitmap     *bitmap,
                                             int             dst_x,
                                             int             dst_y,
                                             int             level,
                                             GError        **error)
{
  return TRUE;
}

static gboolean
cogl_nop_driver_update_features (CoglDriver   *driver,
                                 CoglContext  *ctx,
                                 GError      **error)
{
  memset (ctx->private_features, 0, sizeof (ctx->private_features));

  return TRUE;
}

static gboolean
cogl_nop_driver_context_init (CoglDriver  *driver,
                              CoglContext *context)
{
  return TRUE;
}

static gboolean
cogl_nop_driver_is_hardware_accelerated (CoglDriver  *driver,
                                         CoglContext *context)
{
  return FALSE;
}

static const char *
cogl_nop_driver_get_vendor (CoglDriver  *driver,
                            CoglContext *context)
{
  return "NOP";
}

static CoglFramebufferDriver *
cogl_nop_driver_create_framebuffer_driver (CoglDriver                         *driver,
                                           CoglContext                        *context,
                                           CoglFramebuffer                    *framebuffer,
                                           const CoglFramebufferDriverConfig  *driver_config,
                                           GError                            **error)
{
  return g_object_new (COGL_TYPE_NOP_FRAMEBUFFER,
                       "framebuffer", framebuffer,
                       NULL);
}


static void
cogl_nop_driver_class_init (CoglNopDriverClass *klass)
{
  CoglDriverClass *driver_klass = COGL_DRIVER_CLASS (klass);

  driver_klass->create_framebuffer_driver = cogl_nop_driver_create_framebuffer_driver;

  driver_klass->texture_2d_can_create = cogl_nop_driver_texture_2d_can_create;
  driver_klass->texture_2d_allocate = cogl_nop_driver_texture_2d_allocate;
  driver_klass->texture_2d_copy_from_framebuffer = cogl_nop_driver_texture_2d_copy_from_framebuffer;
  driver_klass->texture_2d_get_gl_handle = cogl_nop_driver_texture_2d_get_gl_handle;
  driver_klass->texture_2d_copy_from_bitmap = cogl_nop_driver_texture_2d_copy_from_bitmap;

  driver_klass->update_features = cogl_nop_driver_update_features;
  driver_klass->context_init = cogl_nop_driver_context_init;
  driver_klass->is_hardware_accelerated = cogl_nop_driver_is_hardware_accelerated;
  driver_klass->get_vendor = cogl_nop_driver_get_vendor;
}

static void
cogl_nop_driver_init (CoglNopDriver *driver)
{
}
