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

#include "cogl/driver/nop/cogl-driver-nop-private.h"
#include "cogl/cogl-private.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-feature-private.h"
#include "cogl/cogl-renderer-private.h"

G_DEFINE_FINAL_TYPE (CoglDriverNop, cogl_driver_nop,
                     COGL_TYPE_DRIVER)

#define COGL_TYPE_FRAMEBUFFER_NOP (cogl_framebuffer_nop_get_type ())
G_DECLARE_FINAL_TYPE (CoglFramebufferNop, cogl_framebuffer_nop,
                      COGL, FRAMEBUFFER_DRIVER_NOP,
                      CoglFramebufferDriver)


struct _CoglFramebufferNop
{
  CoglFramebufferDriver parent;
};

G_DEFINE_FINAL_TYPE (CoglFramebufferNop, cogl_framebuffer_nop,
                     COGL_TYPE_FRAMEBUFFER_DRIVER)


static void
cogl_framebuffer_nop_init (CoglFramebufferNop *nop_framebuffer)
{
}

static void
cogl_framebuffer_nop_class_init (CoglFramebufferNopClass *klass)
{
}

static gboolean
cogl_driver_nop_update_features (CoglDriver   *driver,
                                 CoglContext  *ctx,
                                 GError      **error)
{
  memset (ctx->private_features, 0, sizeof (ctx->private_features));

  return TRUE;
}

static const char *
cogl_driver_nop_get_vendor (CoglDriver  *driver,
                            CoglContext *context)
{
  return "NOP";
}

static CoglFramebufferDriver *
cogl_driver_nop_create_framebuffer_driver (CoglDriver                         *driver,
                                           CoglContext                        *context,
                                           CoglFramebuffer                    *framebuffer,
                                           const CoglFramebufferDriverConfig  *driver_config,
                                           GError                            **error)
{
  return g_object_new (COGL_TYPE_FRAMEBUFFER_NOP,
                       "framebuffer", framebuffer,
                       NULL);
}

static void
cogl_driver_nop_class_init (CoglDriverNopClass *klass)
{
  CoglDriverClass *driver_klass = COGL_DRIVER_CLASS (klass);

  driver_klass->create_framebuffer_driver = cogl_driver_nop_create_framebuffer_driver;

  driver_klass->update_features = cogl_driver_nop_update_features;
  driver_klass->get_vendor = cogl_driver_nop_get_vendor;
}

static void
cogl_driver_nop_init (CoglDriverNop *driver)
{
}
