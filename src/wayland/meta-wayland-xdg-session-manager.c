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

#include "wayland/meta-wayland-xdg-session-manager.h"

#include <glib.h>

#include "wayland/meta-wayland-xdg-shell.h"
#include "wayland/meta-wayland-xdg-session.h"
#include "wayland/meta-wayland-private.h"
#include "core/meta-debug-control-private.h"
#include "core/meta-session-manager.h"

#include "session-management-v1-server-protocol.h"

#define TIMEOUT_DELAY_SECONDS 3

typedef struct _MetaWaylandXdgSessionManager
{
  MetaWaylandCompositor *compositor;
  struct wl_global *global;

  GHashTable *sessions;
  GHashTable *session_states;
  guint save_timeout_id;
} MetaWaylandXdgSessionManager;

static void xdg_session_manager_remove_session (MetaWaylandXdgSessionManager *session_manager,
                                                MetaWaylandXdgSession        *session);

static void
xdg_session_manager_destroy (struct wl_client   *client,
                             struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static gboolean
on_restore_toplevel (MetaWaylandXdgSession        *session,
                     MetaWaylandXdgToplevel       *xdg_toplevel,
                     const char                   *name,
                     MetaWaylandXdgSessionManager *xdg_session_manager)
{
  MetaContext *context =
    meta_wayland_compositor_get_context (xdg_session_manager->compositor);
  MetaSessionManager *session_manager =
    meta_context_get_session_manager (context);
  MetaSessionState *session_state;
  MetaWaylandSurface *surface;
  MetaWindow *window;

  session_state =
    meta_session_manager_get_session (META_SESSION_MANAGER (session_manager),
                                      META_TYPE_WAYLAND_XDG_SESSION_STATE,
                                      meta_wayland_xdg_session_get_id (session));

  surface =
    meta_wayland_surface_role_get_surface (META_WAYLAND_SURFACE_ROLE (xdg_toplevel));
  if (!surface)
    return FALSE;

  window = meta_wayland_surface_get_toplevel_window (surface);
  if (!window)
    return FALSE;

  if (!meta_session_state_restore_window (session_state, name, window))
    return FALSE;

  meta_wayland_xdg_toplevel_set_hint_restored (xdg_toplevel);

  return TRUE;
}

static void
save_async_cb (GObject      *source,
               GAsyncResult *res,
               gpointer      user_data)
{
  MetaSessionManager *manager = META_SESSION_MANAGER (source);
  g_autoptr (GError) error = NULL;

  if (!meta_session_manager_save_finish (manager, res, &error))
    g_message ("Could not save session data: %s", error->message);
}

static void
on_save_idle_cb (gpointer user_data)
{
  MetaWaylandXdgSessionManager *xdg_session_manager = user_data;
  MetaContext *context =
    meta_wayland_compositor_get_context (xdg_session_manager->compositor);
  MetaSessionManager *session_manager =
    meta_context_get_session_manager (context);

  meta_session_manager_save (session_manager, save_async_cb, xdg_session_manager);
  xdg_session_manager->save_timeout_id = 0;
}

static void
on_save_toplevel (MetaWaylandXdgSession        *session,
                  MetaWaylandXdgToplevel       *xdg_toplevel,
                  const char                   *name,
                  MetaWindow                   *window,
                  MetaWaylandXdgSessionManager *xdg_session_manager)
{
  MetaContext *context =
    meta_wayland_compositor_get_context (xdg_session_manager->compositor);
  MetaSessionManager *session_manager =
    meta_context_get_session_manager (context);
  MetaSessionState *session_state;

  session_state =
    meta_session_manager_get_session (META_SESSION_MANAGER (session_manager),
                                      META_TYPE_WAYLAND_XDG_SESSION_STATE,
                                      meta_wayland_xdg_session_get_id (session));

  meta_session_state_save_window (session_state, name, window);

  if (xdg_session_manager->save_timeout_id == 0)
    {
      xdg_session_manager->save_timeout_id =
        g_timeout_add_seconds_once (TIMEOUT_DELAY_SECONDS, on_save_idle_cb,
                                    xdg_session_manager);
    }
}

static void
on_remove_toplevel (MetaWaylandXdgSession        *session,
                    const char                   *name,
                    MetaWaylandXdgSessionManager *xdg_session_manager)
{
  MetaContext *context =
    meta_wayland_compositor_get_context (xdg_session_manager->compositor);
  MetaSessionManager *session_manager =
    meta_context_get_session_manager (context);
  MetaSessionState *session_state;

  session_state =
    meta_session_manager_get_session (session_manager,
                                      META_TYPE_WAYLAND_XDG_SESSION_STATE,
                                      meta_wayland_xdg_session_get_id (session));

  meta_session_state_remove_window (session_state, name);
}

static void
on_session_destroyed (MetaWaylandXdgSession        *session,
                      MetaWaylandXdgSessionManager *session_manager)
{
  xdg_session_manager_remove_session (session_manager, session);
}

static void
on_session_delete (MetaWaylandXdgSession        *xdg_session,
                   MetaWaylandXdgSessionManager *xdg_session_manager)
{
  MetaContext *context =
    meta_wayland_compositor_get_context (xdg_session_manager->compositor);
  MetaSessionManager *session_manager =
    meta_context_get_session_manager (context);
  const char *session_id = meta_wayland_xdg_session_get_id (xdg_session);

  g_hash_table_remove (xdg_session_manager->session_states, session_id);
  meta_session_manager_delete_session (session_manager, session_id);
}

static void
xdg_session_manager_remove_session (MetaWaylandXdgSessionManager *session_manager,
                                    MetaWaylandXdgSession        *session)
{
  const char *session_id = meta_wayland_xdg_session_get_id (session);

  g_signal_handlers_disconnect_by_func (session,
                                        on_session_destroyed,
                                        session_manager);
  g_signal_handlers_disconnect_by_func (session,
                                        on_restore_toplevel,
                                        session_manager);
  g_signal_handlers_disconnect_by_func (session,
                                        on_save_toplevel,
                                        session_manager);
  g_signal_handlers_disconnect_by_func (session,
                                        on_remove_toplevel,
                                        session_manager);
  g_signal_handlers_disconnect_by_func (session,
                                        on_session_delete,
                                        session_manager);

  g_hash_table_remove (session_manager->sessions, session_id);
}

static char *
generate_session_id (MetaWaylandXdgSessionManager *session_manager)
{
  while (TRUE)
    {
      g_autofree char *id = NULL;

      id = g_uuid_string_random ();
      if (!g_hash_table_lookup (session_manager->sessions, id))
        return g_steal_pointer (&id);
    }
}

static void
xdg_session_manager_get_session (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 uint32_t            id,
                                 uint32_t            reason_value,
                                 const char         *session_id)
{
  MetaWaylandXdgSessionManager *xdg_session_manager =
    wl_resource_get_user_data (resource);
  MetaContext *context =
    meta_wayland_compositor_get_context (xdg_session_manager->compositor);
  MetaSessionManager *session_manager =
    meta_context_get_session_manager (context);
  g_autoptr (MetaSessionState) session_state = NULL;
  g_autoptr (MetaWaylandXdgSession) session = NULL;
  g_autofree char *name = NULL, *stolen_name = NULL;
  gboolean created = FALSE;

  /* Unknown session ID is the same as NULL */
  if (session_id &&
      !meta_session_manager_get_session_exists (session_manager, session_id))
    session_id = NULL;

  if (session_id)
    {
      MetaWaylandXdgSession *prev_session;

      prev_session =
        g_hash_table_lookup (xdg_session_manager->sessions, session_id);

      if (prev_session)
        {
          if (meta_wayland_xdg_session_is_same_client (prev_session, client))
            {
              wl_resource_post_error (resource,
                                      XX_SESSION_MANAGER_V1_ERROR_IN_USE,
                                      "Session %s already in use",
                                      session_id);
              return;
            }

          /* Replace existing session */
          meta_wayland_xdg_session_emit_replaced (prev_session);
          xdg_session_manager_remove_session (xdg_session_manager,
                                              prev_session);
        }

      name = g_strdup (session_id);
    }
  else
    {
      name = generate_session_id (xdg_session_manager);
      created = TRUE;
    }

  if (!g_hash_table_steal_extended (xdg_session_manager->session_states,
                                    name,
                                    (gpointer *) &stolen_name,
                                    (gpointer *) &session_state))
    {
      session_state =
        meta_session_manager_get_session (session_manager,
                                          META_TYPE_WAYLAND_XDG_SESSION_STATE,
                                          name);
    }

  session = meta_wayland_xdg_session_new (META_WAYLAND_XDG_SESSION_STATE (session_state),
                                          client,
                                          wl_resource_get_version (resource),
                                          id);
  g_signal_connect (session, "destroyed",
                    G_CALLBACK (on_session_destroyed), xdg_session_manager);
  g_signal_connect (session, "restore-toplevel",
                    G_CALLBACK (on_restore_toplevel), xdg_session_manager);
  g_signal_connect (session, "save-toplevel",
                    G_CALLBACK (on_save_toplevel), xdg_session_manager);
  g_signal_connect (session, "remove-toplevel",
                    G_CALLBACK (on_remove_toplevel), xdg_session_manager);
  g_signal_connect (session, "delete",
                    G_CALLBACK (on_session_delete), xdg_session_manager);

  if (created)
    meta_wayland_xdg_session_emit_created (session);
  else
    meta_wayland_xdg_session_emit_restored (session);

  g_hash_table_insert (xdg_session_manager->sessions,
                       g_strdup (name),
                       session);

  g_hash_table_insert (xdg_session_manager->session_states,
                       g_strdup (name),
                       g_steal_pointer (&session_state));
}

static const struct xx_session_manager_v1_interface meta_xdg_session_manager_interface = {
  xdg_session_manager_destroy,
  xdg_session_manager_get_session,
};

static void
bind_session_manager (struct wl_client *client,
                      void             *data,
                      uint32_t          version,
                      uint32_t          id)
{
  MetaWaylandXdgSessionManager *session_manager = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &xx_session_manager_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &meta_xdg_session_manager_interface,
                                  session_manager, NULL);
}

static void
update_enabled (MetaWaylandXdgSessionManager *session_manager)
{
  MetaWaylandCompositor *compositor = session_manager->compositor;
  MetaDebugControl *debug_control =
    meta_context_get_debug_control (compositor->context);
  gboolean is_enabled;

  is_enabled =
    meta_debug_control_is_session_management_protocol_enabled (debug_control);

  if (is_enabled && !session_manager->global)
    {
      struct wl_display *wayland_display;

      wayland_display =
        meta_wayland_compositor_get_wayland_display (compositor);

      session_manager->global =
        wl_global_create (wayland_display,
                          &xx_session_manager_v1_interface,
                          META_XDG_SESSION_MANAGER_V1_VERSION,
                          session_manager, bind_session_manager);
      if (!session_manager->global)
        g_error ("Could not create session manager global");
    }
  else if (!is_enabled)
    {
      g_clear_pointer (&session_manager->global, wl_global_destroy);
    }
}

static MetaWaylandXdgSessionManager *
meta_wayland_session_manager_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandXdgSessionManager *session_manager;

  session_manager = g_new0 (MetaWaylandXdgSessionManager, 1);
  session_manager->compositor = compositor;

  session_manager->sessions =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           g_free, NULL);
  session_manager->session_states =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           g_free, g_object_unref);

  return session_manager;
}

static void
meta_wayland_session_manager_free (MetaWaylandXdgSessionManager *session_manager)
{
  g_clear_pointer (&session_manager->sessions, g_hash_table_unref);
  g_clear_pointer (&session_manager->session_states, g_hash_table_unref);
  g_clear_handle_id (&session_manager->save_timeout_id, g_source_remove);
  g_free (session_manager);
}

static void
on_protocol_enabled_changed (GObject    *object,
                             GParamSpec *pspec,
                             gpointer    user_data)
{
  MetaWaylandXdgSessionManager *session_manager = user_data;

  update_enabled (session_manager);
}

void
meta_wayland_xdg_session_management_init (MetaWaylandCompositor *compositor)
{
  MetaDebugControl *debug_control =
    meta_context_get_debug_control (compositor->context);

  compositor->session_manager = meta_wayland_session_manager_new (compositor);

  g_signal_connect (debug_control, "notify::session-management-protocol",
                    G_CALLBACK (on_protocol_enabled_changed),
                    compositor->session_manager);

  update_enabled (compositor->session_manager);
}

void
meta_wayland_xdg_session_management_finalize (MetaWaylandCompositor *compositor)
{
  MetaDebugControl *debug_control =
    meta_context_get_debug_control (compositor->context);

  g_signal_handlers_disconnect_by_func (debug_control,
                                        on_protocol_enabled_changed,
                                        compositor->session_manager);

  g_clear_pointer (&compositor->session_manager,
                   meta_wayland_session_manager_free);
}
