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

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#include "cogl/cogl-util.h"
#include "cogl/cogl-private.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-mutter.h"

#include "cogl/cogl-renderer.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-driver-private.h"
#include "cogl/driver/nop/cogl-driver-nop-private.h"


static CoglDriverId _cogl_drivers[] =
{
#ifdef HAVE_GL
  COGL_DRIVER_ID_GL3,
#endif
#ifdef HAVE_GLES2
  COGL_DRIVER_ID_GLES2,
#endif
  COGL_DRIVER_ID_NOP,
};

typedef struct _CoglRendererPrivate
{
  GObject parent_instance;

  gboolean connected;
  CoglDriver *driver;

  CoglDriverId driver_id;
} CoglRendererPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (CoglRenderer, cogl_renderer, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_DRIVER,
  N_PROPS
};

static GParamSpec *props[N_PROPS] = { NULL };

static void
cogl_renderer_dispose (GObject *object)
{
  CoglRenderer *renderer = COGL_RENDERER (object);
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  g_clear_object (&priv->driver);

  G_OBJECT_CLASS (cogl_renderer_parent_class)->dispose (object);
}

static void
cogl_renderer_get_property (GObject      *object,
                            unsigned int  prop_id,
                            GValue       *value,
                            GParamSpec   *pspec)
{
  CoglRenderer *renderer = COGL_RENDERER (object);
  CoglRendererPrivate *priv = cogl_renderer_get_instance_private (renderer);

  switch (prop_id)
    {
    case PROP_DRIVER:
      g_value_set_object (value, priv->driver);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cogl_renderer_set_property (GObject      *object,
                            unsigned int  prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  CoglRenderer *renderer = COGL_RENDERER (object);
  CoglRendererPrivate *priv = cogl_renderer_get_instance_private (renderer);

  switch (prop_id)
    {
    case PROP_DRIVER:
      g_set_object (&priv->driver, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cogl_renderer_init (CoglRenderer *renderer)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  priv->connected = FALSE;
}

static void
cogl_renderer_class_init (CoglRendererClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_renderer_dispose;
  object_class->get_property = cogl_renderer_get_property;
  object_class->set_property = cogl_renderer_set_property;

  props[PROP_DRIVER] =
    g_param_spec_object ("driver", NULL, NULL,
                         COGL_TYPE_DRIVER,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

uint32_t
cogl_renderer_error_quark (void)
{
  return g_quark_from_static_string ("cogl-renderer-error-quark");
}

static CoglDriverId
driver_name_to_id (const char *name)
{
  if (g_ascii_strcasecmp ("gl3", name) == 0)
    return COGL_DRIVER_ID_GL3;
  else if (g_ascii_strcasecmp ("gles2", name) == 0)
    return COGL_DRIVER_ID_GLES2;
  else if (g_ascii_strcasecmp ("nop", name) == 0)
    return COGL_DRIVER_ID_NOP;

  return COGL_DRIVER_ID_ANY;
}

static gboolean
_cogl_renderer_choose_driver (CoglRenderer  *renderer,
                              GError       **error)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);
  const char *driver_name = g_getenv ("COGL_DRIVER");
  CoglDriverId driver_override = COGL_DRIVER_ID_ANY;
  int i;

  if (driver_name)
    {
      driver_override = driver_name_to_id (driver_name);
      if (driver_override == COGL_DRIVER_ID_ANY)
        {
          g_set_error (error, COGL_RENDERER_ERROR,
                       COGL_RENDERER_ERROR_BAD_CONSTRAINT,
                       "Driver \"%s\" is not available",
                       driver_name);
          return FALSE;
        }
    }

  if (driver_override != COGL_DRIVER_ID_ANY)
    {
      priv->driver_id = driver_override;

      if (!COGL_RENDERER_GET_CLASS (renderer)->load_driver (renderer,
                                                            driver_override,
                                                            error))
        return FALSE;

      return TRUE;
    }

  for (i = 0; i < G_N_ELEMENTS (_cogl_drivers); i++)
    {
      CoglDriverId candidate = _cogl_drivers[i];

      if (candidate == COGL_DRIVER_ID_NOP)
        continue;

      if (COGL_RENDERER_GET_CLASS (renderer)->load_driver (renderer,
                                                           candidate,
                                                           NULL))
        {
          priv->driver_id = candidate;
          return TRUE;
        }
    }

  g_set_error (error, COGL_RENDERER_ERROR,
               COGL_RENDERER_ERROR_BAD_CONSTRAINT,
               "No suitable driver found");
  return FALSE;
}

/* Final connection API */

gboolean
cogl_renderer_connect (CoglRenderer *renderer, GError **error)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);
  CoglRendererClass *class = COGL_RENDERER_GET_CLASS (renderer);

  if (priv->connected)
    return TRUE;

  if (class->connect && !class->connect (renderer, error))
    return FALSE;

  if (!_cogl_renderer_choose_driver (renderer, error))
    return FALSE;

  priv->connected = TRUE;
  return TRUE;
}

void *
cogl_renderer_get_proc_address (CoglRenderer *renderer,
                                const char   *name)
{
  CoglRendererClass *class = COGL_RENDERER_GET_CLASS (renderer);

  return class->get_proc_address (renderer, name);
}

void
cogl_renderer_set_driver (CoglRenderer *renderer,
                          CoglDriver   *driver)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  priv->driver = driver;
}


CoglDriverId
cogl_renderer_get_driver_id (CoglRenderer *renderer)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  return priv->driver_id;
}

GArray *
cogl_renderer_query_drm_modifiers (CoglRenderer           *renderer,
                                   CoglPixelFormat         format,
                                   CoglDrmModifierFilter   filter,
                                   GError                **error)
{
  CoglRendererClass *class = COGL_RENDERER_GET_CLASS (renderer);

  if (class->query_drm_modifiers)
    {
      return class->query_drm_modifiers (renderer,
                                         format,
                                         filter,
                                         error);
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "CoglRenderer doesn't support querying drm modifiers");

  return NULL;
}

uint64_t
cogl_renderer_get_implicit_drm_modifier (CoglRenderer *renderer)
{
  CoglRendererClass *class = COGL_RENDERER_GET_CLASS (renderer);

  g_return_val_if_fail (class->get_implicit_drm_modifier, 0);

  return class->get_implicit_drm_modifier (renderer);
}

gboolean
cogl_renderer_is_implicit_drm_modifier (CoglRenderer *renderer,
                                        uint64_t      modifier)
{
  CoglRendererClass *class = COGL_RENDERER_GET_CLASS (renderer);
  uint64_t implicit_modifier;

  g_return_val_if_fail (class->get_implicit_drm_modifier, FALSE);

  implicit_modifier = class->get_implicit_drm_modifier (renderer);
  return modifier == implicit_modifier;
}

CoglDmaBufHandle *
cogl_renderer_create_dma_buf (CoglRenderer     *renderer,
                              CoglPixelFormat   format,
                              uint64_t         *modifiers,
                              int               n_modifiers,
                              int               width,
                              int               height,
                              GError          **error)
{
  CoglRendererClass *class = COGL_RENDERER_GET_CLASS (renderer);

  if (class->create_dma_buf)
    return class->create_dma_buf (renderer,
                                  format,
                                  modifiers, n_modifiers,
                                  width, height,
                                  error);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "CoglRenderer doesn't support creating DMA buffers");

  return NULL;
}

gboolean
cogl_renderer_is_dma_buf_supported (CoglRenderer *renderer)
{
  CoglRendererClass *class = COGL_RENDERER_GET_CLASS (renderer);

  if (class->is_dma_buf_supported)
    return class->is_dma_buf_supported (renderer);
  else
    return FALSE;
}

CoglFramebuffer *
cogl_renderer_create_dma_buf_framebuffer (CoglRenderer     *renderer,
                                          CoglContext      *context,
                                          uint32_t          width,
                                          uint32_t          height,
                                          uint32_t          drm_format,
                                          CoglPixelFormat   cogl_format,
                                          int               n_planes,
                                          const int        *fds,
                                          const uint32_t   *strides,
                                          const uint32_t   *offsets,
                                          const uint64_t   *modifiers,
                                          GError          **error)
{
  CoglRendererClass *class = COGL_RENDERER_GET_CLASS (renderer);

  if (class->create_dma_buf_framebuffer)
    return class->create_dma_buf_framebuffer (renderer, context,
                                              width, height,
                                              drm_format, cogl_format,
                                              n_planes,
                                              fds, strides,
                                              offsets, modifiers,
                                              error);

  g_set_error (error, COGL_RENDERER_ERROR,
               COGL_RENDERER_ERROR_BAD_CONSTRAINT,
               "DMA buf framebuffers not supported by this renderer");
  return NULL;
}

void
cogl_renderer_bind_api (CoglRenderer *renderer)
{
  CoglRendererClass *class = COGL_RENDERER_GET_CLASS (renderer);

  class->bind_api (renderer);
}

CoglDriver *
cogl_renderer_get_driver (CoglRenderer *renderer)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  return priv->driver;
}

void
cogl_renderer_update_sync (CoglRenderer *renderer)
{
  CoglRendererClass *class = COGL_RENDERER_GET_CLASS (renderer);

  if (!class->update_sync)
    return;

  class->update_sync (renderer);
}

int
cogl_renderer_get_latest_sync_fd (CoglRenderer *renderer)
{
  CoglRendererClass *class = COGL_RENDERER_GET_CLASS (renderer);

  if (!class->get_sync_fd)
    return -1;

  return class->get_sync_fd (renderer);
}
