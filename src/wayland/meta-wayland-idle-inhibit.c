/*
 * Copyright (C) 2018 SUSE Linux Products GmbH
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
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
visibility_changed_cb (MetaSurfaceActor *actor,
                       MetaWaylandIdleInhibitor *inhibitor)
{
  GVariant *ret;

  if (!inhibitor->session_proxy)
    return;

  if (inhibitor->surface && inhibitor->surface->surface_actor != actor)
    return;

  if (!meta_surface_actor_is_obscured (actor))
    {
      if (!inhibitor->idle_inhibited)
        {
          ret = g_dbus_proxy_call_sync (G_DBUS_PROXY(inhibitor->session_proxy),
                  "Inhibit",
	          g_variant_new ("(ss)", "gnome-shell", "idle-inhibit"),
                  G_DBUS_CALL_FLAGS_NONE,
                  -1,
                  NULL,
                  NULL);
          g_variant_get (ret, "(u)", &inhibitor->cookie);
          inhibitor->idle_inhibited = true;
          meta_verbose ("Inhibit org.freedesktop.ScreenSaver cookie = %u\n",
                        inhibitor->cookie);
      }
    }
  else if (inhibitor->idle_inhibited)
    {
      ret = g_dbus_proxy_call_sync (G_DBUS_PROXY(inhibitor->session_proxy),
              "UnInhibit",
              g_variant_new ("(u)", inhibitor->cookie),
              G_DBUS_CALL_FLAGS_NONE,
              -1,
              NULL,
              NULL);
      inhibitor->idle_inhibited = false;
      meta_verbose ("UnInhibit org.freedesktop.ScreenSaver");
    }
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
  GVariant *ret;
  MetaWaylandIdleInhibitor *inhibitor = wl_resource_get_user_data (resource);

  g_signal_handler_disconnect (inhibitor->surface->surface_actor,
                                   inhibitor->inhibit_idle_handler);

  /* Uninhibit when the inhibitor is destroyed */
  if (inhibitor->session_proxy && inhibitor->idle_inhibited)
    {
       meta_verbose ("UnInhibit org.freedesktop.ScreenSaver\n");
       ret = g_dbus_proxy_call_sync (G_DBUS_PROXY(inhibitor->session_proxy),
               "UnInhibit",
               g_variant_new ("(u)", inhibitor->cookie),
               G_DBUS_CALL_FLAGS_NONE,
               -1,
               NULL,
               NULL);
      inhibitor->idle_inhibited = false;
  }

  wl_resource_destroy (resource);
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

  if (!s)
    return;

  cr = wl_resource_create (client, &zwp_idle_inhibitor_v1_interface,
		  		wl_resource_get_version (resource), id);

  if (cr == NULL)
    {
      wl_client_post_no_memory(client);
      return;
    }
  
  inhibitor = g_new0 (MetaWaylandIdleInhibitor, 1);
  inhibitor->cookie = 0;
  inhibitor->surface = s;
  inhibitor->idle_inhibited = false;

  inhibitor->inhibit_idle_handler =
    g_signal_connect (s->surface_actor, "obscured_changed",
                      G_CALLBACK (visibility_changed_cb),
                      inhibitor);

  inhibitor->session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,
                                  "org.freedesktop.ScreenSaver",
                                  "/org/freedesktop/ScreenSaver",
                                  "org.freedesktop.ScreenSaver",
                                  NULL,
                                  NULL);

  if (!inhibitor->session_proxy)
    {
      meta_verbose ("idle_inhibitor %p no org.freedesktop.ScreenSaver proxy\n",
		    inhibitor);
    }

  /* as the surface has already been created, we check its visibility state in
   * the inhibitor initialization.
   */
  visibility_changed_cb (inhibitor->surface->surface_actor,
                        inhibitor);

  wl_resource_set_implementation (cr, &idle_inhibitor_interface,
                                  inhibitor, NULL);
}

static const struct zwp_idle_inhibitor_v1_interface
  idle_inhibitor_interface = {
    idle_inhibitor_destroy,
 };

static const struct zwp_idle_inhibit_manager_v1_interface
  idle_inhibit_manager_interface = {
    idle_inhibit_manager_destroy,
    idle_inhibit_manager_create_inhibitor,
  };

static void
bind_idle_inhibit (struct wl_client *client,
           void *data,
           guint32 version,
           guint32 id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zwp_idle_inhibit_manager_v1_interface,
                                 version, id);
  if (resource == NULL)
    {
      wl_client_post_no_memory(client);
      return;
    }

  wl_resource_set_implementation (resource,
                                  &idle_inhibit_manager_interface,
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
