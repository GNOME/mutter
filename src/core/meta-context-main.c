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

#include "backends/meta-monitor-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-virtual-monitor.h"
#include "core/meta-session-manager.h"
#include "meta/meta-backend.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-backend-native-types.h"
#endif

#ifdef HAVE_DEVKIT
#include "core/meta-mdk.h"
#endif

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland.h"
#endif

typedef struct _MetaContextMainOptions
{
#ifdef HAVE_WAYLAND
  gboolean wayland;
  char *wayland_display;
#endif
#ifdef HAVE_NATIVE_BACKEND
  gboolean display_server;
  gboolean headless;
  gboolean devkit;
  GList *virtual_monitor_infos;
#endif
  char *trace_file;
  gboolean debug_control;
  gboolean unsafe_mode;
} MetaContextMainOptions;

struct _MetaContextMain
{
  GObject parent;

  MetaContextMainOptions options;

  MetaSessionManager *session_manager;

#ifdef HAVE_NATIVE_BACKEND
  GList *persistent_virtual_monitors;
#endif

#ifdef HAVE_DEVKIT
  MetaMdk *mdk;
#endif
};

G_DEFINE_TYPE (MetaContextMain, meta_context_main, META_TYPE_CONTEXT)

static gboolean
check_configuration (MetaContextMain  *context_main,
                     GError          **error)
{
#ifdef HAVE_NATIVE_BACKEND
  if (context_main->options.headless && context_main->options.devkit)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Can't run in both MDK and headless mode");
      return FALSE;
    }

  if (context_main->options.display_server &&
      (context_main->options.headless || context_main->options.devkit))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Can't run in display server mode headlessly");
      return FALSE;
    }
#endif /* HAVE_NATIVE_BACKEND */

  return TRUE;
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

#ifdef HAVE_WAYLAND
  if (context_main->options.wayland_display)
    meta_wayland_override_display_name (context_main->options.wayland_display);
#endif

#ifdef HAVE_PROFILER
  meta_context_set_trace_file (context, context_main->options.trace_file);
#endif

  if (context_main->options.debug_control)
    {
      MetaDebugControl *debug_control = meta_context_get_debug_control (context);

      meta_debug_control_set_exported (debug_control, TRUE);
    }

  g_unsetenv ("DESKTOP_AUTOSTART_ID");

  return TRUE;
}

static MetaX11DisplayPolicy
meta_context_main_get_x11_display_policy (MetaContext *context)
{
#ifdef HAVE_LOGIND
  g_autofree char *unit = NULL;

  if (sd_pid_get_user_unit (0, &unit) < 0)
    return META_X11_DISPLAY_POLICY_MANDATORY;
  else
#endif
  return META_X11_DISPLAY_POLICY_ON_DEMAND;
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

#ifdef HAVE_DEVKIT
static gboolean
initialize_mdk (MetaContext  *context,
                GError      **error)
{
  MetaContextMain *context_main = META_CONTEXT_MAIN (context);
  MetaMdkFlag flags;

  if (context_main->options.headless)
    flags = META_MDK_FLAG_NONE;
  else
    flags = META_MDK_FLAG_LAUNCH_VIEWER;
  context_main->mdk = meta_mdk_new (context, flags, error);
  if (!context_main->mdk)
    return FALSE;

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

#ifdef HAVE_DEVKIT
  if (context_main->options.devkit)
    {
      if (!initialize_mdk (context, error))
        return FALSE;
    }
#endif

  return TRUE;
}

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

#ifdef HAVE_WAYLAND
#ifdef HAVE_NATIVE_BACKEND
      if (context_main->options.headless ||
          context_main->options.devkit)
        return create_headless_backend (context, error);

      return create_native_backend (context, error);
#endif /* HAVE_NATIVE_BACKEND */
#else /* HAVE_WAYLAND */
      g_assert_not_reached ();
#endif /* HAVE_WAYLAND */

  g_assert_not_reached ();
}

static void
meta_context_main_notify_ready (MetaContext *context)
{
  MetaContextMain *context_main = META_CONTEXT_MAIN (context);
  g_autoptr (GError) error = NULL;

  context_main->session_manager =
    meta_session_manager_new (meta_context_get_nick (context), &error);

  if (!context_main->session_manager)
    g_critical ("Could not create session manager: %s", error->message);
}

static MetaSessionManager *
meta_context_main_get_session_manager (MetaContext *context)
{
  MetaContextMain *context_main = META_CONTEXT_MAIN (context);

  return context_main->session_manager;
}

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
#ifdef HAVE_WAYLAND
    {
      "wayland", 0, 0, G_OPTION_ARG_NONE,
      &context_main->options.wayland,
      N_("Run as a wayland compositor"),
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
#ifdef HAVE_DEVKIT
    {
      "devkit", 0, 0, G_OPTION_ARG_NONE,
      &context_main->options.devkit,
      N_("Run development kit")
    },
#endif
    {
      "unsafe-mode", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE,
      &context_main->options.unsafe_mode,
      "Run in unsafe mode"
    },
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

  if (context_main->session_manager)
    meta_session_manager_save_sync (context_main->session_manager, NULL);
  g_clear_object (&context_main->session_manager);
#endif

#ifdef HAVE_DEVKIT
  g_clear_object (&context_main->mdk);
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
  context_class->get_x11_display_policy =
    meta_context_main_get_x11_display_policy;
  context_class->setup = meta_context_main_setup;
  context_class->create_backend = meta_context_main_create_backend;
  context_class->notify_ready = meta_context_main_notify_ready;
  context_class->get_session_manager = meta_context_main_get_session_manager;
}

static void
meta_context_main_init (MetaContextMain *context_main)
{
}
