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
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 */

#include "config.h"

#include <string.h>

#include "cogl/cogl-private.h"

#include "cogl/cogl-display-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/winsys/cogl-winsys-private.h"


G_DEFINE_TYPE (CoglDisplay, cogl_display, G_TYPE_OBJECT);

static const CoglWinsysVtable *
_cogl_display_get_winsys (CoglDisplay *display)
{
  return display->renderer->winsys_vtable;
}

static void
cogl_display_dispose (GObject *object)
{
  CoglDisplay *display = COGL_DISPLAY (object);

  const CoglWinsysVtable *winsys;

  if (display->setup)
    {
      winsys = _cogl_display_get_winsys (display);
      winsys->display_destroy (display);
      display->setup = FALSE;
    }

  g_clear_object (&display->renderer);
  g_clear_object (&display->onscreen_template);

  G_OBJECT_CLASS (cogl_display_parent_class)->dispose (object);
}

static void
cogl_display_init (CoglDisplay *display)
{
}

static void
cogl_display_class_init (CoglDisplayClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_display_dispose;
}

CoglDisplay *
cogl_display_new (CoglRenderer         *renderer,
                  CoglOnscreenTemplate *onscreen_template)
{
  CoglDisplay *display = g_object_new (COGL_TYPE_DISPLAY, NULL);
  GError *error = NULL;

  _cogl_init ();

  display->renderer = renderer;
  if (renderer)
    g_object_ref (renderer);
  else
    display->renderer = cogl_renderer_new ();

  if (!cogl_renderer_connect (display->renderer, &error))
    g_error ("Failed to connect to renderer: %s\n", error->message);

  display->setup = FALSE;

  cogl_display_set_onscreen_template (display, onscreen_template);

  return display;
}

CoglRenderer *
cogl_display_get_renderer (CoglDisplay *display)
{
  return display->renderer;
}

void
cogl_display_set_onscreen_template (CoglDisplay *display,
                                    CoglOnscreenTemplate *onscreen_template)
{
  g_return_if_fail (display->setup == FALSE);

  if (onscreen_template)
    g_object_ref (onscreen_template);

  if (display->onscreen_template)
    g_object_unref (display->onscreen_template);

  display->onscreen_template = onscreen_template;

  /* NB: we want to maintain the invariable that there is always an
   * onscreen template associated with a CoglDisplay... */
  if (!onscreen_template)
    display->onscreen_template = cogl_onscreen_template_new (NULL);
}

gboolean
cogl_display_setup (CoglDisplay *display,
                    GError **error)
{
  const CoglWinsysVtable *winsys;

  if (display->setup)
    return TRUE;

  winsys = _cogl_display_get_winsys (display);
  if (!winsys->display_setup (display, error))
    return FALSE;

  display->setup = TRUE;

  return TRUE;
}
