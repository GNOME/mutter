/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2024 Red Hat.
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

#include "config.h"

#include "cogl/cogl-driver-private.h"
#include "cogl/cogl-texture-driver.h"

typedef struct _CoglTextureDriverPrivate
{
  CoglDriver *driver;
} CoglTextureDriverPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (CoglTextureDriver, cogl_texture_driver, G_TYPE_OBJECT)

enum
{
  PROP_0,

  PROP_DRIVER,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

static void
cogl_texture_driver_set_property (GObject *gobject,
                                  unsigned int prop_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  CoglTextureDriver *tex_driver = COGL_TEXTURE_DRIVER (gobject);
  CoglTextureDriverPrivate *priv =
    cogl_texture_driver_get_instance_private (tex_driver);

  switch (prop_id)
    {

    case PROP_DRIVER:
      priv->driver = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (tex_driver, prop_id, pspec);
      break;
    }
}

static void
cogl_texture_driver_class_init (CoglTextureDriverClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = cogl_texture_driver_set_property;

  obj_props[PROP_DRIVER] =
    g_param_spec_object ("driver", NULL, NULL,
                         COGL_TYPE_DRIVER,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     obj_props);
}

static void
cogl_texture_driver_init (CoglTextureDriver *driver)
{
}

CoglDriver *
cogl_texture_driver_get_driver (CoglTextureDriver *tex_driver)
{
  CoglTextureDriverPrivate *priv =
    cogl_texture_driver_get_instance_private (tex_driver);

   return priv->driver;
}
