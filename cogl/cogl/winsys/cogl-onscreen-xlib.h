/*
 * Copyright (C) 2011,2013 Intel Corporation.
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

#pragma once

#include "cogl/cogl-onscreen.h"
#include "cogl/winsys/cogl-onscreen-egl.h"

#define COGL_TYPE_ONSCREEN_XLIB (cogl_onscreen_xlib_get_type ())
G_DECLARE_FINAL_TYPE (CoglOnscreenXlib, cogl_onscreen_xlib,
                      COGL, ONSCREEN_XLIB,
                      CoglOnscreenEgl)

COGL_EXPORT CoglOnscreenXlib *
cogl_onscreen_xlib_new (CoglContext *context,
                        int          width,
                        int          height);

gboolean
cogl_onscreen_xlib_is_for_window (CoglOnscreen *onscreen,
                                  Window        window);

void
cogl_onscreen_xlib_resize (CoglOnscreen *onscreen,
                           int           width,
                           int           height);
