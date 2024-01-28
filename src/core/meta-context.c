/*
 * Copyright (C) 2019 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "core/meta-context-private.h"

#include <locale.h>
#include <sys/resource.h>

#include "backends/meta-backend-private.h"
#include "compositor/meta-plugin-manager.h"
#include "core/display-private.h"
#include "core/meta-service-channel.h"
#include "core/prefs-private.h"
#include "core/util-private.h"

#ifdef HAVE_PROFILER
#include "core/meta-profiler.h"
#endif

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland.h"
#endif

enum
{
  PROP_0,

  PROP_NAME,
  PROP_UNSAFE_MODE,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  STARTED,
  PREPARE_SHUTDOWN,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef enum _MetaContextState
{
  META_CONTEXT_STATE_INIT,
  META_CONTEXT_STATE_CONFIGURED,
  META_CONTEXT_STATE_SETUP,
  META_CONTEXT_STATE_STARTED,
  META_CONTEXT_STATE_RUNNING,
  META_CONTEXT_STATE_TERMINATED,
} MetaContextState;

typedef struct _MetaContextPrivate
{
  char *name;
  char *plugin_name;
  GType plugin_gtype;
  char *gnome_wm_keybindings;

  gboolean unsafe_mode;

  MetaContextState state;

  GOptionContext *option_context;

  MetaBackend *backend;
  MetaDisplay *display;
#ifdef HAVE_WAYLAND
  MetaWaylandCompositor *wayland_compositor;
#endif

  GMainLoop *main_loop;
  GError *termination_error;
#ifdef RLIMIT_NOFILE
  struct rlimit saved_rlimit_nofile;
#endif

#ifdef HAVE_PROFILER
  char *trace_file;
  MetaProfiler *profiler;
#endif

#ifdef HAVE_WAYLAND
  MetaServiceChannel *service_channel;
#endif

  MetaDebugControl *debug_control;
} MetaContextPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaContext, meta_context, G_TYPE_OBJECT)

/**
 * meta_context_add_option_entries:
 * @context: a #MetaContext
 * @entries: (array zero-terminated=1): a %NULL-terminated array of #GOptionEntrys
 * @translation_domain: (nullable): a translation domain to use, or %NULL
 *
 * See g_option_context_add_main_entries() for more details.
 **/
void
meta_context_add_option_entries (MetaContext        *context,
                                 const GOptionEntry *entries,
                                 const char         *translation_domain)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_warn_if_fail (priv->state == META_CONTEXT_STATE_INIT);

  g_option_context_add_main_entries (priv->option_context,
                                     entries,
                                     translation_domain);
}

/**
 * meta_context_add_option_group:
 * @context: a #MetaContext
 * @group: (transfer full): the group to add
 *
 * See g_option_context_add_group() for more details.
 **/
void
meta_context_add_option_group (MetaContext  *context,
                               GOptionGroup *group)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_warn_if_fail (priv->state == META_CONTEXT_STATE_INIT);
  g_return_if_fail (priv->option_context);

  g_option_context_add_group (priv->option_context, group);
}

void
meta_context_set_plugin_gtype (MetaContext *context,
                               GType        plugin_gtype)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_return_if_fail (priv->state <= META_CONTEXT_STATE_CONFIGURED);
  g_return_if_fail (!priv->plugin_name);

  priv->plugin_gtype = plugin_gtype;
}

void
meta_context_set_plugin_name (MetaContext *context,
                              const char  *plugin_name)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_return_if_fail (priv->state <= META_CONTEXT_STATE_CONFIGURED);
  g_return_if_fail (priv->plugin_gtype == G_TYPE_NONE);

  priv->plugin_name = g_strdup (plugin_name);
}

void
meta_context_set_gnome_wm_keybindings (MetaContext *context,
                                       const char  *wm_keybindings)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_return_if_fail (priv->state <= META_CONTEXT_STATE_CONFIGURED);

  g_clear_pointer (&priv->gnome_wm_keybindings, g_free);
  priv->gnome_wm_keybindings = g_strdup (wm_keybindings);
}

const char *
meta_context_get_gnome_wm_keybindings (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  return priv->gnome_wm_keybindings;
}

void
meta_context_notify_ready (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_return_if_fail (priv->state == META_CONTEXT_STATE_STARTED ||
                    priv->state == META_CONTEXT_STATE_RUNNING);

  if (META_CONTEXT_GET_CLASS (context)->notify_ready)
    META_CONTEXT_GET_CLASS (context)->notify_ready (context);
}

const char *
meta_context_get_name (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  return priv->name;
}

/**
 * meta_context_get_backend:
 * @context: The #MetaContext
 *
 * Returns: (transfer none): the #MetaBackend
 */
MetaBackend *
meta_context_get_backend (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  return priv->backend;
}

/**
 * meta_context_get_display:
 * @context: The #MetaContext
 *
 * Returns: (transfer none): the #MetaDisplay
 */
MetaDisplay *
meta_context_get_display (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  return priv->display;
}

#ifdef HAVE_WAYLAND
/**
 * meta_context_get_wayland_compositor:
 * @context: The #MetaContext
 *
 * Get the #MetaWaylandCompositor associated with the MetaContext. The might be
 * none currently associated if the context hasn't been started or if the
 * requested compositor type is not %META_COMPOSITOR_TYPE_WAYLAND.
 *
 * Returns: (transfer none) (nullable): the #MetaWaylandCompositor
 */
MetaWaylandCompositor *
meta_context_get_wayland_compositor (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  return priv->wayland_compositor;
}

MetaServiceChannel *
meta_context_get_service_channel (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  return priv->service_channel;
}
#endif

MetaCompositorType
meta_context_get_compositor_type (MetaContext *context)
{
  return META_CONTEXT_GET_CLASS (context)->get_compositor_type (context);
}

gboolean
meta_context_is_replacing (MetaContext *context)
{
  return META_CONTEXT_GET_CLASS (context)->is_replacing (context);
}

MetaX11DisplayPolicy
meta_context_get_x11_display_policy (MetaContext *context)
{
  return META_CONTEXT_GET_CLASS (context)->get_x11_display_policy (context);
}

#ifdef HAVE_X11
gboolean
meta_context_is_x11_sync (MetaContext *context)
{
  return META_CONTEXT_GET_CLASS (context)->is_x11_sync (context);
}
#endif

#ifdef HAVE_PROFILER
MetaProfiler *
meta_context_get_profiler (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  return priv->profiler;
}

void
meta_context_set_trace_file (MetaContext *context,
                             const char  *trace_file)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  priv->trace_file = g_strdup (trace_file);
}
#endif

static gboolean
meta_context_real_configure (MetaContext   *context,
                             int           *argc,
                             char        ***argv,
                             GError       **error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);
  g_autoptr (GOptionContext) option_context = NULL;

  if (!priv->option_context)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Tried to configure multiple times");
      return FALSE;
    }

  option_context = g_steal_pointer (&priv->option_context);
  if (!g_option_context_parse (option_context, argc, argv, error))
    return FALSE;

  priv->debug_control = g_object_new (META_TYPE_DEBUG_CONTROL,
                                      "context", context,
                                      NULL);
  return TRUE;
}

/**
 * meta_context_configure:
 * @context: a #MetaContext
 * @argc: (inout): Address of the `argc` parameter of your main() function (or 0
 * if @argv is %NULL).
 * @argv: (array length=argc) (inout) (nullable): Address of the`argv` parameter
 * of main(), or %NULL.
 * @error: a return location for errors
 *
 * Returns: %TRUE if the commandline arguments (if any) were valid and if the
 * configuration has been successful, %FALSE otherwise
 */
gboolean
meta_context_configure (MetaContext   *context,
                        int           *argc,
                        char        ***argv,
                        GError       **error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);
  MetaCompositorType compositor_type;

  g_warn_if_fail (priv->state == META_CONTEXT_STATE_INIT);

  if (!META_CONTEXT_GET_CLASS (context)->configure (context, argc, argv, error))
    {
      priv->state = META_CONTEXT_STATE_TERMINATED;
      return FALSE;
    }

#ifdef HAVE_PROFILER
  priv->profiler = meta_profiler_new (priv->trace_file);
#endif

  compositor_type = meta_context_get_compositor_type (context);
  switch (compositor_type)
    {
    case META_COMPOSITOR_TYPE_WAYLAND:
      meta_set_is_wayland_compositor (TRUE);
      break;
    case META_COMPOSITOR_TYPE_X11:
      meta_set_is_wayland_compositor (FALSE);
      break;
    }

  priv->state = META_CONTEXT_STATE_CONFIGURED;

  return TRUE;
}

static const char *
compositor_type_to_description (MetaCompositorType compositor_type)
{
  switch (compositor_type)
    {
    case META_COMPOSITOR_TYPE_WAYLAND:
      return "Wayland display server";
    case META_COMPOSITOR_TYPE_X11:
      return "X11 window and compositing manager";
    }

  g_assert_not_reached ();
}

static void
init_introspection (MetaContext *context)
{
#ifdef HAVE_INTROSPECTION
  g_irepository_prepend_search_path (MUTTER_PKGLIBDIR);
#endif
}

static gboolean
meta_context_real_setup (MetaContext  *context,
                         GError      **error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);
  MetaBackend *backend;

  backend = META_CONTEXT_GET_CLASS (context)->create_backend (context, error);
  if (!backend)
    return FALSE;

  priv->backend = backend;
  return TRUE;
}

gboolean
meta_context_setup (MetaContext  *context,
                    GError      **error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);
  MetaCompositorType compositor_type;

  g_warn_if_fail (priv->state == META_CONTEXT_STATE_CONFIGURED);

  if (!priv->plugin_name && priv->plugin_gtype == G_TYPE_NONE)
    {
      priv->state = META_CONTEXT_STATE_TERMINATED;
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No compositor plugin set");
      return FALSE;
    }

  meta_init_debug_utils ();

  compositor_type = meta_context_get_compositor_type (context);
  g_message ("Running %s (using mutter %s) as a %s",
             priv->name, VERSION,
             compositor_type_to_description (compositor_type));

  if (priv->plugin_name)
    meta_plugin_manager_load (priv->plugin_name);
  else
    meta_plugin_manager_set_plugin_type (priv->plugin_gtype);

  init_introspection (context);

  if (!META_CONTEXT_GET_CLASS (context)->setup (context, error))
    {
      priv->state = META_CONTEXT_STATE_TERMINATED;
      return FALSE;
    }

  priv->state = META_CONTEXT_STATE_SETUP;
  return TRUE;
}

gboolean
meta_context_start (MetaContext  *context,
                    GError      **error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_warn_if_fail (priv->state == META_CONTEXT_STATE_SETUP);

  meta_prefs_init ();

#ifdef HAVE_WAYLAND
  if (meta_context_get_compositor_type (context) ==
      META_COMPOSITOR_TYPE_WAYLAND)
    priv->wayland_compositor = meta_wayland_compositor_new (context);
#endif

  priv->display = meta_display_new (context, error);
  if (!priv->display)
    {
      priv->state = META_CONTEXT_STATE_TERMINATED;
      return FALSE;
    }

#ifdef HAVE_WAYLAND
  priv->service_channel = meta_service_channel_new (context);
#endif

  priv->main_loop = g_main_loop_new (NULL, FALSE);

  priv->state = META_CONTEXT_STATE_STARTED;

  g_signal_emit (context, signals[STARTED], 0);

  return TRUE;
}

gboolean
meta_context_run_main_loop (MetaContext  *context,
                            GError      **error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_warn_if_fail (priv->state == META_CONTEXT_STATE_STARTED);
  if (!priv->main_loop)
    {
      priv->state = META_CONTEXT_STATE_TERMINATED;
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Tried to run main loop without having started");
      return FALSE;
    }

  priv->state = META_CONTEXT_STATE_RUNNING;
  g_main_loop_run (priv->main_loop);
  priv->state = META_CONTEXT_STATE_TERMINATED;
  g_clear_pointer (&priv->main_loop, g_main_loop_unref);

  if (priv->termination_error)
    {
      g_propagate_error (error, g_steal_pointer (&priv->termination_error));
      return FALSE;
    }

  return TRUE;
}

void
meta_context_terminate (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_warn_if_fail (priv->state == META_CONTEXT_STATE_RUNNING);
  g_warn_if_fail (g_main_loop_is_running (priv->main_loop));

  g_main_loop_quit (priv->main_loop);
}

void
meta_context_terminate_with_error (MetaContext *context,
                                   GError      *error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  priv->termination_error = g_steal_pointer (&error);
  meta_context_terminate (context);
}

void
meta_context_destroy (MetaContext *context)
{
  g_object_run_dispose (G_OBJECT (context));
  g_object_unref (context);
}

void
meta_context_set_unsafe_mode (MetaContext *context,
                              gboolean     enable)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  if (priv->unsafe_mode == enable)
    return;

  priv->unsafe_mode = enable;
  g_object_notify_by_pspec (G_OBJECT (context), obj_props[PROP_UNSAFE_MODE]);
}

static gboolean
meta_context_save_rlimit_nofile (MetaContext  *context,
                                 GError      **error)
{
#ifdef RLIMIT_NOFILE
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  if (getrlimit (RLIMIT_NOFILE, &priv->saved_rlimit_nofile) != 0)
    {
      priv->saved_rlimit_nofile.rlim_cur = 0;
      priv->saved_rlimit_nofile.rlim_max = 0;
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "getrlimit failed: %s", g_strerror (errno));
      return FALSE;
    }

  return TRUE;
#else
  g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOSYS,
               "Missing support for RLIMIT_NOFILE");

  return FALSE;
#endif
}

/**
 * meta_context_raise_rlimit_nofile:
 * @context: a #MetaContext
 * @error: a return location for errors
 *
 * Raises the RLIMIT_NOFILE limit value to the hard limit.
 */
gboolean
meta_context_raise_rlimit_nofile (MetaContext  *context,
                                  GError      **error)
{
#ifdef RLIMIT_NOFILE
  struct rlimit new_rlimit;

  if (getrlimit (RLIMIT_NOFILE, &new_rlimit) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "getrlimit failed: %s", g_strerror (errno));
      return FALSE;
    }

  /* Raise the rlimit_nofile value to the hard limit */
  new_rlimit.rlim_cur = new_rlimit.rlim_max;

  if (setrlimit (RLIMIT_NOFILE, &new_rlimit) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "setrlimit failed: %s", g_strerror (errno));
      return FALSE;
    }

  return TRUE;
#else
  g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOSYS,
               "Missing support for RLIMIT_NOFILE");

  return FALSE;
#endif
}

/**
 * meta_context_restore_rlimit_nofile:
 * @context: a #MetaContext
 * @error: a return location for errors
 *
 * Restores the RLIMIT_NOFILE limits from when the #MetaContext was created.
 */
gboolean
meta_context_restore_rlimit_nofile (MetaContext  *context,
                                    GError      **error)
{
#ifdef RLIMIT_NOFILE
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  if (priv->saved_rlimit_nofile.rlim_cur == 0)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT,
                   "RLIMIT_NOFILE not saved");
      return FALSE;
    }

  if (setrlimit (RLIMIT_NOFILE, &priv->saved_rlimit_nofile) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "setrlimit failed: %s", g_strerror (errno));
      return FALSE;
    }

  return TRUE;
#else
  g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOSYS,
               "Missing support for RLIMIT_NOFILE");

  return FALSE;
#endif
}

static void
meta_context_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  MetaContext *context = META_CONTEXT (object);
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_UNSAFE_MODE:
      g_value_set_boolean (value, priv->unsafe_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_context_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  MetaContext *context = META_CONTEXT (object);
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  switch (prop_id)
    {
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;
    case PROP_UNSAFE_MODE:
      meta_context_set_unsafe_mode (META_CONTEXT (object),
                                    g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_context_dispose (GObject *object)
{
  MetaContext *context = META_CONTEXT (object);
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_signal_emit (context, signals[PREPARE_SHUTDOWN], 0);

#ifdef HAVE_WAYLAND
  g_clear_object (&priv->service_channel);

  if (priv->wayland_compositor)
    meta_wayland_compositor_prepare_shutdown (priv->wayland_compositor);
#endif

  if (priv->display)
    meta_display_close (priv->display, META_CURRENT_TIME);
  g_clear_object (&priv->display);

#ifdef HAVE_WAYLAND
  g_clear_object (&priv->wayland_compositor);
#endif

  g_clear_pointer (&priv->backend, meta_backend_destroy);

  g_clear_object (&priv->debug_control);

  g_clear_pointer (&priv->option_context, g_option_context_free);
  g_clear_pointer (&priv->main_loop, g_main_loop_unref);

  G_OBJECT_CLASS (meta_context_parent_class)->dispose (object);
}

static void
meta_context_finalize (GObject *object)
{
  MetaContext *context = META_CONTEXT (object);
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

#ifdef HAVE_PROFILER
  g_clear_object (&priv->profiler);
  g_clear_pointer (&priv->trace_file, g_free);
#endif

  g_clear_pointer (&priv->gnome_wm_keybindings, g_free);
  g_clear_pointer (&priv->plugin_name, g_free);
  g_clear_pointer (&priv->name, g_free);

  G_OBJECT_CLASS (meta_context_parent_class)->finalize (object);
}

static void
meta_context_class_init (MetaContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_context_get_property;
  object_class->set_property = meta_context_set_property;
  object_class->dispose = meta_context_dispose;
  object_class->finalize = meta_context_finalize;

  klass->configure = meta_context_real_configure;
  klass->setup = meta_context_real_setup;

  obj_props[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_UNSAFE_MODE] =
    g_param_spec_boolean ("unsafe-mode", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[STARTED] =
    g_signal_new ("started",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[PREPARE_SHUTDOWN] =
    g_signal_new ("prepare-shutdown",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_context_init (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);
  g_autoptr (GError) error = NULL;

  priv->plugin_gtype = G_TYPE_NONE;
  priv->gnome_wm_keybindings = g_strdup ("Mutter");

  if (!setlocale (LC_ALL, ""))
    g_warning ("Locale not understood by C library");
  bindtextdomain (GETTEXT_PACKAGE, MUTTER_LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

  priv->option_context = g_option_context_new (NULL);
  g_option_context_set_main_group (priv->option_context,
                                   g_option_group_new (NULL, NULL, NULL,
                                                       context, NULL));

  if (!meta_context_save_rlimit_nofile (context, &error))
    {
      if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOSYS))
        g_warning ("Failed to save the nofile limit: %s", error->message);
    }
}

MetaDebugControl *
meta_context_get_debug_control (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  return priv->debug_control;
}
