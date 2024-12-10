/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2008,2009,2010,2011 Intel Corporation.
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

#include "cogl/cogl-attribute-private.h"
#include "cogl/cogl-framebuffer-driver.h"

#define COGL_TYPE_FRAMEBUFFER_GL (cogl_framebuffer_gl_get_type ())
G_DECLARE_DERIVABLE_TYPE (CoglFramebufferGL, cogl_framebuffer_gl,
                          COGL, FRAMEBUFFER_GL,
                          CoglFramebufferDriver)

struct _CoglFramebufferGLClass
{
  CoglFramebufferDriverClass parent_class;

  void (* bind) (CoglFramebufferGL *framebuffer,
                 GLenum             target);
};

void
cogl_framebuffer_gl_bind (CoglFramebufferGL *framebuffer,
                          GLenum             target);

void
cogl_framebuffer_gl_flush_state_differences (CoglFramebufferGL *framebuffer,
                                             unsigned long      differences);
