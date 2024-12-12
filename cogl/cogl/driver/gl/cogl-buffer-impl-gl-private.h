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
 *
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 */

#pragma once

#include "cogl/cogl-driver-private.h"
#include "cogl/cogl-types.h"
#include "cogl/cogl-context.h"
#include "cogl/cogl-buffer.h"
#include "cogl/cogl-buffer-private.h"
#include "cogl/cogl-buffer-impl-private.h"

#define COGL_TYPE_BUFFER_IMPL_GL (cogl_buffer_impl_gl_get_type ())

G_DECLARE_FINAL_TYPE (CoglBufferImplGL,
                      cogl_buffer_impl_gl,
                      COGL,
                      BUFFER_IMPL_GL,
                      CoglBufferImpl)

void *
_cogl_buffer_gl_bind (CoglBuffer *buffer,
                      CoglBufferBindTarget target,
                      GError **error);

void
_cogl_buffer_gl_unbind (CoglBuffer *buffer);
