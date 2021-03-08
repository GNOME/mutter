/*
 * Copyright (C) 2007,2008,2009,2010,2011,2013 Intel Corporation.
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
 */

#ifndef COGL_ONSCREEN_EGL_H
#define COGL_ONSCREEN_EGL_H

#include "cogl-onscreen.h"
#include "winsys/cogl-winsys-egl-private.h"

#define COGL_TYPE_ONSCREEN_EGL (cogl_onscreen_egl_get_type ())
COGL_EXPORT
G_DECLARE_DERIVABLE_TYPE (CoglOnscreenEgl, cogl_onscreen_egl,
                          COGL, ONSCREEN_EGL,
                          CoglOnscreen)

struct _CoglOnscreenEglClass
{
  /*< private >*/
  CoglOnscreenClass parent_class;
};

COGL_EXPORT void
cogl_onscreen_egl_set_egl_surface (CoglOnscreenEgl *onscreen_egl,
                                   EGLSurface       egl_surface);

COGL_EXPORT EGLSurface
cogl_onscreen_egl_get_egl_surface (CoglOnscreenEgl *onscreen_egl);

gboolean
cogl_onscreen_egl_choose_config (CoglOnscreenEgl  *onscreen_egl,
                                 EGLConfig        *out_egl_config,
                                 GError          **error);

#endif /* COGL_ONSCREEN_EGL_H */
