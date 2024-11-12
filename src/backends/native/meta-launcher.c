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

#include "backends/native/meta-launcher.h"

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
  char *seat_id;

  gboolean session_active;
  gboolean have_control;
};

G_DEFINE_FINAL_TYPE (MetaLauncher,
                     meta_launcher,
                     G_TYPE_OBJECT)

const char *
meta_launcher_get_seat_id (MetaLauncher *launcher)
{
  return launcher->seat_id;
}

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

  g_clear_pointer (&launcher->seat_id, g_free);
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

static gboolean
find_systemd_session (char   **session_id,
                      GError **error)
{
  const char * const graphical_session_types[] =
    { "wayland", "x11", "mir", NULL };
  const char * const active_states[] =
    { "active", "online", NULL };
  g_autofree char *class = NULL;
  g_autofree char *local_session_id = NULL;
  g_autofree char *type = NULL;
  g_autofree char *state = NULL;
  g_auto (GStrv) sessions = NULL;
  int n_sessions;
  int saved_errno;
  const char *xdg_session_id = NULL;

  g_assert (session_id != NULL);
  g_assert (error == NULL || *error == NULL);

  xdg_session_id = g_getenv ("XDG_SESSION_ID");
  if (xdg_session_id)
    {
      saved_errno = sd_session_is_active (xdg_session_id);
      if (saved_errno < 0)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_NOT_FOUND,
                       "Failed to get status of XDG_SESSION_ID session (%s)",
                       g_strerror (-saved_errno));
          return FALSE;
        }

      *session_id = g_strdup (xdg_session_id);
      return TRUE;
    }

  /* if we are in a logind session, we can trust that value, so use it. This
   * happens for example when you run mutter directly from a VT but when
   * systemd starts us we will not be in a logind session. */
  saved_errno = sd_pid_get_session (0, &local_session_id);
  if (saved_errno < 0)
    {
      if (saved_errno != -ENODATA)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_NOT_FOUND,
                       "Failed to get session by pid for user %d (%s)",
                       getuid (),
                       g_strerror (-saved_errno));
          return FALSE;
        }
    }
  else
    {
      *session_id = g_steal_pointer (&local_session_id);
      return TRUE;
    }

  saved_errno = sd_uid_get_display (getuid (), &local_session_id);
  if (saved_errno < 0)
    {
      /* no session, maybe there's a greeter session */
      if (saved_errno == -ENODATA)
        {
          n_sessions = sd_uid_get_sessions (getuid (), 1, &sessions);
          if (n_sessions < 0)
            {
              g_set_error (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_FOUND,
                           "Failed to get all sessions for user %d (%m)",
                           getuid ());
              return FALSE;
            }

          if (n_sessions == 0)
            {
              g_set_error (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_FOUND,
                           "User %d has no sessions",
                           getuid ());
              return FALSE;
            }

          for (int i = 0; i < n_sessions; ++i)
            {
              saved_errno = sd_session_get_class (sessions[i], &class);
              if (saved_errno < 0)
                {
                  g_warning ("Couldn't get class for session '%d': %s",
                             i,
                             g_strerror (-saved_errno));
                  continue;
                }

              if (g_strcmp0 (class, "greeter") == 0)
                {
                  local_session_id = g_strdup (sessions[i]);
                  break;
                }
            }

          if (!local_session_id)
            {
              g_set_error (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_FOUND,
                           "Couldn't find a session or a greeter session for user %d",
                           getuid ());
              return FALSE;
            }
        }
      else
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_NOT_FOUND,
                       "Couldn't get display for user %d: %s",
                       getuid (),
                       g_strerror (-saved_errno));
          return FALSE;
        }
    }

  /* sd_uid_get_display will return any session if there is no graphical
   * one, so let's check it really is graphical. */
  saved_errno = sd_session_get_type (local_session_id, &type);
  if (saved_errno < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_FOUND,
                   "Couldn't get type for session '%s': %s",
                   local_session_id,
                   g_strerror (-saved_errno));
      return FALSE;
    }

  if (!g_strv_contains (graphical_session_types, type))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_FOUND,
                   "Session '%s' is not a graphical session (type: '%s')",
                   local_session_id,
                   type);
      return FALSE;
    }

    /* and display sessions can be 'closing' if they are logged out but
     * some processes are lingering; we shouldn't consider these */
    saved_errno = sd_session_get_state (local_session_id, &state);
    if (saved_errno < 0)
      {
        g_set_error (error,
                     G_IO_ERROR,
                     G_IO_ERROR_NOT_FOUND,
                     "Couldn't get state for session '%s': %s",
                     local_session_id,
                     g_strerror (-saved_errno));
        return FALSE;
      }

    if (!g_strv_contains (active_states, state))
      {
         g_set_error (error,
                         G_IO_ERROR,
                         G_IO_ERROR_NOT_FOUND,
                         "Session '%s' is not active",
                         local_session_id);
         return FALSE;
      }

  *session_id = g_steal_pointer (&local_session_id);

  return TRUE;
}

static MetaDBusLogin1Session *
get_session_proxy (const char    *fallback_session_id,
                   GCancellable  *cancellable,
                   GError       **error)
{
  g_autofree char *proxy_path = NULL;
  g_autofree char *session_id = NULL;
  g_autoptr (GError) local_error = NULL;
  GDBusProxyFlags flags;
  MetaDBusLogin1Session *session_proxy;

  if (!find_systemd_session (&session_id, &local_error))
    {
      if (fallback_session_id)
        {
          meta_topic (META_DEBUG_BACKEND,
                      "Failed to get seat ID: %s, using fallback (%s)",
                      local_error->message, fallback_session_id);
          g_clear_error (&local_error);
          session_id = g_strdup (fallback_session_id);
        }
      else
        {
          g_propagate_prefixed_error (error,
                                      g_steal_pointer (&local_error),
                                      "Could not get session ID: ");
          return NULL;
        }
    }

  proxy_path =
    get_escaped_dbus_path ("/org/freedesktop/login1/session", session_id);

  flags = G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START;
  session_proxy =
    meta_dbus_login1_session_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                     flags,
                                                     "org.freedesktop.login1",
                                                     proxy_path,
                                                     cancellable,
                                                     error);
  if (!session_proxy)
    g_prefix_error(error, "Could not get session proxy: ");

  return session_proxy;
}

static MetaDBusLogin1Seat *
get_seat_proxy (char          *seat_id,
                GCancellable  *cancellable,
                GError       **error)
{
  g_autofree char *seat_proxy_path =
    get_escaped_dbus_path ("/org/freedesktop/login1/seat", seat_id);
  GDBusProxyFlags flags;
  MetaDBusLogin1Seat *seat;

  flags = G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START;
  seat =
    meta_dbus_login1_seat_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                  flags,
                                                  "org.freedesktop.login1",
                                                  seat_proxy_path,
                                                  cancellable,
                                                  error);
  if (!seat)
    g_prefix_error (error, "Could not get seat proxy: ");

  return seat;
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

static char *
get_seat_id (GError **error)
{
  g_autoptr (GError) local_error = NULL;
  g_autofree char *session_id = NULL;
  char *seat_id = NULL;
  int r;

  if (!find_systemd_session (&session_id, &local_error))
    {
      g_propagate_prefixed_error (error,
                                  g_steal_pointer (&local_error),
                                  "Could not get session ID: ");
      return NULL;
    }

  r = sd_session_get_seat (session_id, &seat_id);
  if (r < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_FOUND,
                   "Could not get seat for session: %s", g_strerror (-r));
      return NULL;
    }

  return seat_id;
}

MetaDBusLogin1Session *
meta_launcher_get_session_proxy (MetaLauncher *launcher)
{
  return launcher->session_proxy;
}

MetaLauncher *
meta_launcher_new (MetaBackend  *backend,
                   const char   *fallback_session_id,
                   const char   *fallback_seat_id,
                   GError      **error)
{
  g_autoptr (MetaLauncher) launcher = NULL;
  g_autoptr (MetaDBusLogin1Session) session_proxy = NULL;
  g_autoptr (MetaDBusLogin1Seat) seat_proxy = NULL;
  g_autoptr (GError) local_error = NULL;
  g_autofree char *seat_id = NULL;
  gboolean have_control = FALSE;

  seat_id = get_seat_id (&local_error);
  if (!seat_id)
    {
      if (fallback_seat_id)
        {
          meta_topic (META_DEBUG_BACKEND,
                      "Failed to get seat ID: %s, using fallback (%s)",
                      local_error->message, fallback_seat_id);
          g_clear_error (&local_error);
          seat_id = g_strdup (fallback_seat_id);
        }
    }

  if (seat_id)
    {
      seat_proxy = get_seat_proxy (seat_id, NULL, error);
      if (!seat_proxy)
        return NULL;
    }

  session_proxy = get_session_proxy (fallback_session_id, NULL, error);
  if (!session_proxy)
    return NULL;

  if (!meta_dbus_login1_session_call_take_control_sync (session_proxy,
                                                        FALSE,
                                                        NULL,
                                                        &local_error))
    {
      meta_topic (META_DEBUG_BACKEND,
                  "Failed to take control of the session: %s",
                  local_error->message);
      g_clear_error (&local_error);
    }
  else
    {
      have_control = TRUE;
    }

  launcher = g_object_new (META_TYPE_LAUNCHER, NULL);
  launcher->backend = backend;
  launcher->session_proxy = g_steal_pointer (&session_proxy);
  launcher->session_active = TRUE;
  launcher->have_control = have_control;
  launcher->seat_proxy = g_steal_pointer (&seat_proxy);
  launcher->seat_id = g_steal_pointer (&seat_id);

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
meta_launcher_is_session_controller (MetaLauncher *launcher)
{
  return launcher->have_control;
}

MetaBackend *
meta_launcher_get_backend (MetaLauncher *launcher)
{
  return launcher->backend;
}
