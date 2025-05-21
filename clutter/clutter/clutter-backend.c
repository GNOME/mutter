/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By:
 *      Matthew Allum <mallum@openedhand.com>
 *      Emmanuele Bassi <ebassi@linux.intel.com>
 *
 * Copyright (C) 2006, 2007, 2008 OpenedHand Ltd
 * Copyright (C) 2009, 2010 Intel Corp
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * ClutterBackend:
 *
 * Backend abstraction
 *
 * Clutter can be compiled against different backends. Each backend
 * has to implement a set of functions, in order to be used by Clutter.
 *
 * #ClutterBackend is the base class abstracting the various implementation;
 * it provides a basic API to query the backend for generic information
 * and settings.
 */

#include "config.h"

#ifdef HAVE_FONTS
#include <cairo.h>
#include <pango/pangocairo.h>
#endif

#include "clutter/clutter-backend-private.h"
#include "clutter/clutter-context-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-event-private.h"
#include "clutter/clutter-marshal.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-stage-manager-private.h"
#include "clutter/clutter-stage-private.h"
#include "clutter/clutter-stage-window.h"

#include "cogl/cogl.h"

enum
{
  RESOLUTION_CHANGED,
  FONT_CHANGED,
  SETTINGS_CHANGED,

  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CONTEXT,
  N_PROPS
};

static GParamSpec *pspecs[N_PROPS] = { 0 };

G_DEFINE_ABSTRACT_TYPE (ClutterBackend, clutter_backend, G_TYPE_OBJECT)

static guint backend_signals[LAST_SIGNAL] = { 0, };

static void
clutter_backend_dispose (GObject *gobject)
{
  ClutterBackend *backend = CLUTTER_BACKEND (gobject);

  /* clear the events still in the queue of the main context */
  _clutter_clear_events_queue ();

  g_clear_object (&backend->cogl_display);
  g_clear_object (&backend->cogl_context);
  g_clear_object (&backend->dummy_onscreen);
  if (backend->stage_window)
    {
      g_object_remove_weak_pointer (G_OBJECT (backend->stage_window),
                                    (gpointer *) &backend->stage_window);
      backend->stage_window = NULL;
    }

  g_clear_pointer (&backend->cogl_source, g_source_destroy);
#ifdef HAVE_FONTS
  g_clear_pointer (&backend->font_options, cairo_font_options_destroy);
#endif
  g_clear_object (&backend->input_method);

  G_OBJECT_CLASS (clutter_backend_parent_class)->dispose (gobject);
}

static void
clutter_backend_get_property (GObject      *object,
                              guint         prop_id,
                              GValue       *value,
                              GParamSpec   *pspec)
{
  ClutterBackend *backend = CLUTTER_BACKEND (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, backend->context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_backend_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  ClutterBackend *backend = CLUTTER_BACKEND (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      backend->context = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

#ifdef HAVE_FONTS
static void
clutter_backend_real_resolution_changed (ClutterBackend *backend)
{
  ClutterContext *context = backend->context;
  ClutterSettings *settings = clutter_context_get_settings (context);
  gdouble resolution;
  gint dpi;

  g_object_get (settings, "font-dpi", &dpi, NULL);

  if (dpi < 0)
    resolution = 96.0;
  else
    resolution = dpi / 1024.0;

  if (context->font_map != NULL)
    pango_cairo_font_map_set_resolution (PANGO_CAIRO_FONT_MAP (context->font_map),
                                         resolution);
}
#endif

static gboolean
clutter_backend_do_real_create_context (ClutterBackend  *backend,
                                        CoglDriverId     driver_id,
                                        GError         **error)
{
  ClutterBackendClass *klass;

  cogl_init ();

  klass = CLUTTER_BACKEND_GET_CLASS (backend);
  CLUTTER_NOTE (BACKEND, "Creating Cogl renderer");
  backend->cogl_renderer = klass->get_renderer (backend, error);

  if (backend->cogl_renderer == NULL)
    goto error;

  CLUTTER_NOTE (BACKEND, "Connecting the renderer");
  cogl_renderer_set_driver (backend->cogl_renderer, driver_id);
  if (!cogl_renderer_connect (backend->cogl_renderer, error))
    goto error;

  CLUTTER_NOTE (BACKEND, "Creating Cogl display");
  backend->cogl_display = cogl_display_new (backend->cogl_renderer);

  if (backend->cogl_display == NULL)
    goto error;

  CLUTTER_NOTE (BACKEND, "Setting up the display");
  if (!cogl_display_setup (backend->cogl_display, error))
    goto error;

  CLUTTER_NOTE (BACKEND, "Creating the Cogl context");
  backend->cogl_context = cogl_context_new (backend->cogl_display, error);
  if (backend->cogl_context == NULL)
    goto error;

  /* the display owns the renderer and the swap chain */
  g_object_unref (backend->cogl_renderer);

  return TRUE;

error:
  g_clear_object (&backend->cogl_display);
  g_clear_object (&backend->cogl_renderer);

  return FALSE;
}

static const struct {
  const char *driver_name;
  const char *driver_desc;
  CoglDriverId driver_id;
} all_known_drivers[] = {
  { "gl3", "OpenGL 3.1 core profile", COGL_DRIVER_ID_GL3 },
  { "gles2", "OpenGL ES 2.0", COGL_DRIVER_ID_GLES2 },
  { "any", "Default Cogl driver", COGL_DRIVER_ID_ANY },
};

static gboolean
clutter_backend_real_create_context (ClutterBackend  *backend,
                                     GError         **error)
{
  GError *internal_error = NULL;
  const char *drivers_list;
  char **known_drivers;
  int i;

  if (backend->cogl_context != NULL)
    return TRUE;

  drivers_list = g_getenv ("CLUTTER_DRIVER");
  if (drivers_list == NULL)
    drivers_list = "*";

  known_drivers = g_strsplit (drivers_list, ",", 0);

  for (i = 0; backend->cogl_context == NULL && known_drivers[i] != NULL; i++)
    {
      const char *driver_name = known_drivers[i];
      gboolean is_any = g_str_equal (driver_name, "*");
      int j;

      for (j = 0; j < G_N_ELEMENTS (all_known_drivers); j++)
        {
          if (is_any ||
              g_str_equal (all_known_drivers[j].driver_name, driver_name))
            {
              CLUTTER_NOTE (BACKEND, "Checking for the %s driver", all_known_drivers[j].driver_desc);

              if (clutter_backend_do_real_create_context (backend, all_known_drivers[j].driver_id, &internal_error))
                break;

              if (internal_error)
                {
                  CLUTTER_NOTE (BACKEND, "Unable to use the %s driver: %s",
                                all_known_drivers[j].driver_desc,
                                internal_error->message);
                  g_clear_error (&internal_error);
                }
            }
        }
    }

  g_strfreev (known_drivers);

  if (backend->cogl_context == NULL)
    {
      if (internal_error != NULL)
        g_propagate_error (error, internal_error);
      else
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Unable to initialize the Clutter backend: no available drivers found.");

      return FALSE;
    }

  backend->cogl_source = cogl_glib_source_new (backend->cogl_renderer, G_PRIORITY_DEFAULT);
  g_source_attach (backend->cogl_source, NULL);

  return TRUE;
}

static void
clutter_backend_class_init (ClutterBackendClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = clutter_backend_dispose;
  gobject_class->get_property = clutter_backend_get_property;
  gobject_class->set_property = clutter_backend_set_property;

  /**
   * ClutterBackend::resolution-changed:
   * @backend: the #ClutterBackend that emitted the signal
   *
   * The signal is emitted each time the font
   * resolutions has been changed through #ClutterSettings.
   */
  backend_signals[RESOLUTION_CHANGED] =
    g_signal_new (I_("resolution-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (ClutterBackendClass, resolution_changed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * ClutterBackend::font-changed:
   * @backend: the #ClutterBackend that emitted the signal
   *
   * The signal is emitted each time the font options
   * have been changed through #ClutterSettings.
   */
  backend_signals[FONT_CHANGED] =
    g_signal_new (I_("font-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * ClutterBackend::settings-changed:
   * @backend: the #ClutterBackend that emitted the signal
   *
   * The signal is emitted each time the #ClutterSettings
   * properties have been changed.
   */
  backend_signals[SETTINGS_CHANGED] =
    g_signal_new (I_("settings-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  pspecs[PROP_CONTEXT] =
    g_param_spec_object ("context", NULL, NULL,
                         CLUTTER_TYPE_CONTEXT,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (gobject_class, N_PROPS, pspecs);

#ifdef HAVE_FONTS
  klass->resolution_changed = clutter_backend_real_resolution_changed;
#endif

  klass->create_context = clutter_backend_real_create_context;
}

static void
clutter_backend_init (ClutterBackend *self)
{
  self->dummy_onscreen = NULL;

  self->fallback_resource_scale = 1.f;

  /* Default font options */
#ifdef HAVE_FONTS
  self->font_options = cairo_font_options_create ();
  cairo_font_options_set_hint_metrics (self->font_options,
                                       CAIRO_HINT_METRICS_ON);
  cairo_font_options_set_hint_style (self->font_options,
                                     CAIRO_HINT_STYLE_NONE);
  cairo_font_options_set_subpixel_order (self->font_options,
                                         CAIRO_SUBPIXEL_ORDER_DEFAULT);
  cairo_font_options_set_antialias (self->font_options,
                                    CAIRO_ANTIALIAS_DEFAULT);
#endif
}

ClutterStageWindow *
_clutter_backend_create_stage (ClutterBackend  *backend,
                               ClutterStage    *wrapper,
                               GError         **error)
{
  ClutterBackendClass *klass;
  ClutterStageWindow *stage_window;

  g_assert (CLUTTER_IS_BACKEND (backend));
  g_assert (CLUTTER_IS_STAGE (wrapper));

  klass = CLUTTER_BACKEND_GET_CLASS (backend);
  if (klass->create_stage != NULL)
    stage_window = klass->create_stage (backend, wrapper, error);
  else
    stage_window = NULL;

  if (stage_window == NULL)
    return NULL;

  g_assert (CLUTTER_IS_STAGE_WINDOW (stage_window));

  backend->stage_window = stage_window;
  g_object_add_weak_pointer (G_OBJECT (backend->stage_window),
                             (gpointer *) &backend->stage_window);

  return stage_window;
}

gboolean
_clutter_backend_create_context (ClutterBackend  *backend,
                                 GError         **error)
{
  ClutterBackendClass *klass;

  klass = CLUTTER_BACKEND_GET_CLASS (backend);

  return klass->create_context (backend, error);
}

/**
 * clutter_get_default_backend:
 *
 * Retrieves the default #ClutterBackend used by Clutter. The
 * #ClutterBackend holds backend-specific configuration options.
 *
 * Return value: (transfer none): the default backend. You should
 *   not ref or unref the returned object. Applications should rarely
 *   need to use this.
 */
ClutterBackend *
clutter_get_default_backend (void)
{
  ClutterContext *clutter_context;

  clutter_context = _clutter_context_get_default ();

  return clutter_context->backend;
}

/**
 * clutter_backend_get_resolution:
 * @backend: a #ClutterBackend
 *
 * Gets the resolution for font handling on the screen.
 *
 * The resolution is a scale factor between points specified in a
 * #PangoFontDescription and cairo units. The default value is 96.0,
 * meaning that a 10 point font will be 13 units
 * high (10 * 96. / 72. = 13.3).
 *
 * Clutter will set the resolution using the current backend when
 * initializing; the resolution is also stored in the
 * #ClutterSettings:font-dpi property.
 *
 * Return value: the current resolution, or -1 if no resolution
 *   has been set.
 */
gdouble
clutter_backend_get_resolution (ClutterBackend *backend)
{
  ClutterSettings *settings;
  gint resolution;

  g_return_val_if_fail (CLUTTER_IS_BACKEND (backend), -1.0);

  settings = clutter_context_get_settings (backend->context);
  g_object_get (settings, "font-dpi", &resolution, NULL);

  if (resolution < 0)
    return 96.0;

  return resolution / 1024.0;
}

/**
 * clutter_backend_get_cogl_context:
 * @backend: a #ClutterBackend
 *
 * Retrieves the #CoglContext associated with the given clutter
 * @backend. A #CoglContext is required when using some of the
 * experimental 2.0 Cogl API.
 *
 * Since CoglContext is itself experimental API this API should
 * be considered experimental too.
 *
 * This API is not yet supported on OSX because OSX still
 * uses the stub Cogl winsys and the Clutter backend doesn't
 * explicitly create a CoglContext.
 *
 * Return value: (transfer none): The #CoglContext associated with @backend.
 */
CoglContext *
clutter_backend_get_cogl_context (ClutterBackend *backend)
{
  return backend->cogl_context;
}

/**
 * clutter_backend_get_input_method:
 * @backend: the #CLutterBackend
 *
 * Returns the input method used by Clutter
 *
 * Returns: (transfer none): the input method
 **/
ClutterInputMethod *
clutter_backend_get_input_method (ClutterBackend *backend)
{
  return backend->input_method;
}

/**
 * clutter_backend_set_input_method:
 * @backend: the #ClutterBackend
 * @method: (nullable): the input method
 *
 * Sets the input method to be used by Clutter
 **/
void
clutter_backend_set_input_method (ClutterBackend     *backend,
                                  ClutterInputMethod *method)
{
  if (backend->input_method == method)
    return;

  if (backend->input_method)
    clutter_input_method_focus_out (backend->input_method);

  g_set_object (&backend->input_method, method);
}

ClutterStageWindow *
clutter_backend_get_stage_window (ClutterBackend *backend)
{
  return backend->stage_window;
}

/**
 * clutter_backend_get_default_seat:
 * @backend: the #ClutterBackend
 *
 * Returns the default seat
 *
 * Returns: (transfer none): the default seat
 **/
ClutterSeat *
clutter_backend_get_default_seat (ClutterBackend *backend)
{
  g_return_val_if_fail (CLUTTER_IS_BACKEND (backend), NULL);

  return CLUTTER_BACKEND_GET_CLASS (backend)->get_default_seat (backend);
}

void
clutter_backend_set_fallback_resource_scale (ClutterBackend *backend,
                                             float           fallback_resource_scale)
{
  backend->fallback_resource_scale = fallback_resource_scale;
}

float
clutter_backend_get_fallback_resource_scale (ClutterBackend *backend)
{
  return backend->fallback_resource_scale;
}

gboolean
clutter_backend_is_display_server (ClutterBackend *backend)
{
  return CLUTTER_BACKEND_GET_CLASS (backend)->is_display_server (backend);
}

/**
 * clutter_backend_get_sprite:
 * @backend: A #ClutterBackend
 * @stage: A #ClutterStage
 * @for_event: Event to get sprite for
 *
 * Retrieves the #ClutterSprite affected by @for_event
 *
 * Returns: (transfer none)(nullable): a #ClutterSprite, or %NULL if event does not drive one
 **/
ClutterSprite *
clutter_backend_get_sprite (ClutterBackend     *backend,
                            ClutterStage       *stage,
                            const ClutterEvent *for_event)
{
  return CLUTTER_BACKEND_GET_CLASS (backend)->get_sprite (backend,
                                                          stage,
                                                          for_event);
}

/**
 * clutter_backend_lookup_sprite: (skip)
 */
ClutterSprite *
clutter_backend_lookup_sprite (ClutterBackend       *backend,
                               ClutterStage         *stage,
                               ClutterInputDevice   *device,
                               ClutterEventSequence *sequence)
{
  return CLUTTER_BACKEND_GET_CLASS (backend)->lookup_sprite (backend,
                                                             stage,
                                                             device,
                                                             sequence);
}

/**
 * clutter_backend_get_pointer_sprite:
 * @backend: a #ClutterBackend
 * @stage: a #ClutterStage
 *
 * Gets the on-screen sprite typically considered "the pointer"
 *
 * Returns: (transfer none): The "pointer" sprite
 */
ClutterSprite *
clutter_backend_get_pointer_sprite (ClutterBackend *backend,
                                    ClutterStage   *stage)
{
  return CLUTTER_BACKEND_GET_CLASS (backend)->get_pointer_sprite (backend, stage);
}

/**
 * clutter_backend_destroy_sprite: (skip)
 */
void
clutter_backend_destroy_sprite (ClutterBackend *backend,
                                ClutterSprite  *sprite)
{
  CLUTTER_BACKEND_GET_CLASS (backend)->destroy_sprite (backend, sprite);
}

/**
 * clutter_backend_foreach_sprite: (skip)
 */
gboolean
clutter_backend_foreach_sprite (ClutterBackend               *backend,
                                ClutterStage                 *stage,
                                ClutterStageInputForeachFunc  func,
                                gpointer                      user_data)
{
  return CLUTTER_BACKEND_GET_CLASS (backend)->foreach_sprite (backend,
                                                              stage,
                                                              func,
                                                              user_data);
}

/**
 * clutter_backend_get_key_focus:
 * @backend: a #ClutterBackend
 * @stage: a #ClutterStage
 *
 * Returns the key focus for stage
 *
 * Returns: (transfer none): the #ClutterKeyFocus representing key focus
 **/
ClutterKeyFocus *
clutter_backend_get_key_focus (ClutterBackend *backend,
                               ClutterStage   *stage)
{
  return CLUTTER_BACKEND_GET_CLASS (backend)->get_key_focus (backend, stage);
}

void
clutter_backend_destroy (ClutterBackend *backend)
{
  g_object_run_dispose (G_OBJECT (backend));
  g_object_unref (backend);
}
