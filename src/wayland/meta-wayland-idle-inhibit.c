/*
 * Wayland Support
 *
 * Copyright (C) 2015 Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "wayland/meta-wayland-idle-inhibit.h"

#include <wayland-server.h>

#include "backends/meta-settings-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-backend-private.h"
#include "meta/meta-backend.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"

struct _MetaWaylandIdleInhibitor
{
  MetaWaylandSurface	  *surface;
  GDBusProxy 		  *session_proxy;
  guint			   cookie;
  gulong                   inhibit_idle_handler;
  gboolean                 idle_inhibited;
  struct wl_listener       surface_destroy_listener;
  GCancellable            *cancellable;
};

typedef struct _MetaWaylandIdleInhibitor MetaWaylandIdleInhibitor;

static void
inhibit_completed (GObject      *source,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  MetaWaylandIdleInhibitor *inhibitor = user_data;
  GVariant *ret;
  GError *error = NULL;

  ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
  if (!ret)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to inhibit: %s", error->message);
      g_error_free (error);
      return;
    }
  else
    {
      g_variant_get (ret, "(u)", &inhibitor->cookie);
      inhibitor->idle_inhibited = TRUE;
      meta_verbose ("Inhibit org.freedesktop.ScreenSaver cookie = %u\n",
                    inhibitor->cookie);
    }

  g_variant_unref (ret);
}

static void
uninhibit_completed (GObject      *source,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  MetaWaylandIdleInhibitor *inhibitor = user_data;
  GVariant *ret;
  GError *error = NULL;

  ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
  if (!ret)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to uninhibit: %s", error->message);
      g_error_free (error);
      return;
    }
  else
    {
      inhibitor->idle_inhibited = FALSE;
      meta_verbose ("UnInhibit org.freedesktop.ScreenSaver");
    }

  g_variant_unref (ret);
}

static void
visibility_changed_cb (MetaSurfaceActor         *actor,
                       MetaWaylandIdleInhibitor *inhibitor)
{
  if (!inhibitor->session_proxy)
    return;

  if (inhibitor->surface && meta_wayland_surface_get_actor (inhibitor->surface) != actor) {
    meta_verbose ("idle_inhibitor %p surface: %p, expected %p \n",
		  inhibitor, actor, meta_wayland_surface_get_actor (inhibitor->surface));
    return;
  }

  if (!meta_surface_actor_is_obscured (actor)) {
    if (!inhibitor->idle_inhibited) {
      g_dbus_proxy_call (G_DBUS_PROXY(inhibitor->session_proxy),
                         "Inhibit",
	                 g_variant_new ("(ss)", "mutter", "idle-inhibit"),
                         G_DBUS_CALL_FLAGS_NONE,
                         -1,
                         inhibitor->cancellable,
			 inhibit_completed,
			 inhibitor);
    }
  }

  else if (inhibitor->idle_inhibited) {
     g_dbus_proxy_call (G_DBUS_PROXY(inhibitor->session_proxy),
                        "UnInhibit",
                        g_variant_new ("(u)", inhibitor->cookie),
                        G_DBUS_CALL_FLAGS_NONE,
                        -1,
                        inhibitor->cancellable,
			uninhibit_completed,
			inhibitor);
  }

}

static void
inhibitor_proxy_completed (GObject      *source,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  MetaWaylandIdleInhibitor *inhibitor = user_data;
  GDBusProxy *proxy;
  GError *error = NULL;

  proxy = g_dbus_proxy_new_finish (res, &error);
  if (!proxy)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to obtain org.freedesktop.ScreenSaver proxy: %s", error->message);
      g_error_free (error);
      return;
    }

  inhibitor->session_proxy = proxy;

  /* as the surface has already been created, we check its visibility state in
   * the inhibitor initialization.
   */
  visibility_changed_cb (meta_wayland_surface_get_actor (inhibitor->surface),
                        inhibitor);
}

static void
idle_inhibit_manager_destroy (struct wl_client   *client,
                              struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
idle_inhibitor_destroy (struct wl_client   *client,
                        struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
idle_inhibitor_destructor (struct wl_resource *resource)
{
  MetaWaylandIdleInhibitor *inhibitor = wl_resource_get_user_data (resource);

  if (inhibitor && inhibitor->surface)
  g_signal_handler_disconnect (meta_wayland_surface_get_actor (inhibitor->surface),
                                   inhibitor->inhibit_idle_handler);

  /* Uninhibit when the inhibitor is destroyed */
  if (inhibitor->session_proxy && inhibitor->idle_inhibited)
    {
      meta_verbose ("UnInhibit org.freedesktop.ScreenSaver\n");
      g_dbus_proxy_call (G_DBUS_PROXY(inhibitor->session_proxy),
        "UnInhibit",
        g_variant_new ("(u)", inhibitor->cookie),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        inhibitor->cancellable,
	uninhibit_completed,
	inhibitor);
  }

}

static void
inhibitor_surface_destroyed (struct wl_listener *listener,
                             void               *data)
{
  MetaWaylandIdleInhibitor *inhibitor = wl_container_of (listener,
                                                         inhibitor,
                                                         surface_destroy_listener);
  inhibitor->surface = NULL;
}

static const struct zwp_idle_inhibitor_v1_interface
  meta_wayland_idle_inhibitor_interface = {
    idle_inhibitor_destroy,
 };

static void
idle_inhibit_manager_create_inhibitor (struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t            id,
			               struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandIdleInhibitor *inhibitor;
  struct wl_resource *inhibitor_resource;

  if (!surface)
    return;	  

  inhibitor_resource = wl_resource_create (client, &zwp_idle_inhibitor_v1_interface, 
                                           wl_resource_get_version (resource), id);

  if (inhibitor_resource == NULL) {
    wl_client_post_no_memory(client);
    return;
  }
  
  inhibitor = g_new0 (MetaWaylandIdleInhibitor, 1);
  inhibitor->cookie = 0;
  inhibitor->surface = surface;
  inhibitor->idle_inhibited = FALSE;


  inhibitor->inhibit_idle_handler =
    g_signal_connect (meta_wayland_surface_get_actor (surface), "obscured-changed",
                      G_CALLBACK (visibility_changed_cb),
                      inhibitor);

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.freedesktop.ScreenSaver",
                            "/org/freedesktop/ScreenSaver",
                            "org.freedesktop.ScreenSaver",
                            inhibitor->cancellable,
                            inhibitor_proxy_completed,
                            inhibitor);

  wl_resource_set_implementation (inhibitor_resource, &meta_wayland_idle_inhibitor_interface,
                                  inhibitor, idle_inhibitor_destructor);

  inhibitor->surface_destroy_listener.notify = inhibitor_surface_destroyed;
  wl_resource_add_destroy_listener (surface->resource,
                                    &inhibitor->surface_destroy_listener);
}


static const struct zwp_idle_inhibit_manager_v1_interface
  meta_wayland_idle_inhibit_manager_interface = {
    idle_inhibit_manager_destroy,
    idle_inhibit_manager_create_inhibitor,
  };

static void
bind_idle_inhibit (struct wl_client *client,
                   void *data,
                   uint32_t version,
                   uint32_t id)
{
  struct wl_resource *resource;
  meta_verbose ("%s called\n", __func__);

  resource = wl_resource_create (client,
                                 &zwp_idle_inhibit_manager_v1_interface,
                                 version, id);
  if (resource == NULL) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation (resource,
                                  &meta_wayland_idle_inhibit_manager_interface,
                                  NULL, NULL);

}

gboolean
meta_wayland_idle_inhibit_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                            &zwp_idle_inhibit_manager_v1_interface,
                            META_ZWP_IDLE_INHIBIT_V1_VERSION,
                            NULL,
                            bind_idle_inhibit) == NULL)
    return FALSE;
  return TRUE;
}
