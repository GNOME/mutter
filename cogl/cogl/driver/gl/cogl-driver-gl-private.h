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

#pragma once

#include "cogl/cogl-driver-private.h"

typedef struct _CoglDriverGLPrivate
{
  GArray *texture_units;
  int active_texture_unit;

  /* This is used for generated fake unique sampler object numbers
   when the sampler object extension is not supported */
  GLuint next_fake_sampler_object_number;
} CoglDriverGLPrivate;


G_DECLARE_DERIVABLE_TYPE (CoglDriverGL,
                          cogl_driver_gl,
                          COGL,
                          DRIVER_GL,
                          CoglDriver);

struct _CoglDriverGLClass
{
  CoglDriverClass parent_class;

  CoglPixelFormat (* pixel_format_to_gl) (CoglDriverGL    *driver,
                                          CoglContext     *context,
                                          CoglPixelFormat  format,
                                          GLenum          *out_glintformat,
                                          GLenum          *out_glformat,
                                          GLenum          *out_gltype);

  CoglPixelFormat (* get_read_pixels_format) (CoglDriverGL    *driver,
                                              CoglContext     *context,
                                              CoglPixelFormat  from,
                                              CoglPixelFormat  to,
                                              GLenum          *gl_format_out,
                                              GLenum          *gl_type_out);
};

#define COGL_TYPE_DRIVER_GL (cogl_driver_gl_get_type ())

CoglDriverGLPrivate * cogl_driver_gl_get_private (CoglDriverGL *driver);
