/*
 * Copyright (C) 2024 Red Hat
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

#include "wayland/meta-wayland-xdg-session.h"

#include <glib-object.h>

#include "wayland/meta-wayland-xdg-session-state.h"
#include "wayland/meta-wayland-xdg-shell.h"

#include "session-management-v1-server-protocol.h"


typedef struct _MetaWaylandXdgToplevelSession
{
  grefcount ref_count;
  MetaWaylandSurface *surface;
  struct wl_resource *resource;
  MetaWaylandXdgSession *session;
  char *name;
} MetaWaylandXdgToplevelSession;

enum
{
  DESTROYED,
  RESTORE_TOPLEVEL,
  SAVE_TOPLEVEL,
  REMOVE_TOPLEVEL,
  DELETE,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MetaWaylandXdgSession
{
  GObject parent;

  char *id;
  struct wl_resource *resource;
  GHashTable *toplevels; /* name -> MetaWaylandXdgToplevelSession */
};

G_DEFINE_FINAL_TYPE (MetaWaylandXdgSession,
                     meta_wayland_xdg_session,
                     G_TYPE_OBJECT)

static void on_window_unmanaging (MetaWindow                    *window,
                                  MetaWaylandXdgToplevelSession *toplevel_session);

static MetaWaylandXdgToplevelSession *
meta_wayland_xdg_toplevel_session_ref (MetaWaylandXdgToplevelSession *toplevel_session)
{
  g_ref_count_inc (&toplevel_session->ref_count);
  return toplevel_session;
}

static void
meta_wayland_xdg_toplevel_session_unref (MetaWaylandXdgToplevelSession *toplevel_session)
{
  if (g_ref_count_dec (&toplevel_session->ref_count))
    {
      MetaWindow *window = NULL;
      MetaWaylandSurface *surface = toplevel_session->surface;

      if (surface)
        window = meta_wayland_surface_get_toplevel_window (surface);

      if (window)
        {
          g_signal_handlers_disconnect_by_func (window,
                                                on_window_unmanaging,
                                                toplevel_session);
        }

      g_free (toplevel_session->name);
      g_free (toplevel_session);
    }
}

static void
xdg_toplevel_session_destroy (struct wl_client   *wl_client,
                              struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_toplevel_session_remove (struct wl_client   *wl_client,
                             struct wl_resource *resource)
{
  MetaWaylandXdgToplevelSession *toplevel_session =
    wl_resource_get_user_data (resource);
  MetaWaylandXdgSession *session = toplevel_session->session;

  if (session)
    {
      g_signal_emit (session, signals[REMOVE_TOPLEVEL], 0, toplevel_session->name);
      g_hash_table_remove (session->toplevels, toplevel_session->name);
    }

  wl_resource_destroy (resource);
}

static const struct xx_toplevel_session_v1_interface meta_xdg_toplevel_session_interface = {
  xdg_toplevel_session_destroy,
  xdg_toplevel_session_remove,
};

static void
xdg_toplevel_session_destructor (struct wl_resource *resource)
{
  MetaWaylandXdgToplevelSession *toplevel_session =
    wl_resource_get_user_data (resource);

  meta_wayland_xdg_toplevel_session_unref (toplevel_session);
}

static MetaWaylandXdgToplevelSession *
meta_wayland_xdg_toplevel_session_new (MetaWaylandXdgSession *xdg_session,
                                       MetaWaylandSurface    *surface,
                                       const char            *name,
                                       struct wl_client      *wl_client,
                                       uint32_t               version,
                                       uint32_t               id)
{
  MetaWaylandXdgToplevelSession *toplevel_session;

  toplevel_session = g_new0 (MetaWaylandXdgToplevelSession, 1);
  g_ref_count_init (&toplevel_session->ref_count);
  toplevel_session->surface = surface;
  toplevel_session->session = xdg_session;
  toplevel_session->name = g_strdup (name);
  toplevel_session->resource =
    wl_resource_create (wl_client,
                        &xx_toplevel_session_v1_interface,
                        version, id);
  wl_resource_set_implementation (toplevel_session->resource,
                                  &meta_xdg_toplevel_session_interface,
                                  meta_wayland_xdg_toplevel_session_ref (toplevel_session),
                                  xdg_toplevel_session_destructor);

  return toplevel_session;
}

static void
meta_wayland_xdg_toplevel_session_emit_restored (MetaWaylandXdgToplevelSession *toplevel_session)
{
  MetaWaylandXdgToplevel *xdg_toplevel =
    META_WAYLAND_XDG_TOPLEVEL (toplevel_session->surface->role);
  struct wl_resource *xdg_toplevel_resource =
    meta_wayland_xdg_toplevel_get_resource (xdg_toplevel);

  xx_toplevel_session_v1_send_restored (toplevel_session->resource,
                                        xdg_toplevel_resource);
}

static void
meta_wayland_xdg_session_dispose (GObject *object)
{
  MetaWaylandXdgSession *session = META_WAYLAND_XDG_SESSION (object);

  g_clear_pointer (&session->id, g_free);
  g_clear_pointer (&session->toplevels, g_hash_table_unref);

  G_OBJECT_CLASS (meta_wayland_xdg_session_parent_class)->dispose (object);
}

static void
meta_wayland_xdg_session_class_init (MetaWaylandXdgSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_wayland_xdg_session_dispose;

  signals[DESTROYED] =
    g_signal_new ("destroyed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);

  signals[RESTORE_TOPLEVEL] =
    g_signal_new ("restore-toplevel",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  g_signal_accumulator_true_handled,
                  NULL,
                  NULL,
                  G_TYPE_BOOLEAN, 2,
                  META_TYPE_WAYLAND_XDG_TOPLEVEL,
                  G_TYPE_STRING);
  signals[SAVE_TOPLEVEL] =
    g_signal_new ("save-toplevel",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 3,
                  META_TYPE_WAYLAND_XDG_TOPLEVEL,
                  G_TYPE_STRING,
                  META_TYPE_WINDOW);
  signals[REMOVE_TOPLEVEL] =
    g_signal_new ("remove-toplevel",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_TYPE_STRING);
  signals[DELETE] =
    g_signal_new ("delete",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_wayland_xdg_session_init (MetaWaylandXdgSession *session)
{
}

static void
xdg_session_destroy (struct wl_client   *wl_client,
                     struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_session_remove (struct wl_client   *wl_client,
                    struct wl_resource *resource)
{
  MetaWaylandXdgSession *session =
    META_WAYLAND_XDG_SESSION (wl_resource_get_user_data (resource));

  g_signal_emit (session, signals[DELETE], 0);

  wl_resource_destroy (resource);
}

static void
on_window_unmanaging (MetaWindow                    *window,
                      MetaWaylandXdgToplevelSession *toplevel_session)
{
  MetaWaylandXdgSession *session = toplevel_session->session;

  if (session)
    {
      MetaWaylandXdgToplevel *xdg_toplevel =
        META_WAYLAND_XDG_TOPLEVEL (toplevel_session->surface->role);

      g_signal_emit (session, signals[SAVE_TOPLEVEL], 0,
                     xdg_toplevel, toplevel_session->name, window);
    }

  toplevel_session->surface = NULL;
}

static void
xdg_session_add_toplevel (struct wl_client   *wl_client,
                          struct wl_resource *resource,
                          uint32_t            id,
                          struct wl_resource *toplevel_resource,
                          const char         *name)
{
  MetaWaylandXdgSession *session =
    META_WAYLAND_XDG_SESSION (wl_resource_get_user_data (resource));
  MetaWaylandXdgToplevel *xdg_toplevel =
    wl_resource_get_user_data (toplevel_resource);
  MetaWaylandSurfaceRole *surface_role;
  MetaWaylandSurface *surface;
  MetaWaylandXdgToplevelSession *toplevel_session;
  MetaWindow *window;

  if (g_hash_table_lookup (session->toplevels, name))
    {
      wl_resource_post_error (resource, XX_SESSION_V1_ERROR_NAME_IN_USE,
                              "Name of toplevel was already in use");
      return;
    }

  surface_role = META_WAYLAND_SURFACE_ROLE (xdg_toplevel);
  surface = meta_wayland_surface_role_get_surface (surface_role);
  toplevel_session =
    meta_wayland_xdg_toplevel_session_new (session, surface, name,
                                           wl_client,
                                           wl_resource_get_version (resource),
                                           id);
  g_hash_table_insert (session->toplevels, g_strdup (name), toplevel_session);

  window = meta_wayland_surface_get_toplevel_window (surface);
  if (window)
    {
      g_signal_connect (window, "unmanaging",
                        G_CALLBACK (on_window_unmanaging), toplevel_session);
    }
}

static void
xdg_session_restore_toplevel (struct wl_client   *wl_client,
                              struct wl_resource *resource,
                              uint32_t            id,
                              struct wl_resource *toplevel_resource,
                              const char         *name)
{
  MetaWaylandXdgSession *session =
    META_WAYLAND_XDG_SESSION (wl_resource_get_user_data (resource));
  MetaWaylandXdgToplevel *xdg_toplevel =
    wl_resource_get_user_data (toplevel_resource);
  MetaWaylandSurfaceRole *surface_role;
  MetaWaylandSurface *surface;
  MetaWaylandXdgToplevelSession *toplevel_session;
  MetaWindow *window;
  gboolean restored = FALSE;

  if (g_hash_table_lookup (session->toplevels, name))
    {
      wl_resource_post_error (resource, XX_SESSION_V1_ERROR_NAME_IN_USE,
                              "Name of toplevel was already in use");
      return;
    }

  surface_role = META_WAYLAND_SURFACE_ROLE (xdg_toplevel);
  surface = meta_wayland_surface_role_get_surface (surface_role);
  if (meta_wayland_surface_has_initial_commit (surface))
    {
      wl_resource_post_error (resource, XX_SESSION_V1_ERROR_ALREADY_MAPPED,
                              "Tried to restore an already mapped toplevel");
      return;
    }

  toplevel_session =
    meta_wayland_xdg_toplevel_session_new (session, surface, name,
                                           wl_client,
                                           wl_resource_get_version (resource),
                                           id);
  g_hash_table_insert (session->toplevels, g_strdup (name), toplevel_session);

  window = meta_wayland_surface_get_toplevel_window (surface);
  if (window)
    {
      g_signal_connect (window, "unmanaging",
                        G_CALLBACK (on_window_unmanaging), toplevel_session);
    }

  g_signal_emit (session,
                 signals[RESTORE_TOPLEVEL], 0,
                 xdg_toplevel, name, &restored);

  if (restored)
    meta_wayland_xdg_toplevel_session_emit_restored (toplevel_session);
}

static const struct xx_session_v1_interface meta_xdg_session_interface = {
  xdg_session_destroy,
  xdg_session_remove,
  xdg_session_add_toplevel,
  xdg_session_restore_toplevel,
};

static void
xdg_session_destructor (struct wl_resource *resource)
{
  MetaWaylandXdgSession *session =
    META_WAYLAND_XDG_SESSION (wl_resource_get_user_data (resource));
  GHashTableIter iter;
  gpointer value;

  g_signal_emit (session, signals[DESTROYED], 0);

  g_hash_table_iter_init (&iter, session->toplevels);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      MetaWaylandXdgToplevelSession *toplevel_session = value;

      toplevel_session->session = NULL;
    }

  g_object_unref (session);
}

MetaWaylandXdgSession *
meta_wayland_xdg_session_new (MetaWaylandXdgSessionState *session_state,
                              struct wl_client           *wl_client,
                              uint32_t                    version,
                              uint32_t                    id)
{
  g_autoptr (MetaWaylandXdgSession) session = NULL;

  session = g_object_new (META_TYPE_WAYLAND_XDG_SESSION, NULL);
  session->id =
    g_strdup (meta_session_state_get_name (META_SESSION_STATE (session_state)));
  session->toplevels =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           g_free,
                           (GDestroyNotify) meta_wayland_xdg_toplevel_session_unref);
  session->resource = wl_resource_create (wl_client,
                                          &xx_session_v1_interface,
                                          version, id);
  wl_resource_set_implementation (session->resource,
                                  &meta_xdg_session_interface,
                                  g_object_ref (session),
                                  xdg_session_destructor);

  return g_steal_pointer (&session);
}

const char *
meta_wayland_xdg_session_get_id (MetaWaylandXdgSession *session)
{
  return session->id;
}

void
meta_wayland_xdg_session_emit_created (MetaWaylandXdgSession *session)
{
  xx_session_v1_send_created (session->resource, session->id);
}

void
meta_wayland_xdg_session_emit_replaced (MetaWaylandXdgSession *session)
{
  xx_session_v1_send_replaced (session->resource);
}

void
meta_wayland_xdg_session_emit_restored (MetaWaylandXdgSession *session)
{
  xx_session_v1_send_restored (session->resource);
}

gboolean
meta_wayland_xdg_session_is_same_client (MetaWaylandXdgSession *session,
                                         struct wl_client      *client)
{
  return wl_resource_get_client (session->resource) == client;
}
