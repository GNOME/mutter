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

#ifndef COGL_GL_FRAMEBUFFER_BACK_H
#define COGL_GL_FRAMEBUFFER_BACK_H

#include "cogl-framebuffer-gl-private.h"

#define COGL_TYPE_GL_FRAMEBUFFER_BACK (cogl_gl_framebuffer_back_get_type ())
G_DECLARE_FINAL_TYPE (CoglGlFramebufferBack, cogl_gl_framebuffer_back,
                      COGL, GL_FRAMEBUFFER_BACK,
                      CoglGlFramebuffer)

CoglGlFramebufferBack *
cogl_gl_framebuffer_back_new (CoglFramebuffer                    *framebuffer,
                              const CoglFramebufferDriverConfig  *driver_config,
                              GError                            **error);

#endif /* COGL_GL_FRAMEBUFFER_BACK_H */
