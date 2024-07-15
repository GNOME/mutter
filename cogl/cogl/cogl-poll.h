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
 * Authors:
 *  Neil Roberts <neil@linux.intel.com>
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-context.h"

#include <poll.h>

#define COGL_SYSDEF_POLLIN POLLIN
#define COGL_SYSDEF_POLLPRI POLLPRI
#define COGL_SYSDEF_POLLOUT POLLOUT
#define COGL_SYSDEF_POLLERR POLLERR
#define COGL_SYSDEF_POLLHUP POLLHUP
#define COGL_SYSDEF_POLLNVAL POLLNVAL

G_BEGIN_DECLS

/**
 * CoglPoll:
 *
 * Functions for integrating Cogl with an application's main loop
 *
 * Cogl needs to integrate with the application's main loop so that it
 * can internally handle some events from the driver. All Cogl
 * applications must use these functions. They provide enough
 * information to describe the state that Cogl will need to wake up
 * on. An application using the GLib main loop can instead use
 * cogl_glib_source_new() which provides a #GSource ready to be added
 * to the main loop.
 */


/**
 * cogl_poll_renderer_has_idle_closures:
 * @renderer: A #CoglRenderer
 */
COGL_EXPORT gboolean
cogl_poll_renderer_has_idle_closures (CoglRenderer *renderer);

/**
 * cogl_poll_renderer_dispatch:
 * @renderer: A #CoglRenderer
 *
 * This should be called whenever an application is woken up from
 * going idle in its main loop.
 */
COGL_EXPORT void
cogl_poll_renderer_dispatch (CoglRenderer *renderer);

G_END_DECLS
