/*
 * Copyright (C) 2013-2019 Red Hat, Inc.
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

#include "backends/meta-session.h"

#include <errno.h>

#ifdef HAVE_LOGIND
#include <systemd/sd-login.h>
#endif

#include "backends/meta-dbus-utils.h"

#include "meta-dbus-login1.h"

enum
{
  IS_ACTIVE_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _MetaSessionPrivate
{
  GObject parent;

  Login1Session *session_proxy;

  gboolean is_active;
} MetaSessionPrivate;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaSession, meta_session, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (MetaSession)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

#ifdef HAVE_LOGIND
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

  /*
   * If we are in a logind session, we can trust that value, so use it. This
   * happens for example when you run mutter directly from a VT but when
   * systemd starts us we will not be in a logind session.
   */
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
      /* No session, maybe there's a greeter session. */
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

  /*
   * sd_uid_get_display will return any session if there is no graphical
   * one, so let's check it really is graphical.
   */
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

    /*
     * Display sessions can be 'closing' if they are logged out but some
     * processes are lingering; we shouldn't consider these.
     */
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

static Login1Session *
get_session_proxy (GCancellable  *cancellable,
                   GError       **error)
{
  g_autofree char *proxy_path = NULL;
  g_autofree char *session_id = NULL;
  g_autoptr (GError) local_error = NULL;
  Login1Session *session_proxy;

  if (!find_systemd_session (&session_id, &local_error))
    {
      g_propagate_prefixed_error (error,
                                  g_steal_pointer (&local_error),
                                  "Could not get session ID: ");
      return NULL;
    }

  proxy_path = meta_get_escaped_dbus_path ("/org/freedesktop/login1/session",
                                           session_id);

  session_proxy =
    login1_session_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                           G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                           "org.freedesktop.login1",
                                           proxy_path,
                                           cancellable, error);
  if (!session_proxy)
    g_prefix_error (error, "Could not get session proxy: ");

  return session_proxy;
}

gboolean
meta_session_is_active (MetaSession *session)
{
  MetaSessionPrivate *priv = meta_session_get_instance_private (session);

  return priv->is_active;
}

static void
sync_active (MetaSession *session)
{
  MetaSessionPrivate *priv = meta_session_get_instance_private (session);
  gboolean is_active;

  is_active = login1_session_get_active (LOGIN1_SESSION (priv->session_proxy));

  if (is_active == priv->is_active)
    return;

  priv->is_active = is_active;
  g_signal_emit (session, signals[IS_ACTIVE_CHANGED], 0);
}

static void
on_active_changed (Login1Session *session_proxy,
                   GParamSpec    *pspec,
                   gpointer       user_data)
{
  MetaSession *session = META_SESSION (user_data);

  sync_active (session);
}
#endif /* HAVE_LOGIND */

static gboolean
meta_session_initable_init (GInitable     *initable,
                            GCancellable  *cancellable,
                            GError       **error)
{
#ifdef HAVE_LOGIND
  MetaSession *session = META_SESSION (initable);
  MetaSessionPrivate *priv = meta_session_get_instance_private (session);
  g_autoptr (Login1Session) session_proxy = NULL;
  g_autoptr (Login1Seat) seat_proxy = NULL;

  session_proxy = get_session_proxy (cancellable, error);
  if (!session_proxy)
    return FALSE;

  priv->session_proxy = g_steal_pointer (&session_proxy);

  g_signal_connect (priv->session_proxy, "notify::active",
                    G_CALLBACK (on_active_changed),
                    session);
  priv->is_active = TRUE;
  sync_active (session);

  return TRUE;
#else /* HAVE_LOGIND */
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "Missing build time logind support");
  return FALSE;
#endif /* HAVE_LOGIND */
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = meta_session_initable_init;
}

static void
meta_session_init (MetaSession *session)
{
}

static void
meta_session_class_init (MetaSessionClass *klass)
{
  signals[IS_ACTIVE_CHANGED] =
    g_signal_new ("is-active-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}
