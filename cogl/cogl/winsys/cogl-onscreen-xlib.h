/*
 * Copyright (C) 2011,2013 Intel Corporation.
 * Copyrigth (C) 2020 Red Hat
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

#ifndef COGL_ONSCREEN_XLIB_H
#define COGL_ONSCREEN_XLIB_H

#include "cogl-onscreen.h"
#include "winsys/cogl-onscreen-egl.h"
#include "winsys/cogl-winsys-egl-private.h"

#define COGL_TYPE_ONSCREEN_XLIB (cogl_onscreen_xlib_get_type ())
G_DECLARE_FINAL_TYPE (CoglOnscreenXlib, cogl_onscreen_xlib,
                      COGL, ONSCREEN_XLIB,
                      CoglOnscreenEgl)

gboolean
_cogl_winsys_egl_onscreen_xlib_init (CoglOnscreen  *onscreen,
                                     EGLConfig      egl_config,
                                     GError       **error);

COGL_EXPORT CoglOnscreenXlib *
cogl_onscreen_xlib_new (CoglContext *context,
                        int          width,
                        int          height);

void
_cogl_winsys_egl_onscreen_xlib_deinit (CoglOnscreen *onscreen);

gboolean
cogl_onscreen_xlib_is_for_window (CoglOnscreen *onscreen,
                                  Window        window);

void
cogl_onscreen_xlib_resize (CoglOnscreen *onscreen,
                           int           width,
                           int           height);

#endif /* COGL_ONSCREEN_XLIB_H */
