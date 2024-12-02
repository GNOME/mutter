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
 */

#include "config.h"

#include "cogl/driver/gl/cogl-texture-driver-gl-private.h"
#include "cogl/driver/gl/cogl-texture-2d-gl-private.h"

G_DEFINE_TYPE (CoglTextureDriverGL, cogl_texture_driver_gl, COGL_TYPE_TEXTURE_DRIVER)

static void
cogl_texture_driver_gl_class_init (CoglTextureDriverGLClass *klass)
{
  CoglTextureDriverClass *driver_klass = COGL_TEXTURE_DRIVER_CLASS (klass);

  driver_klass->texture_2d_free = _cogl_texture_2d_gl_free;
  driver_klass->texture_2d_can_create = _cogl_texture_2d_gl_can_create;
  driver_klass->texture_2d_init = _cogl_texture_2d_gl_init;
  driver_klass->texture_2d_allocate = _cogl_texture_2d_gl_allocate;
  driver_klass->texture_2d_copy_from_framebuffer = _cogl_texture_2d_gl_copy_from_framebuffer;
  driver_klass->texture_2d_get_gl_handle = _cogl_texture_2d_gl_get_gl_handle;
  driver_klass->texture_2d_generate_mipmap = _cogl_texture_2d_gl_generate_mipmap;
  driver_klass->texture_2d_copy_from_bitmap = _cogl_texture_2d_gl_copy_from_bitmap;
}

static void
cogl_texture_driver_gl_init (CoglTextureDriverGL *driver)
{
}
