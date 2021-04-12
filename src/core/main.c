/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter main() */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2006 Elijah Newren
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
 */

/**
 * SECTION:main
 * @title: Main
 * @short_description: Program startup.
 *
 * Functions which parse the command-line arguments, create the display,
 * kick everything off and then close down Mutter when it's time to go.
 *
 *
 *
 * Mutter - a boring window manager for the adult in you
 *
 * Many window managers are like Marshmallow Froot Loops; Mutter
 * is like Frosted Flakes: it's still plain old corn, but dusted
 * with some sugar.
 *
 * The best way to get a handle on how the whole system fits together
 * is discussed in doc/code-overview.txt; if you're looking for functions
 * to investigate, read main(), meta_display_open(), and event_callback().
 */

#define _XOPEN_SOURCE /* for putenv() and some signal-related functions */

#include "config.h"

#include "meta/main.h"

#include <errno.h>
#include <fcntl.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <gobject/gvaluecollector.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <unistd.h>

#ifdef HAVE_INTROSPECTION
#include <girepository.h>
#endif

#if defined(HAVE_NATIVE_BACKEND) && defined(HAVE_WAYLAND)
#include <systemd/sd-login.h>
#endif /* HAVE_WAYLAND && HAVE_NATIVE_BACKEND */

#ifdef HAVE_SYS_PRCTL
#include <sys/prctl.h>
#endif

#include "backends/meta-backend-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-virtual-monitor.h"
#include "backends/x11/cm/meta-backend-x11-cm.h"
#include "backends/x11/meta-backend-x11.h"
#include "clutter/clutter.h"
#include "core/display-private.h"
#include "core/main-private.h"
#include "core/util-private.h"
#include "meta/compositor.h"
#include "meta/meta-backend.h"
#include "meta/meta-x11-errors.h"
#include "meta/prefs.h"
#include "ui/ui.h"
#include "x11/session.h"

#ifdef HAVE_WAYLAND
#include "backends/x11/nested/meta-backend-x11-nested.h"
#include "wayland/meta-wayland.h"
#include "wayland/meta-xwayland.h"
#endif

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#endif

static const GDebugKey meta_debug_keys[] = {
  { "focus", META_DEBUG_FOCUS },
  { "workarea", META_DEBUG_WORKAREA },
  { "stack", META_DEBUG_STACK },
  { "sm", META_DEBUG_SM },
  { "events", META_DEBUG_EVENTS },
  { "window-state", META_DEBUG_WINDOW_STATE },
  { "window-ops", META_DEBUG_WINDOW_OPS },
  { "geometry", META_DEBUG_GEOMETRY },
  { "placement", META_DEBUG_PLACEMENT },
  { "ping", META_DEBUG_PING },
  { "keybindings", META_DEBUG_KEYBINDINGS },
  { "sync", META_DEBUG_SYNC },
  { "startup", META_DEBUG_STARTUP },
  { "prefs", META_DEBUG_PREFS },
  { "groups", META_DEBUG_GROUPS },
  { "resizing", META_DEBUG_RESIZING },
  { "shapes", META_DEBUG_SHAPES },
  { "edge-resistance", META_DEBUG_EDGE_RESISTANCE },
  { "dbus", META_DEBUG_DBUS },
  { "input", META_DEBUG_INPUT },
  { "wayland", META_DEBUG_WAYLAND },
  { "kms", META_DEBUG_KMS },
  { "screen-cast", META_DEBUG_SCREEN_CAST },
  { "remote-desktop", META_DEBUG_REMOTE_DESKTOP },
};

/*
 * The exit code we'll return to our parent process when we eventually die.
 */
static MetaExitCode meta_exit_code = META_EXIT_SUCCESS;

/*
 * Handle on the main loop, so that we have an easy way of shutting Mutter
 * down.
 */
static GMainLoop *meta_main_loop = NULL;

static void prefs_changed_callback (MetaPreference pref,
                                    gpointer       data);

#ifdef HAVE_NATIVE_BACKEND
static void release_virtual_monitors (void);
#endif

/**
 * meta_print_compilation_info:
 *
 * Prints a list of which configure script options were used to
 * build this copy of Mutter. This is actually always called
 * on startup, but it's all no-op unless we're in verbose mode
 * (see meta_set_verbose()).
 */
static void
meta_print_compilation_info (void)
{
#ifdef HAVE_STARTUP_NOTIFICATION
  meta_verbose ("Compiled with startup notification");
#else
  meta_verbose ("Compiled without startup notification");
#endif
}

/**
 * meta_print_self_identity:
 *
 * Prints the version number, the current timestamp (not the
 * build date), the locale, the character encoding, and a list
 * of configure script options that were used to build this
 * copy of Mutter. This is actually always called
 * on startup, but it's all no-op unless we're in verbose mode
 * (see meta_set_verbose()).
 */
static void
meta_print_self_identity (void)
{
  char buf[256];
  GDate d;
  const char *charset;

  /* Version and current date. */
  g_date_clear (&d, 1);
  g_date_set_time_t (&d, time (NULL));
  g_date_strftime (buf, sizeof (buf), "%x", &d);
  meta_verbose ("Mutter version %s running on %s",
    VERSION, buf);

  /* Locale and encoding. */
  g_get_charset (&charset);
  meta_verbose ("Running in locale \"%s\" with encoding \"%s\"",
    setlocale (LC_ALL, NULL), charset);

  /* Compilation settings. */
  meta_print_compilation_info ();
}

/*
 * The set of possible options that can be set on Mutter's
 * command line.
 */
static gchar    *opt_save_file;
static gchar    *opt_display_name;
static gchar    *opt_client_id;
static gboolean  opt_replace_wm;
static gboolean  opt_disable_sm;
static gboolean  opt_sync;
#ifdef HAVE_WAYLAND
static gboolean  opt_wayland;
static gboolean  opt_nested;
static gboolean  opt_no_x11;
static char     *opt_wayland_display;
#endif
#ifdef HAVE_NATIVE_BACKEND
static gboolean  opt_display_server;
static gboolean  opt_headless;
#endif
static gboolean  opt_x11;

#ifdef HAVE_NATIVE_BACKEND
static gboolean opt_virtual_monitor_cb (const char  *option_name,
                                        const char  *value,
                                        gpointer     data,
                                        GError     **error);
#endif

static GOptionEntry meta_options[] = {
  {
    "sm-disable", 0, 0, G_OPTION_ARG_NONE,
    &opt_disable_sm,
    N_("Disable connection to session manager"),
    NULL
  },
  {
    "replace", 'r', 0, G_OPTION_ARG_NONE,
    &opt_replace_wm,
    N_("Replace the running window manager"),
    NULL
  },
  {
    "sm-client-id", 0, 0, G_OPTION_ARG_STRING,
    &opt_client_id,
    N_("Specify session management ID"),
    "ID"
  },
  {
    "display", 'd', 0, G_OPTION_ARG_STRING,
    &opt_display_name, N_("X Display to use"),
    "DISPLAY"
  },
  {
    "sm-save-file", 0, 0, G_OPTION_ARG_FILENAME,
    &opt_save_file,
    N_("Initialize session from savefile"),
    "FILE"
  },
  {
    "sync", 0, 0, G_OPTION_ARG_NONE,
    &opt_sync,
    N_("Make X calls synchronous"),
    NULL
  },
#ifdef HAVE_WAYLAND
  {
    "wayland", 0, 0, G_OPTION_ARG_NONE,
    &opt_wayland,
    N_("Run as a wayland compositor"),
    NULL
  },
  {
    "nested", 0, 0, G_OPTION_ARG_NONE,
    &opt_nested,
    N_("Run as a nested compositor"),
    NULL
  },
  {
    "no-x11", 0, 0, G_OPTION_ARG_NONE,
    &opt_no_x11,
    N_("Run wayland compositor without starting Xwayland"),
    NULL
  },
  {
    "wayland-display", 0, 0, G_OPTION_ARG_STRING,
    &opt_wayland_display,
    N_("Specify Wayland display name to use"),
    NULL
  },
#endif
#ifdef HAVE_NATIVE_BACKEND
  {
    "display-server", 0, 0, G_OPTION_ARG_NONE,
    &opt_display_server,
    N_("Run as a full display server, rather than nested")
  },
  {
    "headless", 0, 0, G_OPTION_ARG_NONE,
    &opt_headless,
    N_("Run as a headless display server")
  },
  {
    "virtual-monitor", 0, 0, G_OPTION_ARG_CALLBACK,
    &opt_virtual_monitor_cb,
    N_("Add persistent virtual monitor (WxH or WxH@R)")
  },
#endif
  {
    "x11", 0, 0, G_OPTION_ARG_NONE,
    &opt_x11,
    N_("Run with X11 backend")
  },
  {NULL}
};

/**
 * meta_get_option_context: (skip)
 *
 * Returns a #GOptionContext initialized with mutter-related options.
 * Parse the command-line args with this before calling meta_init().
 *
 * Return value: the #GOptionContext
 */
GOptionContext *
meta_get_option_context (void)
{
  GOptionContext *ctx;

  if (setlocale (LC_ALL, "") == NULL)
    meta_warning ("Locale not understood by C library, internationalization will not work");
  bindtextdomain (GETTEXT_PACKAGE, MUTTER_LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

  ctx = g_option_context_new (NULL);
  g_option_context_add_main_entries (ctx, meta_options, GETTEXT_PACKAGE);
  return ctx;
}

/**
 * meta_select_display:
 *
 * Selects which display Mutter should use. It first tries to use
 * @display_name as the display. If @display_name is %NULL then
 * try to use the environment variable MUTTER_DISPLAY. If that
 * also is %NULL, use the default - :0.0
 */
static void
meta_select_display (char *display_arg)
{
  const char *display_name;

  if (display_arg)
    display_name = (const char *) display_arg;
  else
    display_name = g_getenv ("MUTTER_DISPLAY");

  if (display_name)
    g_setenv ("DISPLAY", display_name, TRUE);
}

void
meta_finalize (void)
{
  MetaDisplay *display = meta_get_display ();
  MetaBackend *backend = meta_get_backend ();

  if (backend)
    meta_backend_prepare_shutdown (backend);

#ifdef HAVE_WAYLAND
  if (meta_is_wayland_compositor ())
    meta_wayland_finalize ();
#endif

  if (display)
    meta_display_close (display,
                        META_CURRENT_TIME); /* I doubt correct timestamps matter here */

#ifdef HAVE_NATIVE_BACKEND
  release_virtual_monitors ();
#endif

  meta_release_backend ();
}

static gboolean
on_sigterm (gpointer user_data)
{
  meta_quit (EXIT_SUCCESS);

  return G_SOURCE_REMOVE;
}

#if defined(HAVE_WAYLAND) && defined(HAVE_NATIVE_BACKEND)
static gboolean
session_type_is_supported (const char *session_type)
{
   return (g_strcmp0 (session_type, "x11") == 0) ||
          (g_strcmp0 (session_type, "wayland") == 0);
}

static char *
find_session_type (void)
{
  char **sessions = NULL;
  char *session_id;
  char *session_type;
  const char *session_type_env;
  gboolean is_tty = FALSE;
  int ret, i;

  ret = sd_pid_get_session (0, &session_id);
  if (ret == 0 && session_id != NULL)
    {
      ret = sd_session_get_type (session_id, &session_type);
      free (session_id);

      if (ret == 0)
        {
          if (session_type_is_supported (session_type))
            goto out;
          else
            is_tty = g_strcmp0 (session_type, "tty") == 0;
          free (session_type);
        }
    }
  else if (sd_uid_get_sessions (getuid (), 1, &sessions) > 0)
    {
      for (i = 0; sessions[i] != NULL; i++)
        {
          ret = sd_session_get_type (sessions[i], &session_type);

          if (ret < 0)
            continue;

          if (session_type_is_supported (session_type))
            {
              g_strfreev (sessions);
              goto out;
            }

          free (session_type);
        }
    }
  g_strfreev (sessions);

  session_type_env = g_getenv ("XDG_SESSION_TYPE");
  if (session_type_is_supported (session_type_env))
    {
      /* The string should be freeable */
      session_type = strdup (session_type_env);
      goto out;
    }

  /* Legacy support for starting through xinit */
  if (is_tty && (g_getenv ("MUTTER_DISPLAY") || g_getenv ("DISPLAY")))
    {
      session_type = strdup ("x11");
      goto out;
    }

  meta_warning ("Unsupported session type");
  meta_exit (META_EXIT_ERROR);

out:
  return session_type;
}

static gboolean
check_for_wayland_session_type (void)
{
  char *session_type;
  gboolean is_wayland;

  session_type = find_session_type ();
  is_wayland = g_strcmp0 (session_type, "wayland") == 0;
  free (session_type);

  return is_wayland;
}
#endif

#ifdef HAVE_NATIVE_BACKEND
static GList *opt_virtual_monitor_infos = NULL;

static GList *persistent_virtual_monitors = NULL;

static gboolean
opt_virtual_monitor_cb (const char  *option_name,
                        const char  *value,
                        gpointer     data,
                        GError     **error)
{
  int width, height;
  float refresh_rate = 60.0;

  if (sscanf (value, "%dx%d@%f",
              &width, &height, &refresh_rate) == 3 ||
      sscanf (value, "%dx%d",
              &width, &height) == 2)
    {
      g_autofree char *serial = NULL;
      MetaVirtualMonitorInfo *virtual_monitor;

      serial = g_strdup_printf ("0x%.2x",
                                g_list_length (opt_virtual_monitor_infos));
      virtual_monitor = meta_virtual_monitor_info_new (width,
                                                       height,
                                                       refresh_rate,
                                                       "MetaVendor",
                                                       "MetaVirtualMonitor",
                                                       serial);
      opt_virtual_monitor_infos = g_list_append (opt_virtual_monitor_infos,
                                                 virtual_monitor);
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static void
release_virtual_monitors (void)
{
  g_list_free_full (persistent_virtual_monitors, g_object_unref);
  persistent_virtual_monitors = NULL;
}

static void
add_persistent_virtual_monitors (void)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *l;

  for (l = opt_virtual_monitor_infos; l; l = l->next)
    {
      MetaVirtualMonitorInfo *info = l->data;
      g_autoptr (GError) error = NULL;
      MetaVirtualMonitor *virtual_monitor;

      virtual_monitor =
        meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                     info,
                                                     &error);
      if (!virtual_monitor)
        {
          g_warning ("Failed to add virtual monitor: %s", error->message);
          meta_exit (META_EXIT_ERROR);
        }

      persistent_virtual_monitors = g_list_append (persistent_virtual_monitors,
                                                   virtual_monitor);
    }

  if (opt_virtual_monitor_infos)
    {
      g_list_free_full (opt_virtual_monitor_infos,
                        (GDestroyNotify) meta_virtual_monitor_info_free);
      opt_virtual_monitor_infos = NULL;

      meta_monitor_manager_reload (monitor_manager);
    }
}
#endif

/*
 * Determine the compositor configuration, i.e. whether to run as a Wayland
 * compositor, as well as what backend to use.
 *
 * There are various different flags affecting this:
 *
 *    --nested always forces the use of the nested X11 backend
 *    --display-server always forces the use of the native backend
 *    --wayland always forces the compositor type to be a Wayland compositor
 *
 * If no flag is passed that forces the compositor type, the compositor type
 * is determined first from the logind session type, or if that fails, from the
 * XDG_SESSION_TYPE environment variable.
 *
 * If no flag is passed that forces the backend type, the backend type is
 * determined given the compositor type. If the compositor is a Wayland
 * compositor, then the native backend is used, or the nested backend, would
 * the native backend not be enabled at build time. If the compositor is not a
 * Wayland compositor, then the X11 Compositing Manager backend is used.
 */
static void
calculate_compositor_configuration (MetaCompositorType  *compositor_type,
                                    GType               *backend_gtype,
                                    unsigned int        *n_properties,
                                    const char         **prop_names[],
                                    GValue              *prop_values[])
{
#ifdef HAVE_WAYLAND
  gboolean run_as_wayland_compositor = ((opt_wayland ||
                                         opt_display_server ||
                                         opt_headless) &&
                                        !opt_x11);

#ifdef HAVE_NATIVE_BACKEND
  if ((opt_wayland || opt_nested || opt_display_server || opt_headless) &&
      opt_x11)
#else
  if ((opt_wayland || opt_nested) && opt_x11)
#endif
    {
      meta_warning ("Can't run both as Wayland compositor and X11 compositing manager");
      meta_exit (META_EXIT_ERROR);
    }

#ifdef HAVE_NATIVE_BACKEND
  if (opt_nested && (opt_display_server || opt_headless))
    {
      meta_warning ("Can't run both as nested and as a display server");
      meta_exit (META_EXIT_ERROR);
    }

  if (!run_as_wayland_compositor && !opt_x11)
    run_as_wayland_compositor = check_for_wayland_session_type ();
#endif /* HAVE_NATIVE_BACKEND */

  if (!run_as_wayland_compositor && opt_no_x11)
    {
      meta_warning ("Can't disable X11 support on X11 compositor");
      meta_exit (META_EXIT_ERROR);
    }

  if (run_as_wayland_compositor)
    *compositor_type = META_COMPOSITOR_TYPE_WAYLAND;
  else
#endif /* HAVE_WAYLAND */
    *compositor_type = META_COMPOSITOR_TYPE_X11;

  *n_properties = 0;
  *prop_names = NULL;
  *prop_values = NULL;

#ifdef HAVE_WAYLAND
  if (opt_nested)
    {
      *backend_gtype = META_TYPE_BACKEND_X11_NESTED;
      return;
    }
#endif /* HAVE_WAYLAND */

#ifdef HAVE_NATIVE_BACKEND
  if (opt_display_server || opt_headless)
    {
      *backend_gtype = META_TYPE_BACKEND_NATIVE;
      if (opt_headless)
        {
          static const char *headless_prop_names[] = {
            "headless",
          };
          static GValue headless_prop_values[] = {
            G_VALUE_INIT,
          };

          g_value_init (&headless_prop_values[0], G_TYPE_BOOLEAN);
          g_value_set_boolean (&headless_prop_values[0], TRUE);

          *n_properties = G_N_ELEMENTS (headless_prop_values);
          *prop_names = headless_prop_names;
          *prop_values = headless_prop_values;
        }
      return;
    }

#ifdef HAVE_WAYLAND
  if (run_as_wayland_compositor)
    {
      *backend_gtype = META_TYPE_BACKEND_NATIVE;
      return;
    }
#endif /* HAVE_WAYLAND */
#endif /* HAVE_NATIVE_BACKEND */

#ifdef HAVE_WAYLAND
  if (run_as_wayland_compositor)
    {
      *backend_gtype = META_TYPE_BACKEND_X11_NESTED;
      return;
    }
  else
#endif /* HAVE_WAYLAND */
    {
      *backend_gtype = META_TYPE_BACKEND_X11_CM;
      return;
    }
}

static gboolean _compositor_configuration_overridden = FALSE;
static MetaCompositorType _compositor_type_override;
static GType _backend_gtype_override;
static GArray *_backend_property_names;
static GArray *_backend_property_values;

void
meta_override_compositor_configuration (MetaCompositorType compositor_type,
                                        GType              backend_gtype,
                                        const char        *first_property_name,
                                        ...)
{
  va_list var_args;
  GArray *names;
  GArray *values;
  GObjectClass *object_class;
  const char *property_name;

  names = g_array_new (FALSE, FALSE, sizeof (const char *));
  values = g_array_new (FALSE, FALSE, sizeof (GValue));
  g_array_set_clear_func (values, (GDestroyNotify) g_value_unset);

  object_class = g_type_class_ref (backend_gtype);

  property_name = first_property_name;

  va_start (var_args, first_property_name);

  while (property_name)
    {
      GValue value = G_VALUE_INIT;
      GParamSpec *pspec;
      GType ptype;
      char *error = NULL;

      pspec = g_object_class_find_property (object_class,
                                            property_name);
      g_assert (pspec);

      ptype = G_PARAM_SPEC_VALUE_TYPE (pspec);
      G_VALUE_COLLECT_INIT (&value, ptype, var_args, 0, &error);
      g_assert (!error);

      g_array_append_val (names, property_name);
      g_array_append_val (values, value);

      property_name = va_arg (var_args, const char *);
    }

  va_end (var_args);

  g_type_class_unref (object_class);

  _compositor_configuration_overridden = TRUE;
  _compositor_type_override = compositor_type;
  _backend_gtype_override = backend_gtype;
  _backend_property_names = names;
  _backend_property_values = values;
}

/**
 * meta_init: (skip)
 *
 * Initialize mutter. Call this after meta_get_option_context() and
 * meta_plugin_manager_set_plugin_type(), and before meta_run().
 */
void
meta_init (void)
{
  struct sigaction act;
  sigset_t empty_mask;
  const char *debug_env;
  MetaCompositorType compositor_type;
  GType backend_gtype;
  unsigned int n_properties;
  const char **prop_names;
  GValue *prop_values;
  int i;

#ifdef HAVE_SYS_PRCTL
  prctl (PR_SET_DUMPABLE, 1);
#endif

  sigemptyset (&empty_mask);
  act.sa_handler = SIG_IGN;
  act.sa_mask    = empty_mask;
  act.sa_flags   = 0;
  if (sigaction (SIGPIPE,  &act, NULL) < 0)
    g_printerr ("Failed to register SIGPIPE handler: %s\n",
                g_strerror (errno));
#ifdef SIGXFSZ
  if (sigaction (SIGXFSZ,  &act, NULL) < 0)
    g_printerr ("Failed to register SIGXFSZ handler: %s\n",
                g_strerror (errno));
#endif

  g_unix_signal_add (SIGTERM, on_sigterm, NULL);

  if (g_getenv ("MUTTER_VERBOSE"))
    meta_set_verbose (TRUE);

  debug_env = g_getenv ("MUTTER_DEBUG");
  if (debug_env)
    {
      MetaDebugTopic topics;

      topics = g_parse_debug_string (debug_env,
                                     meta_debug_keys,
                                     G_N_ELEMENTS (meta_debug_keys));
      meta_add_verbose_topic (topics);
    }

  if (_compositor_configuration_overridden)
    {
      compositor_type = _compositor_type_override;
      backend_gtype = _backend_gtype_override;
      n_properties = _backend_property_names->len;
      prop_names = (const char **) _backend_property_names->data;
      prop_values = (GValue *) _backend_property_values->data;
    }
  else
    {
      calculate_compositor_configuration (&compositor_type,
                                          &backend_gtype,
                                          &n_properties,
                                          &prop_names,
                                          &prop_values);
    }

#ifdef HAVE_WAYLAND
  if (compositor_type == META_COMPOSITOR_TYPE_WAYLAND)
    {
      meta_set_is_wayland_compositor (TRUE);
      if (opt_wayland_display)
        {
          meta_wayland_override_display_name (opt_wayland_display);
          g_free (opt_wayland_display);
        }
    }
#endif

  if (g_get_home_dir ())
    if (chdir (g_get_home_dir ()) < 0)
      meta_warning ("Could not change to home directory %s.",
                    g_get_home_dir ());

  meta_print_self_identity ();

#ifdef HAVE_INTROSPECTION
  g_irepository_prepend_search_path (MUTTER_PKGLIBDIR);
#endif

  /* NB: When running as a hybrid wayland compositor we run our own headless X
   * server so the user can't control the X display to connect too. */
  if (!meta_is_wayland_compositor ())
    meta_select_display (opt_display_name);

  meta_init_backend (backend_gtype, n_properties, prop_names, prop_values);

  for (i = 0; i < n_properties; i++)
    g_value_reset (&prop_values[i]);

  if (_backend_property_names)
    {
      g_array_free (_backend_property_names, TRUE);
      g_array_free (_backend_property_values, TRUE);
    }

#ifdef HAVE_NATIVE_BACKEND
  add_persistent_virtual_monitors ();
#endif

  meta_set_syncing (opt_sync || (g_getenv ("MUTTER_SYNC") != NULL));

  if (opt_replace_wm)
    meta_set_replace_current_wm (TRUE);

  if (opt_save_file && opt_client_id)
    meta_fatal ("Can't specify both SM save file and SM client id");

  meta_main_loop = g_main_loop_new (NULL, FALSE);
}

/**
 * meta_register_with_session:
 *
 * Registers mutter with the session manager.  Call this after completing your own
 * initialization.
 *
 * This should be called when the session manager can safely continue to the
 * next phase of startup and potentially display windows.
 */
void
meta_register_with_session (void)
{
  if (!opt_disable_sm)
    {
      if (opt_client_id == NULL)
        {
          const gchar *desktop_autostart_id;

          desktop_autostart_id = g_getenv ("DESKTOP_AUTOSTART_ID");

          if (desktop_autostart_id != NULL)
            opt_client_id = g_strdup (desktop_autostart_id);
        }

      /* Unset DESKTOP_AUTOSTART_ID in order to avoid child processes to
       * use the same client id. */
      g_unsetenv ("DESKTOP_AUTOSTART_ID");

      meta_session_init (opt_client_id, opt_save_file);
    }
  /* Free memory possibly allocated by the argument parsing which are
   * no longer needed.
   */
  g_free (opt_save_file);
  g_free (opt_display_name);
  g_free (opt_client_id);
}

void
meta_start (void)
{
  meta_prefs_init ();
  meta_prefs_add_listener (prefs_changed_callback, NULL);

  if (!meta_display_open ())
    meta_exit (META_EXIT_ERROR);
}

void
meta_run_main_loop (void)
{
  g_main_loop_run (meta_main_loop);
}

/**
 * meta_run: (skip)
 *
 * Runs mutter. Call this after completing initialization that doesn't require
 * an event loop.
 *
 * Return value: mutter's exit status
 */
int
meta_run (void)
{
  meta_start ();
  meta_run_main_loop ();
  meta_finalize ();

  return meta_exit_code;
}

/**
 * meta_quit:
 * @code: The success or failure code to return to the calling process.
 *
 * Stops Mutter. This tells the event loop to stop processing; it is
 * rather dangerous to use this because this will leave the user with
 * no window manager. We generally do this only if, for example, the
 * session manager asks us to; we assume the session manager knows
 * what it's talking about.
 */
void
meta_quit (MetaExitCode code)
{
  if (g_main_loop_is_running (meta_main_loop))
    {
      meta_exit_code = code;
      g_main_loop_quit (meta_main_loop);
    }
}

MetaExitCode
meta_get_exit_code (void)
{
  return meta_exit_code;
}

/**
 * prefs_changed_callback:
 * @pref:  Which preference has changed
 * @data:  Arbitrary data (which we ignore)
 *
 * Called on pref changes. (One of several functions of its kind and purpose.)
 *
 * FIXME: Why are these particular prefs handled in main.c and not others?
 *        Should they be?
 */
static void
prefs_changed_callback (MetaPreference pref,
                        gpointer       data)
{
  switch (pref)
    {
    case META_PREF_DRAGGABLE_BORDER_WIDTH:
      meta_display_queue_retheme_all_windows (meta_get_display ());
      break;

    default:
      /* handled elsewhere or otherwise */
      break;
    }
}

static MetaDisplayPolicy x11_display_policy_override = -1;

void
meta_override_x11_display_policy (MetaDisplayPolicy x11_display_policy)
{
  x11_display_policy_override = x11_display_policy;
}

MetaDisplayPolicy
meta_get_x11_display_policy (void)
{
  MetaBackend *backend = meta_get_backend ();

  if (META_IS_BACKEND_X11_CM (backend))
    return META_DISPLAY_POLICY_MANDATORY;

  if (x11_display_policy_override != -1)
    return x11_display_policy_override;

#ifdef HAVE_WAYLAND
  if (meta_is_wayland_compositor ())
    {
#ifdef HAVE_XWAYLAND_INITFD
      g_autofree char *unit = NULL;
#endif

      if (opt_no_x11)
        return META_DISPLAY_POLICY_DISABLED;

#ifdef HAVE_XWAYLAND_INITFD
      if (sd_pid_get_user_unit (0, &unit) < 0)
        return META_DISPLAY_POLICY_MANDATORY;
      else
        return META_DISPLAY_POLICY_ON_DEMAND;
#endif
    }
#endif

  return META_DISPLAY_POLICY_MANDATORY;
}

void
meta_test_init (void)
{
#if defined(HAVE_WAYLAND)
  g_autofree char *display_name = g_strdup ("mutter-test-display-XXXXXX");
  int fd = g_mkstemp (display_name);

  meta_override_compositor_configuration (META_COMPOSITOR_TYPE_WAYLAND,
                                          META_TYPE_BACKEND_X11_NESTED,
                                          NULL);
  meta_wayland_override_display_name (display_name);
  meta_xwayland_override_display_number (512 + rand() % 512);
  meta_init ();

  close (fd);
#else
  g_warning ("Tests require wayland support");
#endif
}
