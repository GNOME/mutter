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

#include <wayland-server.h>

#include "meta/meta-backend.h"
#include "backends/meta-settings-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"
#include "backends/meta-logical-monitor.h"
#include "src/backends/meta-backend-private.h"
#include "meta-wayland-idle-inhibit.h"
#include <dbus/dbus.h>

static void
inhibit_idle_cb (MetaSurfaceActor                  *actor,
                        MetaWaylandIdleInhibitor *inhibitor)
{
  GVariant *ret;	
  meta_verbose ("%s called actor %p\n", __func__, actor);

  if (!meta_surface_actor_should_inhibit_idle (actor))
    return;

  if (meta_surface_actor_is_idle_inhibited (actor))
    return;

  if (inhibitor->session_proxy) {
    meta_verbose ("%s called Inhibit org.freedesktop.ScreenSaver\n", __func__);
    ret = g_dbus_proxy_call_sync (G_DBUS_PROXY(inhibitor->session_proxy),
                               "Inhibit",
			       g_variant_new ("(ss)", "gnome-shell", "idle-inhibit"),
                               G_DBUS_CALL_FLAGS_NONE,
                               -1,
			       NULL,
			       NULL);
    g_variant_get (ret, "(u)", &inhibitor->cookie);
    meta_surface_actor_set_idle_inhibited (actor, true);
    meta_verbose ("%s called Inhibit org.freedesktop.ScreenSaver cookie = %u\n", __func__, inhibitor->cookie);
  }
  else
    meta_verbose ("%s called no proxy for  Inhibit org.freedesktop.ScreenSaver found\n", __func__);
}

static void
restore_idle_cb (MetaSurfaceActor                  *actor,
                       MetaWaylandIdleInhibitor *inhibitor)
{
  GVariant *ret;	
  meta_verbose ("%s called actor %p\n", __func__, actor);

  if (!meta_surface_actor_should_inhibit_idle (actor))
    return;
  if (!meta_surface_actor_is_idle_inhibited (actor))
    return;

  if (inhibitor->session_proxy) {
     meta_verbose ("%s call UnInhibit org.freedesktop.ScreenSaver\n", __func__);
     ret = g_dbus_proxy_call_sync (G_DBUS_PROXY(inhibitor->session_proxy),
                               "UnInhibit",
 			       g_variant_new ("(u)", inhibitor->cookie),
                               G_DBUS_CALL_FLAGS_NONE,
                               -1,
			       NULL,
			       NULL);
    meta_surface_actor_set_idle_inhibited (actor, false);
  }   
}

static void
idle_inhibit_manager_destroy (struct wl_client   *client,
                                            struct wl_resource *resource)
{
  meta_verbose ("%s called\n", __func__);
  //wl_resource_destroy (resource);
}

static void
idle_inhibitor_destroy (struct wl_client   *client,
                                            struct wl_resource *resource)
{
  meta_verbose ("%s called\n", __func__);
  MetaWaylandIdleInhibitor *inhibitor = wl_resource_get_user_data (resource);

  g_signal_handler_disconnect (inhibitor->surface->surface_actor,
                                   inhibitor->restore_idle_handler);

  g_signal_handler_disconnect (inhibitor->surface->surface_actor,
                                   inhibitor->inhibit_idle_handler);

  restore_idle_cb (inhibitor->surface->surface_actor, inhibitor);
  meta_surface_actor_set_should_inhibit_idle (inhibitor->surface->surface_actor, false);
  //wl_resource_destroy (resource);
}

static const struct zwp_idle_inhibitor_v1_interface idle_inhibitor_interface;

static void
idle_inhibit_manager_create_inhibitor (struct wl_client   *client,
                               struct wl_resource *resource,
                               uint32_t            id,
			       struct wl_resource *surface)
{
  MetaWaylandSurface *s = wl_resource_get_user_data (surface);
  MetaWaylandIdleInhibitor *inhibitor;
  struct wl_resource *cr;

  meta_verbose ("%s called with resource %p (surface %p) surface %p \n", __func__, resource, s, surface);

  if (!s)
    return;	  

  meta_verbose ("%s called\n", __func__);
  cr = wl_resource_create (client, &zwp_idle_inhibitor_v1_interface, 
		  		wl_resource_get_version (resource), id);

  if (cr == NULL) {
    wl_client_post_no_memory(client);
    return;
  }
  
  meta_verbose ("%s called\n", __func__);
  inhibitor = g_new0 (MetaWaylandIdleInhibitor, 1);
  inhibitor->cookie = 0;
  inhibitor->surface = s;
  meta_surface_actor_set_should_inhibit_idle (s->surface_actor, true);

  inhibitor->inhibit_idle_handler =
    g_signal_connect (s->surface_actor, "idle-inhibit",
                      G_CALLBACK (inhibit_idle_cb),
                      inhibitor);

  inhibitor->restore_idle_handler =
    g_signal_connect (s->surface_actor, "idle-restore",
                      G_CALLBACK (restore_idle_cb),
                      inhibitor);

  inhibitor->session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                  G_DBUS_PROXY_FLAGS_NONE,//0
                                  NULL,
                                  "org.freedesktop.ScreenSaver",
                                  "/org/freedesktop/ScreenSaver",
                                  "org.freedesktop.ScreenSaver",
                                  NULL,
                                  NULL);

  wl_resource_set_implementation (cr, &idle_inhibitor_interface, inhibitor, NULL /*idle_inhibitor_destroy*/);

}

static const struct zwp_idle_inhibitor_v1_interface idle_inhibitor_interface = {
  idle_inhibitor_destroy,
};

static const struct zwp_idle_inhibit_manager_v1_interface idle_inhibit_manager_interface = {
  idle_inhibit_manager_destroy,
  idle_inhibit_manager_create_inhibitor,
};

static void
bind_idle_inhibit (struct wl_client *client,
           void *data,
           guint32 version,
           guint32 id)
{
  //MetaWaylandIdleInhibitor *idle = data;
  struct wl_resource *resource;
  meta_verbose ("%s called\n", __func__);

  resource = wl_resource_create (client, &zwp_idle_inhibit_manager_v1_interface, version, id);
  if (resource == NULL) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation (resource, &idle_inhibit_manager_interface, NULL /*idle*/, NULL /*unbind_resource*/);

}

gboolean
meta_wayland_idle_inhibit_init (MetaWaylandCompositor *compositor)
{
  //gboolean error;
  meta_verbose ("%s called\n", __func__);
  if (wl_global_create (compositor->wayland_display,
                            &zwp_idle_inhibit_manager_v1_interface,
                            META_ZWP_IDLE_INHIBIT_V1_VERSION,
                            NULL,
                            bind_idle_inhibit) == NULL)
    return FALSE;
  return TRUE;  
}
