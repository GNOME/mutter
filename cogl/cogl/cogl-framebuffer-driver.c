/*
 * Copyright (C) 2007,2008,2009,2012 Intel Corporation.
 * Copyright (C) 2019 DisplayLink (UK) Ltd.
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
 *
 */

#include "config.h"

#include "cogl/cogl-framebuffer-driver.h"

enum
{
  PROP_0,

  PROP_FRAMEBUFFER,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _CoglFramebufferDriverPrivate
{
  CoglFramebuffer *framebuffer;
} CoglFramebufferDriverPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (CoglFramebufferDriver,
                                     cogl_framebuffer_driver,
                                     G_TYPE_OBJECT)

CoglFramebuffer *
cogl_framebuffer_driver_get_framebuffer (CoglFramebufferDriver *driver)
{
  CoglFramebufferDriverPrivate *priv =
    cogl_framebuffer_driver_get_instance_private (driver);

  return priv->framebuffer;
}

void
cogl_framebuffer_driver_query_bits (CoglFramebufferDriver *driver,
                                    CoglFramebufferBits   *bits)
{
  COGL_FRAMEBUFFER_DRIVER_GET_CLASS (driver)->query_bits (driver, bits);
}

void
cogl_framebuffer_driver_clear (CoglFramebufferDriver *driver,
                               unsigned long          buffers,
                               float                  red,
                               float                  green,
                               float                  blue,
                               float                  alpha)
{
  COGL_FRAMEBUFFER_DRIVER_GET_CLASS (driver)->clear (driver,
                                                     buffers,
                                                     red,
                                                     green,
                                                     blue,
                                                     alpha);
}

void
cogl_framebuffer_driver_finish (CoglFramebufferDriver *driver)
{
  COGL_FRAMEBUFFER_DRIVER_GET_CLASS (driver)->finish (driver);
}

void
cogl_framebuffer_driver_flush (CoglFramebufferDriver *driver)
{
  COGL_FRAMEBUFFER_DRIVER_GET_CLASS (driver)->flush (driver);
}

void
cogl_framebuffer_driver_discard_buffers (CoglFramebufferDriver *driver,
                                         unsigned long          buffers)
{
  COGL_FRAMEBUFFER_DRIVER_GET_CLASS (driver)->discard_buffers (driver, buffers);
}

void
cogl_framebuffer_driver_draw_attributes (CoglFramebufferDriver  *driver,
                                         CoglPipeline           *pipeline,
                                         CoglVerticesMode        mode,
                                         int                     first_vertex,
                                         int                     n_vertices,
                                         CoglAttribute         **attributes,
                                         int                     n_attributes,
                                         CoglDrawFlags           flags)
{
  COGL_FRAMEBUFFER_DRIVER_GET_CLASS (driver)->draw_attributes (driver,
                                                               pipeline,
                                                               mode,
                                                               first_vertex,
                                                               n_vertices,
                                                               attributes,
                                                               n_attributes,
                                                               flags);
}

void
cogl_framebuffer_driver_draw_indexed_attributes (CoglFramebufferDriver  *driver,
                                                 CoglPipeline           *pipeline,
                                                 CoglVerticesMode        mode,
                                                 int                     first_vertex,
                                                 int                     n_vertices,
                                                 CoglIndices            *indices,
                                                 CoglAttribute         **attributes,
                                                 int                     n_attributes,
                                                 CoglDrawFlags           flags)
{
  CoglFramebufferDriverClass *klass =
    COGL_FRAMEBUFFER_DRIVER_GET_CLASS (driver);

  klass->draw_indexed_attributes (driver,
                                  pipeline,
                                  mode,
                                  first_vertex,
                                  n_vertices,
                                  indices,
                                  attributes,
                                  n_attributes,
                                  flags);
}

gboolean
cogl_framebuffer_driver_read_pixels_into_bitmap (CoglFramebufferDriver  *driver,
                                                 int                     x,
                                                 int                     y,
                                                 CoglReadPixelsFlags     source,
                                                 CoglBitmap             *bitmap,
                                                 GError                **error)
{
  CoglFramebufferDriverClass *klass =
    COGL_FRAMEBUFFER_DRIVER_GET_CLASS (driver);

  return klass->read_pixels_into_bitmap (driver, x, y, source, bitmap, error);
}

static void
cogl_framebuffer_driver_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (object);
  CoglFramebufferDriverPrivate *priv =
    cogl_framebuffer_driver_get_instance_private (driver);

  switch (prop_id)
    {
    case PROP_FRAMEBUFFER:
      g_value_set_object (value, priv->framebuffer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
cogl_framebuffer_driver_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (object);
  CoglFramebufferDriverPrivate *priv =
    cogl_framebuffer_driver_get_instance_private (driver);

  switch (prop_id)
    {
    case PROP_FRAMEBUFFER:
      priv->framebuffer = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
cogl_framebuffer_driver_init (CoglFramebufferDriver *driver)
{
}

static void
cogl_framebuffer_driver_class_init (CoglFramebufferDriverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = cogl_framebuffer_driver_get_property;
  object_class->set_property = cogl_framebuffer_driver_set_property;

  obj_props[PROP_FRAMEBUFFER] =
    g_param_spec_object ("framebuffer", NULL, NULL,
                         COGL_TYPE_FRAMEBUFFER,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}
