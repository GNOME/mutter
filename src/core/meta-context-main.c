/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2006 Elijah Newren
 * Copyright (C) 2021 Red Hat Inc.
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

#include "core/meta-context-main.h"

#include <glib.h>
#include <gio/gio.h>

#ifdef HAVE_WAYLAND
#include <systemd/sd-login.h>
#endif

#include "backends/meta-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-virtual-monitor.h"
#include "meta/meta-backend.h"

#ifdef HAVE_X11
#include "backends/x11/cm/meta-backend-x11-cm.h"
#include "x11/session.h"
#endif

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#endif

#if defined (HAVE_X11) && defined (HAVE_WAYLAND)
#include "backends/x11/nested/meta-backend-x11-nested.h"
#endif

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland.h"
#endif

typedef struct _MetaContextMainOptions
{
  struct {
    char *display_name;
    gboolean replace;
    gboolean sync;
    gboolean force;
  } x11;
  struct {
    char *save_file;
    char *client_id;
    gboolean disable;
  } sm;
#ifdef HAVE_WAYLAND
  gboolean wayland;
  gboolean nested;
  gboolean no_x11;
  char *wayland_display;
#endif
#ifdef HAVE_NATIVE_BACKEND
  gboolean display_server;
  gboolean headless;
#endif
  gboolean unsafe_mode;
#ifdef HAVE_NATIVE_BACKEND
  GList *virtual_monitor_infos;
#endif
  char *trace_file;
  gboolean debug_control;
} MetaContextMainOptions;

struct _MetaContextMain
{
  GObject parent;

  MetaContextMainOptions options;

  MetaCompositorType compositor_type;

  GList *persistent_virtual_monitors;
};

G_DEFINE_TYPE (MetaContextMain, meta_context_main, META_TYPE_CONTEXT)

static gboolean
check_configuration (MetaContextMain  *context_main,
                     GError          **error)
{
#ifdef HAVE_WAYLAND
  if (context_main->options.x11.force && context_main->options.no_x11)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Can't run in X11 mode with no X11");
      return FALSE;
    }
  if (context_main->options.x11.force && context_main->options.wayland)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Can't run in X11 mode with Wayland enabled");
      return FALSE;
    }
  if (context_main->options.x11.force && context_main->options.nested)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Can't run in X11 mode nested");
      return FALSE;
    }
#endif /* HAVE_WAYLAND */

#ifdef HAVE_NATIVE_BACKEND
  if (context_main->options.x11.force && context_main->options.display_server)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Can't run in X11 mode as a display server");
      return FALSE;
    }

  if (context_main->options.x11.force && context_main->options.headless)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Can't run in X11 mode headlessly");
      return FALSE;
    }

  if (context_main->options.display_server && context_main->options.headless)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Can't run in display server mode headlessly");
      return FALSE;
    }
#endif /* HAVE_NATIVE_BACKEND */

  if (context_main->options.sm.save_file &&
      context_main->options.sm.client_id)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Can't specify both SM save file and SM client id");
      return FALSE;
    }

  return TRUE;
}

#if defined(HAVE_WAYLAND) && defined(HAVE_NATIVE_BACKEND)
static gboolean
session_type_is_supported (const char *session_type)
{
   return (g_strcmp0 (session_type, "x11") == 0) ||
          (g_strcmp0 (session_type, "wayland") == 0);
}

static char *
find_session_type (GError **error)
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

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "Unsupported session type");
  return NULL;

out:
  return session_type;
}
#else /* defined(HAVE_WAYLAND) && defined(HAVE_NATIVE_BACKEND) */
static char *
find_session_type (GError **error)
{
  return g_strdup ("x11");
}
#endif /* defined(HAVE_WAYLAND) && defined(HAVE_NATIVE_BACKEND) */

static MetaCompositorType
determine_compositor_type (MetaContextMain  *context_main,
                           GError          **error)
{
  g_autofree char *session_type = NULL;

#ifdef HAVE_WAYLAND
  if (context_main->options.wayland ||
#ifdef HAVE_NATIVE_BACKEND
      context_main->options.display_server ||
      context_main->options.headless ||
#endif /* HAVE_NATIVE_BACKEND */
      context_main->options.nested)
    return META_COMPOSITOR_TYPE_WAYLAND;
#endif /* HAVE_WAYLAND */

  if (context_main->options.x11.force)
    return META_COMPOSITOR_TYPE_X11;

  session_type = find_session_type (error);
  if (!session_type)
    return -1;

  if (strcmp (session_type, "x11") == 0)
    return META_COMPOSITOR_TYPE_X11;
#ifdef HAVE_WAYLAND
  else if (strcmp (session_type, "wayland") == 0)
    return META_COMPOSITOR_TYPE_WAYLAND;
#endif
  else
    g_assert_not_reached ();
}

static gboolean
meta_context_main_configure (MetaContext   *context,
                             int           *argc,
                             char        ***argv,
                             GError       **error)
{
  MetaContextMain *context_main = META_CONTEXT_MAIN (context);
  MetaContextClass *context_class =
    META_CONTEXT_CLASS (meta_context_main_parent_class);

  if (!context_class->configure (context, argc, argv, error))
    return FALSE;

  if (!check_configuration (context_main, error))
    return FALSE;

  context_main->compositor_type = determine_compositor_type (context_main,
                                                             error);
  if (context_main->compositor_type == -1)
    return FALSE;

#ifdef HAVE_WAYLAND
  if (context_main->options.wayland_display)
    meta_wayland_override_display_name (context_main->options.wayland_display);
#endif

  if (!context_main->options.sm.client_id)
    {
      const char *desktop_autostart_id;

      desktop_autostart_id = g_getenv ("DESKTOP_AUTOSTART_ID");
      if (desktop_autostart_id)
        context_main->options.sm.client_id = g_strdup (desktop_autostart_id);
    }

#ifdef HAVE_PROFILER
  meta_context_set_trace_file (context, context_main->options.trace_file);
#endif

  if (context_main->options.debug_control)
    {
      MetaDebugControl *debug_control = meta_context_get_debug_control (context);

      meta_debug_control_export (debug_control);
    }

  g_unsetenv ("DESKTOP_AUTOSTART_ID");

  return TRUE;
}

static MetaCompositorType
meta_context_main_get_compositor_type (MetaContext *context)
{
  MetaContextMain *context_main = META_CONTEXT_MAIN (context);

  return context_main->compositor_type;
}

static MetaX11DisplayPolicy
meta_context_main_get_x11_display_policy (MetaContext *context)
{
  MetaCompositorType compositor_type;
#ifdef HAVE_WAYLAND
  MetaContextMain *context_main = META_CONTEXT_MAIN (context);
  g_autofree char *unit = NULL;
#endif

  compositor_type = meta_context_get_compositor_type (context);
  switch (compositor_type)
    {
    case META_COMPOSITOR_TYPE_X11:
      return META_X11_DISPLAY_POLICY_MANDATORY;
    case META_COMPOSITOR_TYPE_WAYLAND:
#ifdef HAVE_WAYLAND
      if (context_main->options.no_x11)
        return META_X11_DISPLAY_POLICY_DISABLED;
      else if (sd_pid_get_user_unit (0, &unit) < 0)
        return META_X11_DISPLAY_POLICY_MANDATORY;
      else
        return META_X11_DISPLAY_POLICY_ON_DEMAND;
#else /* HAVE_WAYLAND */
      g_assert_not_reached ();
#endif /* HAVE_WAYLAND */
    }

  g_assert_not_reached ();
}

static gboolean
meta_context_main_is_replacing (MetaContext *context)
{
  MetaContextMain *context_main = META_CONTEXT_MAIN (context);

  return context_main->options.x11.replace;
}

#ifdef HAVE_NATIVE_BACKEND
static gboolean
add_persistent_virtual_monitors (MetaContextMain  *context_main,
                                 GError          **error)
{
  MetaContext *context = META_CONTEXT (context_main);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *l;

  for (l = context_main->options.virtual_monitor_infos; l; l = l->next)
    {
      MetaVirtualMonitorInfo *info = l->data;
      MetaVirtualMonitor *virtual_monitor;

      virtual_monitor =
        meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                     info,
                                                     error);
      if (!virtual_monitor)
        {
          g_prefix_error (error, "Failed to add virtual monitor: ");
          return FALSE;
        }

      context_main->persistent_virtual_monitors =
        g_list_append (context_main->persistent_virtual_monitors, virtual_monitor);
    }

  if (context_main->options.virtual_monitor_infos)
    {
      g_list_free_full (context_main->options.virtual_monitor_infos,
                        (GDestroyNotify) meta_virtual_monitor_info_free);
      context_main->options.virtual_monitor_infos = NULL;

      meta_monitor_manager_reload (monitor_manager);
    }

  return TRUE;
}
#endif

static gboolean
meta_context_main_setup (MetaContext  *context,
                         GError      **error)
{
  MetaContextMain *context_main = META_CONTEXT_MAIN (context);

  if (!META_CONTEXT_CLASS (meta_context_main_parent_class)->setup (context,
                                                                   error))
    return FALSE;

  meta_context_set_unsafe_mode (context, context_main->options.unsafe_mode);

#ifdef HAVE_NATIVE_BACKEND
  if (!add_persistent_virtual_monitors (context_main, error))
    return FALSE;
#endif

  return TRUE;
}

#ifdef HAVE_X11
static MetaBackend *
create_x11_cm_backend (MetaContext  *context,
                       GError      **error)
{
  MetaContextMain *context_main = META_CONTEXT_MAIN (context);

#ifdef HAVE_NATIVE_BACKEND
  if (context_main->options.virtual_monitor_infos)
    g_warning ("Ignoring added virtual monitors in X11 session");
#endif

  return g_initable_new (META_TYPE_BACKEND_X11_CM,
                         NULL, error,
                         "context", context,
                         "display-name", context_main->options.x11.display_name,
                         NULL);
}
#endif

#if defined (HAVE_X11) && defined (HAVE_WAYLAND)
static MetaBackend *
create_nested_backend (MetaContext  *context,
                       GError      **error)
{
  return g_initable_new (META_TYPE_BACKEND_X11_NESTED,
                         NULL, error,
                         "context", context,
                         NULL);
}
#endif

#ifdef HAVE_WAYLAND
#ifdef HAVE_NATIVE_BACKEND
static MetaBackend *
create_headless_backend (MetaContext  *context,
                         GError      **error)
{
  return g_initable_new (META_TYPE_BACKEND_NATIVE,
                         NULL, error,
                         "context", context,
                         "mode", META_BACKEND_NATIVE_MODE_HEADLESS,
                         NULL);
}

static MetaBackend *
create_native_backend (MetaContext  *context,
                       GError      **error)
{
  return g_initable_new (META_TYPE_BACKEND_NATIVE,
                         NULL, error,
                         "context", context,
                         NULL);
}
#endif /* HAVE_NATIVE_BACKEND */
#endif /* HAVE_WAYLAND */

static MetaBackend *
meta_context_main_create_backend (MetaContext  *context,
                                  GError      **error)
{
#ifdef HAVE_WAYLAND
  MetaContextMain *context_main = META_CONTEXT_MAIN (context);
#endif
  MetaCompositorType compositor_type;

  compositor_type = meta_context_get_compositor_type (context);
  switch (compositor_type)
    {
    case META_COMPOSITOR_TYPE_X11:
#ifdef HAVE_X11
      return create_x11_cm_backend (context, error);
#endif
    case META_COMPOSITOR_TYPE_WAYLAND:
#ifdef HAVE_WAYLAND
#ifdef HAVE_X11
      if (context_main->options.nested)
        return create_nested_backend (context, error);
      else
#endif
#ifdef HAVE_NATIVE_BACKEND
      if (context_main->options.headless)
        return create_headless_backend (context, error);
      else
        return create_native_backend (context, error);
#endif /* HAVE_NATIVE_BACKEND */
#else /* HAVE_WAYLAND */
      g_assert_not_reached ();
#endif /* HAVE_WAYLAND */
    }

  g_assert_not_reached ();
}

#ifdef HAVE_X11
static void
meta_context_main_notify_ready (MetaContext *context)
{
  MetaContextMain *context_main = META_CONTEXT_MAIN (context);

  if (!context_main->options.sm.disable)
    {
      meta_session_init (context,
                         context_main->options.sm.client_id,
                         context_main->options.sm.save_file);
    }
  g_clear_pointer (&context_main->options.sm.client_id, g_free);
  g_clear_pointer (&context_main->options.sm.save_file, g_free);
}

static gboolean
meta_context_main_is_x11_sync (MetaContext *context)
{
  MetaContextMain *context_main = META_CONTEXT_MAIN (context);

  return context_main->options.x11.sync || g_getenv ("MUTTER_SYNC");
}
#endif

#ifdef HAVE_NATIVE_BACKEND
static gboolean
add_virtual_monitor_cb (const char  *option_name,
                        const char  *value,
                        gpointer     user_data,
                        GError     **error)
{
  MetaContextMain *context_main = user_data;
  int width, height;
  float refresh_rate;

  if (meta_parse_monitor_mode (value, &width, &height, &refresh_rate, 60.0))
    {
      g_autofree char *serial = NULL;
      MetaVirtualMonitorInfo *virtual_monitor;
      int n_existing_virtual_monitor_infos;

      n_existing_virtual_monitor_infos =
        g_list_length (context_main->options.virtual_monitor_infos);
      serial = g_strdup_printf ("0x%.2x", n_existing_virtual_monitor_infos);
      virtual_monitor = meta_virtual_monitor_info_new (width,
                                                       height,
                                                       refresh_rate,
                                                       "MetaVendor",
                                                       "MetaVirtualMonitor",
                                                       serial);
      context_main->options.virtual_monitor_infos =
        g_list_append (context_main->options.virtual_monitor_infos,
                       virtual_monitor);
      return TRUE;
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Unrecognizable virtual monitor spec '%s'", value);
      return FALSE;
    }
}
#endif /* HAVE_NATIVE_BACKEND */

static void
meta_context_main_add_option_entries (MetaContextMain *context_main)
{
  MetaContext *context = META_CONTEXT (context_main);
  GOptionEntry options[] = {
#ifdef HAVE_X11
    {
      "replace", 'r', 0, G_OPTION_ARG_NONE,
      &context_main->options.x11.replace,
      N_("Replace the running window manager"),
      NULL
    },
    {
      "display", 'd', 0, G_OPTION_ARG_STRING,
      &context_main->options.x11.display_name,
      N_("X Display to use"),
      "DISPLAY"
    },
    {
      "sm-disable", 0, 0, G_OPTION_ARG_NONE,
      &context_main->options.sm.disable,
      N_("Disable connection to session manager"),
      NULL
    },
    {
      "sm-client-id", 0, 0, G_OPTION_ARG_STRING,
      &context_main->options.sm.client_id,
      N_("Specify session management ID"),
      "ID"
    },
    {
      "sm-save-file", 0, 0, G_OPTION_ARG_FILENAME,
      &context_main->options.sm.save_file,
      N_("Initialize session from savefile"),
      "FILE"
    },
    {
      "sync", 0, 0, G_OPTION_ARG_NONE,
      &context_main->options.x11.sync,
      N_("Make X calls synchronous"),
      NULL
    },
#endif
#ifdef HAVE_WAYLAND
    {
      "wayland", 0, 0, G_OPTION_ARG_NONE,
      &context_main->options.wayland,
      N_("Run as a wayland compositor"),
      NULL
    },
    {
      "nested", 0, 0, G_OPTION_ARG_NONE,
      &context_main->options.nested,
      N_("Run as a nested compositor"),
      NULL
    },
    {
      "no-x11", 0, 0, G_OPTION_ARG_NONE,
      &context_main->options.no_x11,
      N_("Run wayland compositor without starting Xwayland"),
      NULL
    },
    {
      "wayland-display", 0, 0, G_OPTION_ARG_STRING,
      &context_main->options.wayland_display,
      N_("Specify Wayland display name to use"),
      NULL
    },
#endif
#ifdef HAVE_NATIVE_BACKEND
    {
      "display-server", 0, 0, G_OPTION_ARG_NONE,
      &context_main->options.display_server,
      N_("Run as a full display server, rather than nested")
    },
    {
      "headless", 0, 0, G_OPTION_ARG_NONE,
      &context_main->options.headless,
      N_("Run as a headless display server")
    },
    {
      "virtual-monitor", 0, 0, G_OPTION_ARG_CALLBACK,
      add_virtual_monitor_cb,
      N_("Add persistent virtual monitor (WxH or WxH@R)")
    },
#endif
    {
      "unsafe-mode", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE,
      &context_main->options.unsafe_mode,
      "Run in unsafe mode"
    },
#ifdef HAVE_X11
    {
      "x11", 0, 0, G_OPTION_ARG_NONE,
      &context_main->options.x11.force,
      N_("Run with X11 backend")
    },
#endif
    {
      "profile", 0, 0, G_OPTION_ARG_FILENAME,
      &context_main->options.trace_file,
      N_("Profile performance using trace instrumentation"),
      "FILE"
    },
    {
      "debug-control", 0, 0, G_OPTION_ARG_NONE,
      &context_main->options.debug_control,
      N_("Enable debug control D-Bus interface")
    },
    { NULL }
  };

  meta_context_add_option_entries (context, options, GETTEXT_PACKAGE);
}

/**
 * meta_create_context:
 * @name: Human readable name of display server or window manager
 *
 * Create a context.
 *
 * Returns: (transfer full): A new context instance.
 */
MetaContext *
meta_create_context (const char *name)
{
  return g_object_new (META_TYPE_CONTEXT_MAIN,
                       "name", name,
                       NULL);
}

static void
meta_context_main_finalize (GObject *object)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaContextMain *context_main = META_CONTEXT_MAIN (object);

  g_list_free_full (context_main->persistent_virtual_monitors, g_object_unref);
  context_main->persistent_virtual_monitors = NULL;
#endif

  G_OBJECT_CLASS (meta_context_main_parent_class)->finalize (object);
}

static void
meta_context_main_constructed (GObject *object)
{
  MetaContextMain *context_main = META_CONTEXT_MAIN (object);

  G_OBJECT_CLASS (meta_context_main_parent_class)->constructed (object);

  meta_context_main_add_option_entries (context_main);
}

static void
meta_context_main_class_init (MetaContextMainClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaContextClass *context_class = META_CONTEXT_CLASS (klass);

  object_class->finalize = meta_context_main_finalize;
  object_class->constructed = meta_context_main_constructed;

  context_class->configure = meta_context_main_configure;
  context_class->get_compositor_type = meta_context_main_get_compositor_type;
  context_class->get_x11_display_policy =
    meta_context_main_get_x11_display_policy;
  context_class->is_replacing = meta_context_main_is_replacing;
  context_class->setup = meta_context_main_setup;
  context_class->create_backend = meta_context_main_create_backend;
#ifdef HAVE_X11
  context_class->notify_ready = meta_context_main_notify_ready;
  context_class->is_x11_sync = meta_context_main_is_x11_sync;
#endif
}

static void
meta_context_main_init (MetaContextMain *context_main)
{
  context_main->compositor_type = -1;
}
