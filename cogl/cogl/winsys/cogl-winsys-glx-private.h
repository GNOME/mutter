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

#pragma once

#include "cogl/winsys/cogl-winsys.h"

G_DECLARE_FINAL_TYPE (CoglWinsysGlx,
                      cogl_winsys_glx,
                      COGL,
                      WINSYS_GLX,
                      CoglWinsys)

#define COGL_TYPE_WINSYS_GLX (cogl_winsys_glx_get_type ())

gboolean
cogl_display_glx_find_fbconfig (CoglDisplay  *display,
                                GLXFBConfig  *config_ret,
                                GError      **error);

void
cogl_context_glx_set_current_drawable (CoglContext *context,
                                       GLXDrawable  drawable);

GLXDrawable
cogl_context_glx_get_current_drawable (CoglContext *context);
