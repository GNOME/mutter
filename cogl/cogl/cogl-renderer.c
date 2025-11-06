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
#include "cogl/cogl-display-private.h"

#include "cogl/winsys/cogl-winsys.h"

#ifdef HAVE_GL
#include "cogl/driver/gl/gl3/cogl-driver-gl3-private.h"
#endif
#ifdef HAVE_GLES2
#include "cogl/driver/gl/gles2/cogl-driver-gles2-private.h"
#endif
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
  CoglDriverId driver_override;
  CoglDriver *driver;
  CoglWinsys *winsys;

  CoglList idle_closures;

  CoglDriverId driver_id;
  GModule *libgl_module;

  void *winsys_user_data;
  GDestroyNotify winsys_user_data_destroy;
} CoglRendererPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (CoglRenderer, cogl_renderer, G_TYPE_OBJECT);

static void
cogl_renderer_dispose (GObject *object)
{
  CoglRenderer *renderer = COGL_RENDERER (object);
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  g_clear_pointer (&priv->winsys_user_data,
                   priv->winsys_user_data_destroy);

  _cogl_closure_list_disconnect_all (&priv->idle_closures);

  g_clear_object (&priv->winsys);

  if (priv->libgl_module)
    g_module_close (priv->libgl_module);

  g_clear_object (&priv->driver);

  G_OBJECT_CLASS (cogl_renderer_parent_class)->dispose (object);
}

static void
cogl_renderer_init (CoglRenderer *renderer)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  priv->connected = FALSE;

  _cogl_list_init (&priv->idle_closures);
}

static void
cogl_renderer_class_init (CoglRendererClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_renderer_dispose;
}

uint32_t
cogl_renderer_error_quark (void)
{
  return g_quark_from_static_string ("cogl-renderer-error-quark");
}

CoglRenderer *
cogl_renderer_new (void)
{
  CoglRenderer *renderer = g_object_new (COGL_TYPE_RENDERER, NULL);

  return renderer;
}

typedef gboolean (*CoglDriverCallback) (CoglDriverId  driver_id,
                                        void         *user_data);

static void
foreach_driver_description (CoglDriverId        driver_override,
                            CoglDriverCallback  callback,
                            void               *user_data)
{
  int i;

  if (driver_override != COGL_DRIVER_ID_ANY)
    {
      for (i = 0; i < G_N_ELEMENTS (_cogl_drivers); i++)
        {
          if (_cogl_drivers[i] == driver_override)
            {
              callback (_cogl_drivers[i], user_data);
              return;
            }
        }

      g_warn_if_reached ();
      return;
    }

  for (i = 0; i < G_N_ELEMENTS (_cogl_drivers); i++)
    {
      if (!callback (_cogl_drivers[i], user_data))
        return;
    }
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

  g_warn_if_reached ();
  return COGL_DRIVER_ID_ANY;
}

static const char *
driver_id_to_name (CoglDriverId id)
{
  switch (id)
    {
      case COGL_DRIVER_ID_GL3:
        return "gl3";
      case COGL_DRIVER_ID_GLES2:
        return "gles2";
      case COGL_DRIVER_ID_NOP:
        return "nop";
      case COGL_DRIVER_ID_ANY:
        g_warn_if_reached ();
        return "any";
    }

  g_warn_if_reached ();
  return "unknown";
}

/* XXX this is still uglier than it needs to be */
static gboolean
satisfy_constraints (CoglDriverId  driver_id,
                     void         *user_data)
{
  CoglDriverId *state = user_data;

  *state = driver_id;

  return FALSE;
}

static gboolean
_cogl_renderer_choose_driver (CoglRenderer *renderer,
                              GError **error)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);
  const char *driver_name = g_getenv ("COGL_DRIVER");
  CoglDriverId picked_driver, driver_override;
  const char *invalid_override = NULL;
  const char *libgl_name = NULL;
  int i;

  picked_driver = driver_override = COGL_DRIVER_ID_ANY;

  if (driver_name)
    {
      driver_override = driver_name_to_id (driver_name);
      if (driver_override == COGL_DRIVER_ID_ANY)
        invalid_override = driver_name;
    }

  if (priv->driver_override != COGL_DRIVER_ID_ANY)
    {
      if (driver_override != COGL_DRIVER_ID_ANY &&
          priv->driver_override != driver_override)
        {
          g_set_error (error, COGL_RENDERER_ERROR,
                       COGL_RENDERER_ERROR_BAD_CONSTRAINT,
                       "Application driver selection conflicts with driver "
                       "specified in configuration");
          return FALSE;
        }

      driver_override = priv->driver_override;
    }

  if (driver_override != COGL_DRIVER_ID_ANY)
    {
      gboolean found = FALSE;

      for (i = 0; i < G_N_ELEMENTS (_cogl_drivers); i++)
        {
          if (_cogl_drivers[i] == driver_override)
            {
              found = TRUE;
              break;
            }
        }
      if (!found)
        invalid_override = driver_id_to_name (driver_override);
    }

  if (invalid_override)
    {
      g_set_error (error, COGL_RENDERER_ERROR,
                   COGL_RENDERER_ERROR_BAD_CONSTRAINT,
                   "Driver \"%s\" is not available",
                   invalid_override);
      return FALSE;
    }


  foreach_driver_description (driver_override,
                              satisfy_constraints,
                              &picked_driver);

  if (picked_driver == COGL_DRIVER_ID_ANY)
    {
      g_set_error (error, COGL_RENDERER_ERROR,
                   COGL_RENDERER_ERROR_BAD_CONSTRAINT,
                   "No suitable driver found");
      return FALSE;
    }

  priv->driver_id = picked_driver;

  switch (priv->driver_id)
    {
#ifdef HAVE_GL
    case COGL_DRIVER_ID_GL3:
      priv->driver = g_object_new (COGL_TYPE_DRIVER_GL3, NULL);
      libgl_name = COGL_GL_LIBNAME;
      break;
#endif
#ifdef HAVE_GLES2
    case COGL_DRIVER_ID_GLES2:
      priv->driver = g_object_new (COGL_TYPE_DRIVER_GLES2, NULL);
      libgl_name = COGL_GLES2_LIBNAME;
      break;
#endif

    case COGL_DRIVER_ID_NOP:
    default:
      priv->driver = g_object_new (COGL_TYPE_DRIVER_NOP, NULL);
      break;
    }

  if (libgl_name)
    {
      priv->libgl_module = g_module_open (libgl_name,
                                          G_MODULE_BIND_LAZY);

      if (priv->libgl_module == NULL)
        {
          g_set_error (error, COGL_DRIVER_ERROR,
                       COGL_DRIVER_ERROR_FAILED_TO_LOAD_LIBRARY,
                       "Failed to dynamically open the GL library \"%s\"",
                       libgl_name);
          return FALSE;
        }
    }

  return TRUE;
}

/* Final connection API */

void
cogl_renderer_set_custom_winsys (CoglRenderer *renderer,
                                 CoglWinsys   *winsys)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  priv->winsys = winsys;
}

gboolean
cogl_renderer_connect (CoglRenderer *renderer, GError **error)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);
  CoglWinsysClass *winsys_class;

  if (priv->connected)
    return TRUE;

  /* The driver needs to be chosen before connecting the renderer
     because eglInitialize requires the library containing the GL API
     to be loaded before its called */
  if (!_cogl_renderer_choose_driver (renderer, error))
    return FALSE;

  if (!priv->winsys)
    {
      g_set_error (error, COGL_WINSYS_ERROR, COGL_WINSYS_ERROR_INIT,
                   "Failed to connected to any renderer: no winsys set");
      return FALSE;
    }

  winsys_class = COGL_WINSYS_GET_CLASS (priv->winsys);
  if (winsys_class->renderer_connect &&
      !winsys_class->renderer_connect (priv->winsys, renderer, error))
    {
      g_clear_object (&priv->winsys);
      return FALSE;
    }

  priv->connected = TRUE;
  return TRUE;
}

void *
cogl_renderer_get_proc_address (CoglRenderer *renderer,
                                 const char   *name)
{
  CoglWinsys *winsys = cogl_renderer_get_winsys (renderer);
  CoglWinsysClass *winsys_class = COGL_WINSYS_GET_CLASS (winsys);

  return winsys_class->renderer_get_proc_address (winsys, renderer, name);
}

void
cogl_renderer_set_driver (CoglRenderer *renderer,
                          CoglDriverId  driver)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  g_return_if_fail (!priv->connected);

  priv->driver_override = driver;
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
  CoglWinsys *winsys = cogl_renderer_get_winsys (renderer);
  CoglWinsysClass *winsys_class = COGL_WINSYS_GET_CLASS (winsys);

  if (winsys_class->renderer_query_drm_modifiers)
    {
      return winsys_class->renderer_query_drm_modifiers (winsys,
                                                         renderer,
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
  CoglWinsys *winsys = cogl_renderer_get_winsys (renderer);
  CoglWinsysClass *winsys_class = COGL_WINSYS_GET_CLASS (winsys);

  g_return_val_if_fail (winsys_class->renderer_get_implicit_drm_modifier, 0);

  return winsys_class->renderer_get_implicit_drm_modifier (winsys, renderer);
}

gboolean
cogl_renderer_is_implicit_drm_modifier (CoglRenderer *renderer,
                                        uint64_t      modifier)
{
  CoglWinsys *winsys = cogl_renderer_get_winsys (renderer);
  CoglWinsysClass *winsys_class = COGL_WINSYS_GET_CLASS (winsys);
  uint64_t implicit_modifier;

  g_return_val_if_fail (winsys_class->renderer_get_implicit_drm_modifier, FALSE);

  implicit_modifier = winsys_class->renderer_get_implicit_drm_modifier (winsys,
                                                                        renderer);
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
  CoglWinsys *winsys = cogl_renderer_get_winsys (renderer);
  CoglWinsysClass *winsys_class = COGL_WINSYS_GET_CLASS (winsys);

  if (winsys_class->renderer_create_dma_buf)
    return winsys_class->renderer_create_dma_buf (winsys,
                                                  renderer,
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
  CoglWinsys *winsys = cogl_renderer_get_winsys (renderer);
  CoglWinsysClass *winsys_class = COGL_WINSYS_GET_CLASS (winsys);

  if (winsys_class->renderer_is_dma_buf_supported)
    return winsys_class->renderer_is_dma_buf_supported (winsys, renderer);
  else
    return FALSE;
}

void
cogl_renderer_bind_api (CoglRenderer *renderer)
{
  CoglWinsys *winsys = cogl_renderer_get_winsys (renderer);
  CoglWinsysClass *winsys_class = COGL_WINSYS_GET_CLASS (winsys);

  winsys_class->renderer_bind_api (winsys, renderer);
}

CoglDriver *
cogl_renderer_get_driver (CoglRenderer *renderer)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  return priv->driver;
}

CoglWinsys *
cogl_renderer_get_winsys (CoglRenderer *renderer)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  return priv->winsys;
}

void *
cogl_renderer_get_winsys_data (CoglRenderer *renderer)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  return priv->winsys_user_data;
}

void
cogl_renderer_set_winsys_data (CoglRenderer   *renderer,
                               void           *winsys,
                               GDestroyNotify  destroy)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  priv->winsys_user_data = winsys;
  priv->winsys_user_data_destroy = destroy;
}

CoglClosure *
cogl_renderer_add_idle_closure (CoglRenderer  *renderer,
                                void (*closure)(void *),
                                gpointer       data)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  return _cogl_closure_list_add (&priv->idle_closures,
                                 closure,
                                 data,
                                 NULL);
}

CoglList *
cogl_renderer_get_idle_closures (CoglRenderer *renderer)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  return &priv->idle_closures;
}

GModule *
cogl_renderer_get_gl_module (CoglRenderer *renderer)
{
  CoglRendererPrivate *priv =
    cogl_renderer_get_instance_private (renderer);

  return priv->libgl_module;
}
