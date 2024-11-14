/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2024 Red Hat.
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

#include "config.h"

#include "cogl/cogl-driver-private.h"

G_DEFINE_ABSTRACT_TYPE (CoglDriver, cogl_driver, G_TYPE_OBJECT);


static void
cogl_driver_flush_attributes_state_default (CoglDriver          *driver,
                                            CoglFramebuffer     *framebuffer,
                                            CoglPipeline        *pipeline,
                                            CoglFlushLayerState *layers_state,
                                            CoglDrawFlags        flags,
                                            CoglAttribute      **attributes,
                                            int                  n_attributes)
{
}

static void
cogl_driver_clip_stack_flush_default (CoglDriver      *driver,
                                      CoglClipStack   *stack,
                                      CoglFramebuffer *framebuffer)
{
}

static void
cogl_driver_flush_framebuffer_state_default (CoglDriver          *driver,
                                             CoglContext          *ctx,
                                             CoglFramebuffer      *draw_buffer,
                                             CoglFramebuffer      *read_buffer,
                                             CoglFramebufferState  state)
{
}

static void
cogl_driver_context_deinit_default (CoglDriver  *driver,
                                    CoglContext *context)
{
}

static void
cogl_driver_texture_2d_init_default (CoglDriver  *driver,
                                     CoglTexture2D *tex_2d)
{
}

static void
cogl_driver_texture_2d_free_default (CoglDriver  *driver,
                                     CoglTexture2D *tex_2d)
{
}

static void
cogl_driver_texture_2d_generate_mipmap_default (CoglDriver    *driver,
                                                CoglTexture2D *tex_2d)
{
}

static void
cogl_driver_class_init (CoglDriverClass *klass)
{
  klass->flush_attributes_state = cogl_driver_flush_attributes_state_default;
  klass->clip_stack_flush = cogl_driver_clip_stack_flush_default;
  klass->flush_framebuffer_state = cogl_driver_flush_framebuffer_state_default;
  klass->context_deinit = cogl_driver_context_deinit_default;
  klass->texture_2d_init = cogl_driver_texture_2d_init_default;
  klass->texture_2d_free = cogl_driver_texture_2d_free_default;
  klass->texture_2d_generate_mipmap = cogl_driver_texture_2d_generate_mipmap_default;
}

static void
cogl_driver_init (CoglDriver *driver)
{

}
