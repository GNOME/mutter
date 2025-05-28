/*
 * Copyright (C) 2013 Red Hat, Inc.
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

#include "config.h"

#include "backends/meta-launcher.h"

#include <gio/gunixfdlist.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <malloc.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <systemd/sd-login.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-dbus-utils.h"

#include "meta-dbus-login1.h"

enum
{
  PROP_0,

  PROP_SESSION_ACTIVE,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _MetaLauncher
{
  GObject parent;

  MetaBackend *backend;
  MetaDBusLogin1Session *session_proxy;
  MetaDBusLogin1Seat *seat_proxy;

  gboolean session_active;
  gboolean have_control;
};

G_DEFINE_FINAL_TYPE (MetaLauncher,
                     meta_launcher,
                     G_TYPE_OBJECT)

static void
meta_launcher_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  MetaLauncher *launcher = META_LAUNCHER (object);

  switch (prop_id)
    {
    case PROP_SESSION_ACTIVE:
      g_value_set_boolean (value, launcher->session_active);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_launcher_dispose (GObject *object)
{
  MetaLauncher *launcher = META_LAUNCHER (object);

  if (launcher->have_control && launcher->session_proxy)
    {
      meta_dbus_login1_session_call_release_control_sync (launcher->session_proxy,
                                                          NULL, NULL);
      launcher->have_control = FALSE;
    }

  g_clear_object (&launcher->seat_proxy);
  g_clear_object (&launcher->session_proxy);

  G_OBJECT_CLASS (meta_launcher_parent_class)->dispose (object);
}

static void
meta_launcher_class_init (MetaLauncherClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_launcher_dispose;
  object_class->get_property = meta_launcher_get_property;

  obj_props[PROP_SESSION_ACTIVE] =
    g_param_spec_boolean ("session-active", NULL, NULL,
                          TRUE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_launcher_init (MetaLauncher *launcher)
{
}

static MetaDBusLogin1Session *
get_session_proxy_from_id (const char    *session_id,
                           GCancellable  *cancellable,
                           GError       **error)
{
  g_autoptr (MetaDBusLogin1Session) session_proxy = NULL;
  g_autofree char *proxy_path = NULL;

  proxy_path = get_escaped_dbus_path ("/org/freedesktop/login1/session",
                                      session_id);

  session_proxy =
    meta_dbus_login1_session_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                     G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                     "org.freedesktop.login1",
                                                     proxy_path,
                                                     cancellable, error);
  if (!session_proxy)
    {
      g_prefix_error (error, "Could not get session proxy: ");
      return NULL;
    }

  g_warn_if_fail (g_dbus_proxy_get_name_owner (G_DBUS_PROXY (session_proxy)));

  return g_steal_pointer (&session_proxy);
}

static MetaDBusLogin1Session *
get_session_proxy_from_xdg_session_id (GCancellable  *cancellable,
                                       GError       **error)
{
  const char *xdg_session_id = NULL;
  int saved_errno;

  xdg_session_id = g_getenv ("XDG_SESSION_ID");
  if (!xdg_session_id)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "XDG_SESSION_ID is not set");
      return NULL;
    }

  saved_errno = sd_session_is_active (xdg_session_id);
  if (saved_errno < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Failed to get status of XDG_SESSION_ID session (%s)",
                   g_strerror (-saved_errno));
      return NULL;
    }

  return get_session_proxy_from_id (xdg_session_id, cancellable, error);
}

static MetaDBusLogin1Session *
get_session_proxy_from_pid (GCancellable  *cancellable,
                            GError       **error)
{
  g_autoptr (MetaDBusLogin1Manager) manager_proxy = NULL;
  g_autoptr (MetaDBusLogin1Session) session_proxy = NULL;
  char *session_path = NULL;

  manager_proxy =
    meta_dbus_login1_manager_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                     G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                     "org.freedesktop.login1",
                                                     "/org/freedesktop/login1",
                                                     cancellable,
                                                     error);
  if (!manager_proxy)
    return NULL;

  if (!meta_dbus_login1_manager_call_get_session_by_pid_sync (manager_proxy,
                                                              0,
                                                              &session_path,
                                                              cancellable,
                                                              error))
    return NULL;

  session_proxy =
    meta_dbus_login1_session_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                     G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                     "org.freedesktop.login1",
                                                     session_path,
                                                     cancellable,
                                                     error);
  if (!session_proxy)
    return NULL;

  g_warn_if_fail (g_dbus_proxy_get_name_owner (G_DBUS_PROXY (session_proxy)));

  return g_steal_pointer (&session_proxy);
}

static char *
get_display_session (GError **error)
{
  g_autofree char *session_id = NULL;
  int saved_errno;
  int n_sessions;
  g_auto (GStrv) sessions = NULL;

  saved_errno = sd_uid_get_display (getuid (), &session_id);
  if (saved_errno >= 0)
    return g_steal_pointer (&session_id);

  if (saved_errno != -ENODATA)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Couldn't get display for user %d: %s",
                   getuid (),
                   g_strerror (-saved_errno));
      return NULL;
    }

  /* no session, maybe there's a greeter session */
  n_sessions = sd_uid_get_sessions (getuid (), 1, &sessions);
  if (n_sessions < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Failed to get all sessions for user %d (%m)",
                   getuid ());
      return NULL;
    }

  if (n_sessions == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "User %d has no sessions",
                   getuid ());
      return NULL;
    }

  for (int i = 0; i < n_sessions; ++i)
    {
      g_autofree char *class = NULL;

      saved_errno = sd_session_get_class (sessions[i], &class);
      if (saved_errno < 0)
        {
          g_warning ("Couldn't get class for session '%d': %s",
                     i,
                     g_strerror (-saved_errno));
          continue;
        }

      if (g_strcmp0 (class, "greeter") == 0)
        return g_strdup (sessions[i]);
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
               "Couldn't find a session or a greeter session for user %d",
               getuid ());
  return NULL;
}

static MetaDBusLogin1Session *
get_session_proxy_from_display (GCancellable  *cancellable,
                                GError       **error)
{
  const char * const graphical_session_types[] =
    { "wayland", "x11", "mir", NULL };
  const char * const active_states[] =
    { "active", "online", NULL };
  g_autofree char *session_id = NULL;
  g_autofree char *type = NULL;
  g_autofree char *state = NULL;
  int saved_errno;

  g_assert (error == NULL || *error == NULL);

  session_id = get_display_session (error);
  if (!session_id)
    return NULL;

  /* sd_uid_get_display will return any session if there is no graphical
   * one, so let's check it really is graphical. */
  saved_errno = sd_session_get_type (session_id, &type);
  if (saved_errno < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_FOUND,
                   "Couldn't get type for session '%s': %s",
                   session_id,
                   g_strerror (-saved_errno));
      return NULL;
    }

  if (!g_strv_contains (graphical_session_types, type))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_FOUND,
                   "Session '%s' is not a graphical session (type: '%s')",
                   session_id,
                   type);
      return NULL;
    }

  /* and display sessions can be 'closing' if they are logged out but
   * some processes are lingering; we shouldn't consider these */
  saved_errno = sd_session_get_state (session_id, &state);
  if (saved_errno < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_FOUND,
                   "Couldn't get state for session '%s': %s",
                   session_id,
                   g_strerror (-saved_errno));
      return NULL;
    }

  if (!g_strv_contains (active_states, state))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_FOUND,
                   "Session '%s' is not active",
                   session_id);
      return NULL;
    }

  return get_session_proxy_from_id (session_id, cancellable, error);
}

static MetaDBusLogin1Session *
get_session_proxy (GCancellable  *cancellable,
                   GError       **error)
{
  g_autofree char *session_id = NULL;
  g_autoptr (GError) local_error = NULL;
  MetaDBusLogin1Session *session_proxy;

  session_proxy = get_session_proxy_from_xdg_session_id (cancellable,
                                                         &local_error);
  if (session_proxy)
    return session_proxy;

  meta_topic (META_DEBUG_BACKEND,
              "Failed to get the session from environment: %s",
              local_error->message);
  g_clear_error (&local_error);

  session_proxy = get_session_proxy_from_pid (cancellable, &local_error);
  if (session_proxy)
    return session_proxy;

  meta_topic (META_DEBUG_BACKEND,
              "Failed to get the session from login1: %s",
              local_error->message);
  g_clear_error (&local_error);

  session_proxy = get_session_proxy_from_display (cancellable, &local_error);
  if (session_proxy)
    return session_proxy;

  meta_topic (META_DEBUG_BACKEND,
              "Failed to get any session: %s",
              local_error->message);
  g_clear_error (&local_error);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
               "Failed to find any matching session");
  return NULL;

}

static MetaDBusLogin1Seat *
get_seat_proxy (MetaDBusLogin1Session  *session_proxy,
                GCancellable           *cancellable,
                GError                **error)
{
  GVariant *seat_variant;
  MetaDBusLogin1Seat *seat_proxy;
  const char *seat_path;
  GDBusProxyFlags flags;

  seat_variant = meta_dbus_login1_session_get_seat (session_proxy);

  g_variant_get (seat_variant, "(s&o)", NULL, &seat_path);

  flags = G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START;
  seat_proxy =
    meta_dbus_login1_seat_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                  flags,
                                                  "org.freedesktop.login1",
                                                  seat_path,
                                                  cancellable,
                                                  error);
  if (!seat_proxy)
    g_prefix_error (error, "Could not get seat proxy: ");

  g_warn_if_fail (g_dbus_proxy_get_name_owner (G_DBUS_PROXY (seat_proxy)));

  return seat_proxy;
}

static void
sync_active (MetaLauncher *self)
{
  MetaDBusLogin1Session *session_proxy = self->session_proxy;
  gboolean active;

  active = meta_dbus_login1_session_get_active (session_proxy);
  if (active == self->session_active)
    return;

  self->session_active = active;
  g_object_notify_by_pspec (G_OBJECT (self),
                            obj_props[PROP_SESSION_ACTIVE]);
}

static void
on_active_changed (MetaDBusLogin1Session *session,
                   GParamSpec            *pspec,
                   gpointer               user_data)
{
  MetaLauncher *self = user_data;
  sync_active (self);
}

MetaLauncher *
meta_launcher_new (MetaBackend  *backend,
                   GError      **error)
{
  g_autoptr (MetaLauncher) launcher = NULL;
  g_autoptr (MetaDBusLogin1Session) session_proxy = NULL;
  g_autoptr (MetaDBusLogin1Seat) seat_proxy = NULL;
  g_autoptr (GError) local_error = NULL;

  session_proxy = get_session_proxy (NULL, error);
  if (!session_proxy)
    return NULL;

  seat_proxy = get_seat_proxy (session_proxy, NULL, &local_error);
  if (!seat_proxy)
    {
      meta_topic (META_DEBUG_BACKEND,
                  "Failed to get the seat of proxy %s: %s",
                  meta_dbus_login1_session_get_id (session_proxy),
                  local_error->message);

      g_clear_error (&local_error);
    }

  launcher = g_object_new (META_TYPE_LAUNCHER, NULL);
  launcher->backend = backend;
  launcher->session_proxy = g_steal_pointer (&session_proxy);
  launcher->session_active = TRUE;
  launcher->seat_proxy = g_steal_pointer (&seat_proxy);

  g_signal_connect (launcher->session_proxy,
                    "notify::active",
                    G_CALLBACK (on_active_changed),
                    launcher);
  sync_active (launcher);

  return g_steal_pointer (&launcher);
}

gboolean
meta_launcher_activate_vt (MetaLauncher  *launcher,
                           signed char    vt,
                           GError       **error)
{
  g_assert (launcher->seat_proxy);

  return meta_dbus_login1_seat_call_switch_to_sync (launcher->seat_proxy,
                                                    vt,
                                                    NULL,
                                                    error);
}

gboolean
meta_launcher_is_session_active (MetaLauncher *launcher)
{
  return launcher->session_active;
}

gboolean
meta_launcher_take_control (MetaLauncher  *launcher,
                            GError       **error)
{
  if (!meta_dbus_login1_session_call_take_control_sync (launcher->session_proxy,
                                                        FALSE,
                                                        NULL,
                                                        error))
    return FALSE;

  launcher->have_control = TRUE;
  return TRUE;
}

const char *
meta_launcher_get_seat_id (MetaLauncher *launcher)
{
  if (!launcher->seat_proxy)
    return NULL;

  return meta_dbus_login1_seat_get_id (launcher->seat_proxy);
}

MetaDBusLogin1Session *
meta_launcher_get_session_proxy (MetaLauncher *launcher)
{
  return launcher->session_proxy;
}

MetaBackend *
meta_launcher_get_backend (MetaLauncher *launcher)
{
  return launcher->backend;
}
