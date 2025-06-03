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

#include "cogl/winsys/cogl-winsys-private.h"

#ifdef HAVE_EGL_PLATFORM_XLIB
#include "cogl/winsys/cogl-winsys-egl-x11-private.h"
#endif
#ifdef HAVE_GLX
#include "cogl/winsys/cogl-winsys-glx-private.h"
#endif

#ifdef HAVE_GL
#include "cogl/driver/gl/gl3/cogl-driver-gl3-private.h"
#endif
#ifdef HAVE_GLES2
#include "cogl/driver/gl/gles2/cogl-driver-gles2-private.h"
#endif
#include "cogl/driver/nop/cogl-driver-nop-private.h"

#ifdef HAVE_X11
#include "cogl/cogl-xlib-renderer.h"
#include "cogl/cogl-xlib-renderer-private.h"
#endif


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

static CoglWinsysVtableGetter _cogl_winsys_vtable_getters[] =
{
#ifdef HAVE_GLX
  _cogl_winsys_glx_get_vtable,
#endif
#ifdef HAVE_EGL_PLATFORM_XLIB
  _cogl_winsys_egl_xlib_get_vtable,
#endif
};

typedef struct _CoglNativeFilterClosure
{
  CoglNativeFilterFunc func;
  void *data;
} CoglNativeFilterClosure;

typedef struct _CoglRenderer
{
  GObject parent_instance;

  CoglDisplay *display;

  gboolean connected;
  CoglDriverId driver_override;
  CoglDriver *driver;
  const CoglWinsysVtable *winsys_vtable;
  void *custom_winsys_user_data;
  gboolean should_free_custom_winsys_user_data;
  CoglCustomWinsysVtableGetter custom_winsys_vtable_getter;

  CoglList idle_closures;

  CoglDriverId driver_id;
  GModule *libgl_module;

  /* List of callback functions that will be given every native event */
  GSList *event_filters;
  void *winsys;
} CoglRenderer;

static void
native_filter_closure_free (CoglNativeFilterClosure *closure)
{
  g_free (closure);
}

G_DEFINE_FINAL_TYPE (CoglRenderer, cogl_renderer, G_TYPE_OBJECT);

static void
cogl_renderer_dispose (GObject *object)
{
  CoglRenderer *renderer = COGL_RENDERER (object);

  const CoglWinsysVtable *winsys = cogl_renderer_get_winsys_vtable (renderer);

  _cogl_closure_list_disconnect_all (&renderer->idle_closures);

  if (winsys && winsys->renderer_disconnect)
    winsys->renderer_disconnect (renderer);

  g_clear_pointer (&renderer->winsys, g_free);
  if (renderer->should_free_custom_winsys_user_data)
    g_clear_pointer (&renderer->custom_winsys_user_data, g_free);

  if (renderer->libgl_module)
    g_module_close (renderer->libgl_module);

  g_slist_free_full (renderer->event_filters,
                     (GDestroyNotify) native_filter_closure_free);

  g_clear_object (&renderer->driver);

  G_OBJECT_CLASS (cogl_renderer_parent_class)->dispose (object);
}

static void
cogl_renderer_init (CoglRenderer *renderer)
{
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

  renderer->connected = FALSE;
  renderer->event_filters = NULL;

  _cogl_list_init (&renderer->idle_closures);

  return renderer;
}

#ifdef HAVE_X11
void
cogl_xlib_renderer_set_foreign_display (CoglRenderer *renderer,
                                        Display *xdisplay)
{
  CoglXlibRenderer *xlib_renderer;
  g_return_if_fail (COGL_IS_RENDERER (renderer));

  /* NB: Renderers are considered immutable once connected */
  g_return_if_fail (!renderer->connected);

  xlib_renderer = _cogl_xlib_renderer_get_data (renderer);
  xlib_renderer->xdpy = xdisplay;
}
#endif /* HAVE_X11 */

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

  if (renderer->driver_override != COGL_DRIVER_ID_ANY)
    {
      if (driver_override != COGL_DRIVER_ID_ANY &&
          renderer->driver_override != driver_override)
        {
          g_set_error (error, COGL_RENDERER_ERROR,
                       COGL_RENDERER_ERROR_BAD_CONSTRAINT,
                       "Application driver selection conflicts with driver "
                       "specified in configuration");
          return FALSE;
        }

      driver_override = renderer->driver_override;
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

  renderer->driver_id = picked_driver;

  switch (renderer->driver_id)
    {
#ifdef HAVE_GL
    case COGL_DRIVER_ID_GL3:
      renderer->driver = g_object_new (COGL_TYPE_DRIVER_GL3, NULL);
      libgl_name = COGL_GL_LIBNAME;
      break;
#endif
#ifdef HAVE_GLES2
    case COGL_DRIVER_ID_GLES2:
      renderer->driver = g_object_new (COGL_TYPE_DRIVER_GLES2, NULL);
      libgl_name = COGL_GLES2_LIBNAME;
      break;
#endif

    case COGL_DRIVER_ID_NOP:
    default:
      renderer->driver = g_object_new (COGL_TYPE_DRIVER_NOP, NULL);
      break;
    }

  if (libgl_name)
    {
      renderer->libgl_module = g_module_open (libgl_name,
                                              G_MODULE_BIND_LAZY);

      if (renderer->libgl_module == NULL)
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
cogl_renderer_set_custom_winsys (CoglRenderer                *renderer,
                                 CoglCustomWinsysVtableGetter winsys_vtable_getter,
                                 void                        *user_data)
{
  renderer->custom_winsys_user_data = user_data;
  renderer->custom_winsys_vtable_getter = winsys_vtable_getter;
  renderer->should_free_custom_winsys_user_data = FALSE;
}

static gboolean
connect_custom_winsys (CoglRenderer *renderer,
                       GError **error)
{
  const CoglWinsysVtable *winsys;
  GError *tmp_error = NULL;
  GString *error_message;

  winsys = renderer->custom_winsys_vtable_getter (renderer);
  renderer->winsys_vtable = winsys;

  error_message = g_string_new ("");
  if (!winsys->renderer_connect (renderer, &tmp_error))
    {
      g_string_append_c (error_message, '\n');
      g_string_append (error_message, tmp_error->message);
      g_error_free (tmp_error);
      /* Free any leftover state, for now */
      g_clear_pointer (&renderer->winsys, g_free);
    }
  else
    {
      renderer->connected = TRUE;
      g_string_free (error_message, TRUE);
      return TRUE;
    }

  renderer->winsys_vtable = NULL;
  g_set_error (error, COGL_WINSYS_ERROR, COGL_WINSYS_ERROR_INIT,
               "Failed to connected to any renderer: %s", error_message->str);
  g_string_free (error_message, TRUE);
  return FALSE;
}

gboolean
cogl_renderer_connect (CoglRenderer *renderer, GError **error)
{
  int i;
  g_autoptr (GString) error_message = NULL;

  if (renderer->connected)
    return TRUE;

  /* The driver needs to be chosen before connecting the renderer
     because eglInitialize requires the library containing the GL API
     to be loaded before its called */
  if (!_cogl_renderer_choose_driver (renderer, error))
    return FALSE;

  if (renderer->custom_winsys_vtable_getter)
    return connect_custom_winsys (renderer, error);

  error_message = g_string_new ("");
  for (i = 0; i < G_N_ELEMENTS (_cogl_winsys_vtable_getters); i++)
    {
      const CoglWinsysVtable *winsys = _cogl_winsys_vtable_getters[i]();
      GError *tmp_error = NULL;

      /* At least temporarily we will associate this winsys with
       * the renderer in-case ->renderer_connect calls API that
       * wants to query the current winsys... */
      renderer->winsys_vtable = winsys;

      if (!winsys->renderer_connect (renderer, &tmp_error))
        {
          g_string_append_c (error_message, '\n');
          g_string_append (error_message, tmp_error->message);
          g_error_free (tmp_error);
          /* Free any leftover state, for now */
          g_clear_pointer (&renderer->winsys, g_free);
        }
      else
        {
          renderer->connected = TRUE;
          return TRUE;
        }
    }

  if (!renderer->connected)
    {
      renderer->winsys_vtable = NULL;
      g_set_error (error, COGL_WINSYS_ERROR, COGL_WINSYS_ERROR_INIT,
                   "Failed to connected to any renderer: %s",
                   error_message->str);
      return FALSE;
    }

  return TRUE;
}

CoglFilterReturn
cogl_renderer_handle_event (CoglRenderer *renderer,
                            void         *event)
{
  GSList *l, *next;

  /* Pass the event on to all of the registered filters in turn */
  for (l = renderer->event_filters; l; l = next)
    {
      CoglNativeFilterClosure *closure = l->data;

      /* The next pointer is taken now so that we can handle the
         closure being removed during emission */
      next = l->next;

      if (closure->func (event, closure->data) == COGL_FILTER_REMOVE)
        return COGL_FILTER_REMOVE;
    }

  /* If the backend for the renderer also wants to see the events, it
     should just register its own filter */

  return COGL_FILTER_CONTINUE;
}

void
_cogl_renderer_add_native_filter (CoglRenderer *renderer,
                                  CoglNativeFilterFunc func,
                                  void *data)
{
  CoglNativeFilterClosure *closure;

  closure = g_new0 (CoglNativeFilterClosure, 1);
  closure->func = func;
  closure->data = data;

  renderer->event_filters = g_slist_prepend (renderer->event_filters, closure);
}

void
_cogl_renderer_remove_native_filter (CoglRenderer *renderer,
                                     CoglNativeFilterFunc func,
                                     void *data)
{
  GSList *l, *prev = NULL;

  for (l = renderer->event_filters; l; prev = l, l = l->next)
    {
      CoglNativeFilterClosure *closure = l->data;

      if (closure->func == func && closure->data == data)
        {
          native_filter_closure_free (closure);
          if (prev)
            prev->next = g_slist_delete_link (prev->next, l);
          else
            renderer->event_filters =
              g_slist_delete_link (renderer->event_filters, l);
          break;
        }
    }
}

CoglWinsysID
cogl_renderer_get_winsys_id (CoglRenderer *renderer)
{
  g_return_val_if_fail (renderer->connected, 0);

  return renderer->winsys_vtable->id;
}

void *
cogl_renderer_get_proc_address (CoglRenderer *renderer,
                                 const char   *name)
{
  const CoglWinsysVtable *winsys = cogl_renderer_get_winsys_vtable (renderer);

  return winsys->renderer_get_proc_address (renderer, name);
}

void
cogl_renderer_set_driver (CoglRenderer *renderer,
                          CoglDriverId  driver)
{
  g_return_if_fail (!renderer->connected);
  renderer->driver_override = driver;
}

CoglDriverId
cogl_renderer_get_driver_id (CoglRenderer *renderer)
{
  return renderer->driver_id;
}

GArray *
cogl_renderer_query_drm_modifiers (CoglRenderer           *renderer,
                                   CoglPixelFormat         format,
                                   CoglDrmModifierFilter   filter,
                                   GError                **error)
{
  const CoglWinsysVtable *winsys = cogl_renderer_get_winsys_vtable (renderer);

  if (winsys->renderer_query_drm_modifiers)
    {
      return winsys->renderer_query_drm_modifiers (renderer,
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
  const CoglWinsysVtable *winsys = cogl_renderer_get_winsys_vtable (renderer);

  g_return_val_if_fail (winsys->renderer_get_implicit_drm_modifier, 0);

  return winsys->renderer_get_implicit_drm_modifier (renderer);
}

gboolean
cogl_renderer_is_implicit_drm_modifier (CoglRenderer *renderer,
                                        uint64_t      modifier)
{
  const CoglWinsysVtable *winsys = cogl_renderer_get_winsys_vtable (renderer);
  uint64_t implicit_modifier;

  g_return_val_if_fail (winsys->renderer_get_implicit_drm_modifier, FALSE);

  implicit_modifier = winsys->renderer_get_implicit_drm_modifier (renderer);
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
  const CoglWinsysVtable *winsys = cogl_renderer_get_winsys_vtable (renderer);

  if (winsys->renderer_create_dma_buf)
    return winsys->renderer_create_dma_buf (renderer,
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
  const CoglWinsysVtable *winsys = cogl_renderer_get_winsys_vtable (renderer);

  if (winsys->renderer_is_dma_buf_supported)
    return winsys->renderer_is_dma_buf_supported (renderer);
  else
    return FALSE;
}

void
cogl_renderer_bind_api (CoglRenderer *renderer)
{
  const CoglWinsysVtable *winsys = cogl_renderer_get_winsys_vtable (renderer);

  winsys->renderer_bind_api (renderer);
}

CoglDriver *
cogl_renderer_get_driver (CoglRenderer *renderer)
{
  return renderer->driver;
}

const CoglWinsysVtable *
cogl_renderer_get_winsys_vtable (CoglRenderer *renderer)
{
  return renderer->winsys_vtable;
}

void *
cogl_renderer_get_winsys (CoglRenderer *renderer)
{
  return renderer->winsys;
}

void
cogl_renderer_set_winsys (CoglRenderer *renderer,
                          void         *winsys)
{
  renderer->winsys = winsys;
}

CoglClosure *
cogl_renderer_add_idle_closure (CoglRenderer  *renderer,
                                void (*closure)(void *),
                                gpointer       data)
{
  return _cogl_closure_list_add (&renderer->idle_closures,
                                 closure,
                                 data,
                                 NULL);
}

CoglList *
cogl_renderer_get_idle_closures (CoglRenderer *renderer)
{
  return &renderer->idle_closures;
}

GModule *
cogl_renderer_get_gl_module (CoglRenderer *renderer)
{
  return renderer->libgl_module;
}

CoglDisplay *
cogl_renderer_get_display (CoglRenderer *renderer)
{
  return renderer->display;
}

void
cogl_renderer_set_display (CoglRenderer *renderer,
                           CoglDisplay   *display)
{
  renderer->display = display;
}

void *
cogl_renderer_get_custom_winsys_data (CoglRenderer *renderer)
{
  return renderer->custom_winsys_user_data;
}

void
cogl_renderer_set_custom_winsys_data (CoglRenderer *renderer,
                                      void         *winsys_data)
{
  renderer->custom_winsys_user_data = winsys_data;
  renderer->should_free_custom_winsys_user_data = TRUE;
}
