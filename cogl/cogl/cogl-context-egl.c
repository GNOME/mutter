/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2025 Red Hat.
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

#include "config.h"

#include "cogl/cogl-context-egl.h"

#include <gio/gio.h>

struct _CoglContextEGL
{
  CoglContext parent_instance;
};

G_DEFINE_FINAL_TYPE (CoglContextEGL, cogl_context_egl, COGL_TYPE_CONTEXT)

static void
cogl_context_egl_init (CoglContextEGL *context_egl)
{
}

static void
cogl_context_egl_class_init (CoglContextEGLClass *class)
{
}

CoglContext *
cogl_context_egl_new (CoglDisplay  *display,
                      GError      **error)
{
  g_return_val_if_fail (display != NULL, NULL);

  return g_initable_new (COGL_TYPE_CONTEXT_EGL, NULL, error,
                         "display", display,
                         NULL);
}
