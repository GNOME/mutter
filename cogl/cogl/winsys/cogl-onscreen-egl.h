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

gboolean
_cogl_winsys_onscreen_egl_init (CoglOnscreen  *onscreen,
                                GError       **error);

void
_cogl_winsys_onscreen_egl_deinit (CoglOnscreen *onscreen);

void
_cogl_winsys_onscreen_egl_bind (CoglOnscreen *onscreen);

int
_cogl_winsys_onscreen_egl_get_buffer_age (CoglOnscreen *onscreen);

void
_cogl_winsys_onscreen_egl_swap_region (CoglOnscreen  *onscreen,
                                       const int     *user_rectangles,
                                       int            n_rectangles,
                                       CoglFrameInfo *info,
                                       gpointer       user_data);

void
_cogl_winsys_onscreen_egl_swap_buffers_with_damage (CoglOnscreen  *onscreen,
                                                    const int     *rectangles,
                                                    int            n_rectangles,
                                                    CoglFrameInfo *info,
                                                    gpointer       user_data);

#endif /* COGL_ONSCREEN_EGL_H */
