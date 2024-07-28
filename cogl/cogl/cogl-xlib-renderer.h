/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011 Intel Corporation.
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

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "cogl/cogl-renderer.h"

G_BEGIN_DECLS

/**
 * cogl_xlib_renderer_set_foreign_display: (skip)
 * @renderer: a #CoglRenderer
 *
 * Sets a foreign Xlib display that Cogl will use for and Xlib based winsys
 * backend.
 *
 * Note that calling this function will automatically disable Cogl's
 * event retrieval. Cogl still needs to see all of the X events so the
 * application should also use cogl_renderer_handle_event() if it
 * uses this function.
 */
COGL_EXPORT void
cogl_xlib_renderer_set_foreign_display (CoglRenderer *renderer,
                                        Display *display);

G_END_DECLS
