/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011 Intel Corporation.
 *               2025 Red Hat.
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

#include <EGL/eglext.h>

#include "cogl/cogl-context.h"
#include "cogl/winsys/cogl-winsys.h"

#define COGL_TYPE_WINSYS_EGL (cogl_winsys_egl_get_type ())

COGL_EXPORT
G_DECLARE_DERIVABLE_TYPE (CoglWinsysEGL,
                          cogl_winsys_egl,
                          COGL,
                          WINSYS_EGL,
                          CoglWinsys)

struct _CoglWinsysEGLClass
{
  CoglWinsysClass parent_class;

  gboolean (* context_created) (CoglWinsysEGL  *winsys,
                                CoglDisplay    *display,
                                GError        **error);

  void (* cleanup_context) (CoglWinsysEGL *winsys,
                            CoglDisplay   *display);

  gboolean (* choose_config) (CoglWinsysEGL  *winsys,
                              CoglDisplay    *display,
                              EGLint         *attributes,
                              EGLConfig      *out_config,
                              GError        **error);
};

#define COGL_MAX_EGL_CONFIG_ATTRIBS 30
