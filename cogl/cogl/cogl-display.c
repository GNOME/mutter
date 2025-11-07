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

#include "cogl/cogl-renderer-private.h"
#include "cogl/winsys/cogl-winsys.h"

typedef struct _CoglDisplayPrivate
{
  CoglContext *context;

  gboolean setup;
  CoglRenderer *renderer;

} CoglDisplayPrivate;

enum
{
  PROP_0,
  PROP_RENDERER,
  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

G_DEFINE_TYPE_WITH_PRIVATE (CoglDisplay, cogl_display, G_TYPE_OBJECT);


static void
cogl_display_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  CoglDisplay *display = COGL_DISPLAY (object);
  CoglDisplayPrivate *priv =
    cogl_display_get_instance_private (display);

  switch (prop_id)
    {
    case PROP_RENDERER:
      g_value_set_object (value, priv->renderer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cogl_display_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  CoglDisplay *display = COGL_DISPLAY (object);
  CoglDisplayPrivate *priv =
    cogl_display_get_instance_private (display);

  switch (prop_id)
    {
    case PROP_RENDERER:
      priv->renderer = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cogl_display_dispose (GObject *object)
{
  CoglDisplay *display = COGL_DISPLAY (object);
  CoglDisplayPrivate *priv = cogl_display_get_instance_private (display);

  if (priv->setup)
    {
      CoglWinsys *winsys = cogl_renderer_get_winsys (priv->renderer);
      CoglWinsysClass *winsys_class = COGL_WINSYS_GET_CLASS (winsys);

      winsys_class->display_destroy (winsys, display);
      priv->setup = FALSE;
    }

  g_clear_object (&priv->renderer);

  G_OBJECT_CLASS (cogl_display_parent_class)->dispose (object);
}

static void
cogl_display_init (CoglDisplay *display)
{
  CoglDisplayPrivate *priv = cogl_display_get_instance_private (display);

  priv->setup = FALSE;
}

static void
cogl_display_class_init (CoglDisplayClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->get_property = cogl_display_get_property;
  object_class->set_property = cogl_display_set_property;
  object_class->dispose = cogl_display_dispose;

  obj_props[PROP_RENDERER] =
    g_param_spec_object ("renderer", NULL, NULL,
                         COGL_TYPE_RENDERER,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

CoglDisplay *
cogl_display_new (CoglRenderer *renderer)
{
  CoglDisplay *display;

  g_return_val_if_fail (renderer != NULL, NULL);

  display = g_object_new (COGL_TYPE_DISPLAY,
                          "renderer", renderer,
                          NULL);

  return display;
}

CoglRenderer *
cogl_display_get_renderer (CoglDisplay *display)
{
  CoglDisplayPrivate *priv = cogl_display_get_instance_private (display);

  return priv->renderer;
}

gboolean
cogl_display_setup (CoglDisplay *display,
                    GError **error)
{
  CoglDisplayPrivate *priv = cogl_display_get_instance_private (display);
  CoglWinsys *winsys = cogl_renderer_get_winsys (priv->renderer);
  CoglWinsysClass *winsys_class = COGL_WINSYS_GET_CLASS (winsys);

  if (priv->setup)
    return TRUE;

  if (!winsys_class->display_setup (winsys, display, error))
    return FALSE;

  priv->setup = TRUE;

  return TRUE;
}
