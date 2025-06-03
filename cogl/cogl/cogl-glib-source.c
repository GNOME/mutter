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
 *
 *
 */

#include "config.h"

#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-glib-source.h"

typedef struct _CoglGLibSource
{
  GSource source;

  CoglRenderer *renderer;

  int64_t expiration_time;
} CoglGLibSource;

static gboolean
cogl_glib_source_prepare (GSource *source, int *timeout)
{
  CoglGLibSource *cogl_source = (CoglGLibSource *) source;

  if (_cogl_list_empty (cogl_renderer_get_idle_closures (cogl_source->renderer)))
    {
      *timeout = -1;
      cogl_source->expiration_time = -1;
    }
  else
    {
      /* Round up to ensure that we don't try again too early */
      *timeout = 999 / 1000;
      cogl_source->expiration_time = g_source_get_time (source);
    }

  return *timeout == 0;
}

static gboolean
cogl_glib_source_check (GSource *source)
{
  CoglGLibSource *cogl_source = (CoglGLibSource *) source;

  if (cogl_source->expiration_time >= 0 &&
      g_source_get_time (source) >= cogl_source->expiration_time)
    return TRUE;

  return FALSE;
}

static gboolean
cogl_glib_source_dispatch (GSource *source,
                           GSourceFunc callback,
                           void *user_data)
{
  CoglGLibSource *cogl_source = (CoglGLibSource *) source;

  _cogl_closure_list_invoke_no_args (cogl_renderer_get_idle_closures (cogl_source->renderer));

  return TRUE;
}

static GSourceFuncs
cogl_glib_source_funcs =
  {
    cogl_glib_source_prepare,
    cogl_glib_source_check,
    cogl_glib_source_dispatch,
    NULL
  };

GSource *
cogl_glib_source_new (CoglRenderer *renderer,
                      int           priority)
{
  GSource *source;
  CoglGLibSource *cogl_source;

  source = g_source_new (&cogl_glib_source_funcs,
                         sizeof (CoglGLibSource));
  g_source_set_name (source, "[mutter] Cogl");
  cogl_source = (CoglGLibSource *) source;

  cogl_source->renderer = renderer;

  if (priority != G_PRIORITY_DEFAULT)
    g_source_set_priority (source, priority);

  return source;
}
