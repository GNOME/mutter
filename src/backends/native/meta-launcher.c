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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
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
#include "backends/native/dbus-utils.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-clutter-backend-native.h"
#include "backends/native/meta-cursor-renderer-native.h"
#include "backends/native/meta-input-thread.h"
#include "backends/native/meta-renderer-native.h"
#include "clutter/clutter.h"

#include "meta-dbus-login1.h"

struct _MetaLauncher
{
  MetaDbusLogin1Session *session_proxy;
  MetaDbusLogin1Seat *seat_proxy;
  char *seat_id;

  gboolean session_active;
};

const char *
meta_launcher_get_seat_id (MetaLauncher *launcher)
{
  return launcher->seat_id;
}

static gboolean
find_systemd_session (gchar **session_id,
                      GError **error)
{
  const gchar * const graphical_session_types[] = { "wayland", "x11", "mir", NULL };
  const gchar * const active_states[] = { "active", "online", NULL };
  g_autofree gchar *class = NULL;
  g_autofree gchar *local_session_id = NULL;
  g_autofree gchar *type = NULL;
  g_autofree gchar *state = NULL;
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

static MetaDbusLogin1Session *
get_session_proxy (const char    *fallback_session_id,
                   GCancellable  *cancellable,
                   GError       **error)
{
  g_autofree char *proxy_path = NULL;
  g_autofree char *session_id = NULL;
  g_autoptr (GError) local_error = NULL;
  GDBusProxyFlags flags;
  MetaDbusLogin1Session *session_proxy;

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

  proxy_path = get_escaped_dbus_path ("/org/freedesktop/login1/session", session_id);

  flags = G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START;
  session_proxy =
    meta_dbus_login1_session_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                     flags,
                                                     "org.freedesktop.login1",
                                                     proxy_path,
                                                     cancellable, error);
  if (!session_proxy)
    g_prefix_error(error, "Could not get session proxy: ");

  return session_proxy;
}

static MetaDbusLogin1Seat *
get_seat_proxy (gchar        *seat_id,
                GCancellable *cancellable,
                GError      **error)
{
  g_autofree char *seat_proxy_path = get_escaped_dbus_path ("/org/freedesktop/login1/seat", seat_id);
  GDBusProxyFlags flags;
  MetaDbusLogin1Seat *seat;

  flags = G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START;
  seat =
    meta_dbus_login1_seat_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                  flags,
                                                  "org.freedesktop.login1",
                                                  seat_proxy_path,
                                                  cancellable, error);
  if (!seat)
    g_prefix_error(error, "Could not get seat proxy: ");

  return seat;
}

static void
sync_active (MetaLauncher *self)
{
  MetaBackend *backend = meta_get_backend ();
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaDbusLogin1Session *session_proxy = self->session_proxy;
  gboolean active;

  active = meta_dbus_login1_session_get_active (session_proxy);
  if (active == self->session_active)
    return;

  self->session_active = active;

  if (active)
    meta_backend_native_resume (backend_native);
  else
    meta_backend_native_pause (backend_native);
}

static void
on_active_changed (MetaDbusLogin1Session *session,
                   GParamSpec            *pspec,
                   gpointer               user_data)
{
  MetaLauncher *self = user_data;
  sync_active (self);
}

static gchar *
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

MetaDbusLogin1Session *
meta_launcher_get_session_proxy (MetaLauncher *launcher)
{
  return launcher->session_proxy;
}

MetaLauncher *
meta_launcher_new (const char  *fallback_session_id,
                   const char  *fallback_seat_id,
                   GError     **error)
{
  MetaLauncher *self = NULL;
  g_autoptr (MetaDbusLogin1Session) session_proxy = NULL;
  g_autoptr (MetaDbusLogin1Seat) seat_proxy = NULL;
  g_autoptr (GError) local_error = NULL;
  g_autofree char *seat_id = NULL;
  gboolean have_control = FALSE;

  session_proxy = get_session_proxy (fallback_session_id, NULL, error);
  if (!session_proxy)
    goto fail;

  if (!meta_dbus_login1_session_call_take_control_sync (session_proxy,
                                                        FALSE,
                                                        NULL,
                                                        error))
    {
      g_prefix_error (error, "Could not take control: ");
      goto fail;
    }

  have_control = TRUE;

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
      else
        {
          g_propagate_error (error, local_error);
          goto fail;
        }
    }

  seat_proxy = get_seat_proxy (seat_id, NULL, error);
  if (!seat_proxy)
    goto fail;

  self = g_new0 (MetaLauncher, 1);
  self->session_proxy = g_object_ref (session_proxy);
  self->seat_proxy = g_object_ref (seat_proxy);
  self->seat_id = g_steal_pointer (&seat_id);
  self->session_active = TRUE;

  g_signal_connect (self->session_proxy, "notify::active", G_CALLBACK (on_active_changed), self);

  return self;

 fail:
  if (have_control)
    {
      meta_dbus_login1_session_call_release_control_sync (session_proxy,
                                                          NULL, NULL);
    }
  return NULL;
}

void
meta_launcher_free (MetaLauncher *self)
{
  g_free (self->seat_id);
  g_object_unref (self->seat_proxy);
  g_object_unref (self->session_proxy);
  g_free (self);
}

gboolean
meta_launcher_activate_vt (MetaLauncher  *launcher,
                           signed char    vt,
                           GError       **error)
{
  return meta_dbus_login1_seat_call_switch_to_sync (launcher->seat_proxy, vt,
                                                    NULL, error);
}
