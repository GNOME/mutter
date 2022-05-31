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

#include "clutter-build-config.h"

#include "clutter-backend-private.h"
#include "clutter-debug.h"
#include "clutter-event-private.h"
#include "clutter-marshal.h"
#include "clutter-mutter.h"
#include "clutter-private.h"
#include "clutter-stage-manager-private.h"
#include "clutter-stage-private.h"
#include "clutter-stage-window.h"

#include <cogl/cogl.h>

#define DEFAULT_FONT_NAME       "Sans 10"

enum
{
  RESOLUTION_CHANGED,
  FONT_CHANGED,
  SETTINGS_CHANGED,

  LAST_SIGNAL
};

G_DEFINE_ABSTRACT_TYPE (ClutterBackend, clutter_backend, G_TYPE_OBJECT)

static guint backend_signals[LAST_SIGNAL] = { 0, };

static void
clutter_backend_dispose (GObject *gobject)
{
  ClutterBackend *backend = CLUTTER_BACKEND (gobject);

  /* clear the events still in the queue of the main context */
  _clutter_clear_events_queue ();

  g_clear_object (&backend->dummy_onscreen);
  if (backend->stage_window)
    {
      g_object_remove_weak_pointer (G_OBJECT (backend->stage_window),
                                    (gpointer *) &backend->stage_window);
      backend->stage_window = NULL;
    }

  g_clear_pointer (&backend->cogl_source, g_source_destroy);
  g_clear_pointer (&backend->font_name, g_free);
  g_clear_pointer (&backend->font_options, cairo_font_options_destroy);
  g_clear_object (&backend->input_method);

  G_OBJECT_CLASS (clutter_backend_parent_class)->dispose (gobject);
}

static gfloat
get_units_per_em (ClutterBackend       *backend,
                  PangoFontDescription *font_desc)
{
  gfloat units_per_em = -1.0;
  gboolean free_font_desc = FALSE;
  gdouble dpi;

  dpi = clutter_backend_get_resolution (backend);

  if (font_desc == NULL)
    {
      ClutterSettings *settings;
      gchar *font_name = NULL;

      settings = clutter_settings_get_default ();
      g_object_get (settings, "font-name", &font_name, NULL);

      if (G_LIKELY (font_name != NULL && *font_name != '\0'))
        {
          font_desc = pango_font_description_from_string (font_name);
          free_font_desc = TRUE;

          g_free (font_name);
        }
    }

  if (font_desc != NULL)
    {
      gdouble font_size = 0;
      gint pango_size;
      gboolean is_absolute;

      pango_size = pango_font_description_get_size (font_desc);
      is_absolute = pango_font_description_get_size_is_absolute (font_desc);

      /* "absolute" means "device units" (usually, pixels); otherwise,
       * it means logical units (points)
       */
      if (is_absolute)
        font_size = (gdouble) pango_size / PANGO_SCALE;
      else
        font_size = dpi * ((gdouble) pango_size / PANGO_SCALE) / 72.0f;

      /* 10 points at 96 DPI is 13.3 pixels */
      units_per_em = (1.2f * font_size) * dpi / 96.0f;
    }
  else
    units_per_em = -1.0f;

  if (free_font_desc)
    pango_font_description_free (font_desc);

  return units_per_em;
}

static void
clutter_backend_real_resolution_changed (ClutterBackend *backend)
{
  ClutterMainContext *context;
  ClutterSettings *settings;
  gdouble resolution;
  gint dpi;

  settings = clutter_settings_get_default ();
  g_object_get (settings, "font-dpi", &dpi, NULL);

  if (dpi < 0)
    resolution = 96.0;
  else
    resolution = dpi / 1024.0;

  context = _clutter_context_get_default ();
  if (context->font_map != NULL)
    cogl_pango_font_map_set_resolution (context->font_map, resolution);

  backend->units_per_em = get_units_per_em (backend, NULL);
  backend->units_serial += 1;

  CLUTTER_NOTE (BACKEND, "Units per em: %.2f", backend->units_per_em);
}

static void
clutter_backend_real_font_changed (ClutterBackend *backend)
{
  backend->units_per_em = get_units_per_em (backend, NULL);
  backend->units_serial += 1;

  CLUTTER_NOTE (BACKEND, "Units per em: %.2f", backend->units_per_em);
}

static gboolean
clutter_backend_do_real_create_context (ClutterBackend  *backend,
                                        CoglDriver       driver_id,
                                        GError         **error)
{
  ClutterBackendClass *klass;
  CoglSwapChain *swap_chain;

  klass = CLUTTER_BACKEND_GET_CLASS (backend);

  swap_chain = NULL;

  CLUTTER_NOTE (BACKEND, "Creating Cogl renderer");
  backend->cogl_renderer = klass->get_renderer (backend, error);

  if (backend->cogl_renderer == NULL)
    goto error;

  CLUTTER_NOTE (BACKEND, "Connecting the renderer");
  cogl_renderer_set_driver (backend->cogl_renderer, driver_id);
  if (!cogl_renderer_connect (backend->cogl_renderer, error))
    goto error;

  CLUTTER_NOTE (BACKEND, "Creating Cogl swap chain");
  swap_chain = cogl_swap_chain_new ();

  CLUTTER_NOTE (BACKEND, "Creating Cogl display");
  if (klass->get_display != NULL)
    {
      backend->cogl_display = klass->get_display (backend,
                                                  backend->cogl_renderer,
                                                  swap_chain,
                                                  error);
    }
  else
    {
      CoglOnscreenTemplate *tmpl;
      gboolean res;

      tmpl = cogl_onscreen_template_new (swap_chain);

      /* XXX: I have some doubts that this is a good design.
       *
       * Conceptually should we be able to check an onscreen_template
       * without more details about the CoglDisplay configuration?
       */
      res = cogl_renderer_check_onscreen_template (backend->cogl_renderer,
                                                   tmpl,
                                                   error);

      if (!res)
        goto error;

      backend->cogl_display = cogl_display_new (backend->cogl_renderer, tmpl);

      /* the display owns the template */
      cogl_object_unref (tmpl);
    }

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
  cogl_object_unref (backend->cogl_renderer);
  cogl_object_unref (swap_chain);

  return TRUE;

error:
  if (backend->cogl_display != NULL)
    {
      cogl_object_unref (backend->cogl_display);
      backend->cogl_display = NULL;
    }

  if (backend->cogl_renderer != NULL)
    {
      cogl_object_unref (backend->cogl_renderer);
      backend->cogl_renderer = NULL;
    }

  if (swap_chain != NULL)
    cogl_object_unref (swap_chain);

  return FALSE;
}

static const struct {
  const char *driver_name;
  const char *driver_desc;
  CoglDriver driver_id;
} all_known_drivers[] = {
  { "gl3", "OpenGL 3.2 core profile", COGL_DRIVER_GL3 },
  { "gl", "OpenGL legacy profile", COGL_DRIVER_GL },
  { "gles2", "OpenGL ES 2.0", COGL_DRIVER_GLES2 },
  { "any", "Default Cogl driver", COGL_DRIVER_ANY },
};

static const char *allowed_drivers;

static gboolean
clutter_backend_real_create_context (ClutterBackend  *backend,
                                     GError         **error)
{
  GError *internal_error = NULL;
  const char *drivers_list;
  char **known_drivers;
  gboolean allow_any;
  int i;

  if (backend->cogl_context != NULL)
    return TRUE;

  if (allowed_drivers == NULL)
    allowed_drivers = CLUTTER_DRIVERS;

  allow_any = strstr (allowed_drivers, "*") != NULL;

  drivers_list = g_getenv ("CLUTTER_DRIVER");
  if (drivers_list == NULL)
    drivers_list = allowed_drivers;

  known_drivers = g_strsplit (drivers_list, ",", 0);

  for (i = 0; backend->cogl_context == NULL && known_drivers[i] != NULL; i++)
    {
      const char *driver_name = known_drivers[i];
      gboolean is_any = g_str_equal (driver_name, "*");
      int j;

      for (j = 0; j < G_N_ELEMENTS (all_known_drivers); j++)
        {
          if (!allow_any && !is_any && !strstr (driver_name, all_known_drivers[j].driver_name))
            continue;

          if ((allow_any && is_any) ||
              (is_any && strstr (allowed_drivers, all_known_drivers[j].driver_name)) ||
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

  backend->cogl_source = cogl_glib_source_new (backend->cogl_context, G_PRIORITY_DEFAULT);
  g_source_attach (backend->cogl_source, NULL);

  return TRUE;
}

static void
clutter_backend_class_init (ClutterBackendClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = clutter_backend_dispose;

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
                  G_STRUCT_OFFSET (ClutterBackendClass, font_changed),
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
                  G_STRUCT_OFFSET (ClutterBackendClass, settings_changed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  klass->resolution_changed = clutter_backend_real_resolution_changed;
  klass->font_changed = clutter_backend_real_font_changed;

  klass->create_context = clutter_backend_real_create_context;
}

static void
clutter_backend_init (ClutterBackend *self)
{
  self->units_per_em = -1.0;
  self->units_serial = 1;

  self->dummy_onscreen = NULL;

  self->fallback_resource_scale = 1.f;
}

gboolean
_clutter_backend_finish_init (ClutterBackend  *backend,
                              GError         **error)
{
  ClutterBackendClass *klass;

  g_assert (CLUTTER_IS_BACKEND (backend));

  klass = CLUTTER_BACKEND_GET_CLASS (backend);
  if (klass->finish_init)
    return klass->finish_init (backend, error);

  return TRUE;
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

gfloat
_clutter_backend_get_units_per_em (ClutterBackend       *backend,
                                   PangoFontDescription *font_desc)
{
  /* recompute for the font description, but do not cache the result */
  if (font_desc != NULL)
    return get_units_per_em (backend, font_desc);

  if (backend->units_per_em < 0)
    backend->units_per_em = get_units_per_em (backend, NULL);

  return backend->units_per_em;
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
  ClutterMainContext *clutter_context;

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

  settings = clutter_settings_get_default ();
  g_object_get (settings, "font-dpi", &resolution, NULL);

  if (resolution < 0)
    return 96.0;

  return resolution / 1024.0;
}

/**
 * clutter_backend_set_font_options:
 * @backend: a #ClutterBackend
 * @options: Cairo font options for the backend, or %NULL
 *
 * Sets the new font options for @backend. The #ClutterBackend will
 * copy the #cairo_font_options_t.
 *
 * If @options is %NULL, the first following call to
 * clutter_backend_get_font_options() will return the default font
 * options for @backend.
 *
 * This function is intended for actors creating a Pango layout
 * using the PangoCairo API.
 */
void
clutter_backend_set_font_options (ClutterBackend             *backend,
                                  const cairo_font_options_t *options)
{
  g_return_if_fail (CLUTTER_IS_BACKEND (backend));

  if (backend->font_options != options)
    {
      if (backend->font_options)
        cairo_font_options_destroy (backend->font_options);

      if (options)
        backend->font_options = cairo_font_options_copy (options);
      else
        backend->font_options = NULL;

      g_signal_emit (backend, backend_signals[FONT_CHANGED], 0);
    }
}

/**
 * clutter_backend_get_font_options:
 * @backend: a #ClutterBackend
 *
 * Retrieves the font options for @backend.
 *
 * Return value: (transfer none): the font options of the #ClutterBackend.
 *   The returned #cairo_font_options_t is owned by the backend and should
 *   not be modified or freed
 */
const cairo_font_options_t *
clutter_backend_get_font_options (ClutterBackend *backend)
{
  g_return_val_if_fail (CLUTTER_IS_BACKEND (backend), NULL);

  if (G_LIKELY (backend->font_options))
    return backend->font_options;

  backend->font_options = cairo_font_options_create ();

  cairo_font_options_set_hint_style (backend->font_options, CAIRO_HINT_STYLE_NONE);
  cairo_font_options_set_subpixel_order (backend->font_options, CAIRO_SUBPIXEL_ORDER_DEFAULT);
  cairo_font_options_set_antialias (backend->font_options, CAIRO_ANTIALIAS_DEFAULT);

  g_signal_emit (backend, backend_signals[FONT_CHANGED], 0);

  return backend->font_options;
}

gint32
_clutter_backend_get_units_serial (ClutterBackend *backend)
{
  return backend->units_serial;
}

gboolean
_clutter_backend_translate_event (ClutterBackend *backend,
                                  gpointer        native,
                                  ClutterEvent   *event)
{
  return CLUTTER_BACKEND_GET_CLASS (backend)->translate_event (backend,
                                                               native,
                                                               event);
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
 * Stability: unstable
 */
CoglContext *
clutter_backend_get_cogl_context (ClutterBackend *backend)
{
  return backend->cogl_context;
}

void
clutter_set_allowed_drivers (const char *drivers)
{
  if (_clutter_context_is_initialized ())
    {
      g_warning ("Clutter has already been initialized.\n");
      return;
    }

  allowed_drivers = g_strdup (drivers);
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
 * @method: the input method
 *
 * Sets the input method to be used by Clutter
 **/
void
clutter_backend_set_input_method (ClutterBackend     *backend,
                                  ClutterInputMethod *method)
{
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

void
clutter_backend_destroy (ClutterBackend *backend)
{
  g_object_run_dispose (G_OBJECT (backend));
  g_object_unref (backend);
}
