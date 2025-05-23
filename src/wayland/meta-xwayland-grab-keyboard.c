/*
 * Copyright (C) 2017 Red Hat
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
 * Written by:
 *     Olivier Fourdan <ofourdan@redhat.com>
 */

#include "config.h"

#include <wayland-server.h>

#include "meta/meta-backend.h"
#include "backends/meta-settings-private.h"
#include "wayland/meta-wayland-filter-manager.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-window-wayland.h"
#include "wayland/meta-xwayland-grab-keyboard.h"

struct _MetaXwaylandKeyboardActiveGrab
{
  MetaWaylandSurface *surface;
  MetaWaylandSeat *seat;
  MetaWaylandEventHandler *handler;
  gulong surface_destroyed_handler;
  gulong shortcuts_restored_handler;
  gulong window_associate_handler;
  struct wl_resource *resource;
};

static void
meta_xwayland_keyboard_grab_end (MetaXwaylandKeyboardActiveGrab *active_grab)
{
  if (active_grab->handler)
    {
      MetaWaylandInput *input;

      input = meta_wayland_seat_get_input (active_grab->seat);
      meta_wayland_input_detach_event_handler (input, active_grab->handler);
      active_grab->handler = NULL;
    }

  if (!active_grab->surface)
    return;

  g_clear_signal_handler (&active_grab->surface_destroyed_handler,
                          active_grab->surface);

  g_clear_signal_handler (&active_grab->shortcuts_restored_handler,
                          active_grab->surface);

  meta_wayland_surface_restore_shortcuts (active_grab->surface,
                                          active_grab->seat);

  g_clear_signal_handler (&active_grab->window_associate_handler,
                          active_grab->surface->role);

  active_grab->surface = NULL;
}

static MetaWaylandSurface *
meta_xwayland_keyboard_grab_get_focus_surface (MetaWaylandEventHandler *handler,
                                               ClutterInputDevice      *device,
                                               ClutterEventSequence    *sequence,
                                               gpointer                 user_data)
{
  MetaXwaylandKeyboardActiveGrab *active_grab = user_data;

  /* Force focus onto the surface who has the active grab on the keyboard */
  if (clutter_input_device_get_capabilities (device) &
      CLUTTER_INPUT_CAPABILITY_KEYBOARD)
    return active_grab->surface;

  return meta_wayland_event_handler_chain_up_get_focus_surface (handler,
                                                                device,
                                                                sequence);
}

static void
meta_xwayland_keyboard_grab_focus (MetaWaylandEventHandler *handler,
                                   ClutterInputDevice      *device,
                                   ClutterEventSequence    *sequence,
                                   MetaWaylandSurface      *surface,
                                   gpointer                 user_data)
{
  MetaXwaylandKeyboardActiveGrab *active_grab = user_data;

  if (clutter_input_device_get_capabilities (device) &
      CLUTTER_INPUT_CAPABILITY_KEYBOARD &&
      surface != active_grab->surface)
    meta_xwayland_keyboard_grab_end (active_grab);
  else
    meta_wayland_event_handler_chain_up_focus (handler, device, sequence, surface);
}

static const MetaWaylandEventInterface grab_event_interface = {
  meta_xwayland_keyboard_grab_get_focus_surface,
  meta_xwayland_keyboard_grab_focus,
};

static void
zwp_xwayland_keyboard_grab_destructor (struct wl_resource *resource)
{
  MetaXwaylandKeyboardActiveGrab *active_grab;

  active_grab = wl_resource_get_user_data (resource);
  meta_xwayland_keyboard_grab_end (active_grab);

  g_free (active_grab);
}

static void
zwp_xwayland_keyboard_grab_destroy (struct wl_client   *client,
                                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_xwayland_keyboard_grab_v1_interface
  xwayland_keyboard_grab_interface = {
    zwp_xwayland_keyboard_grab_destroy,
  };

static void
surface_destroyed_cb (MetaWaylandSurface             *surface,
                      MetaXwaylandKeyboardActiveGrab *active_grab)
{
  active_grab->surface = NULL;
}

static void
shortcuts_restored_cb (MetaWaylandSurface             *surface,
                       MetaXwaylandKeyboardActiveGrab *active_grab)
{
  meta_xwayland_keyboard_grab_end (active_grab);
}

static void
zwp_xwayland_keyboard_grab_manager_destroy (struct wl_client   *client,
                                            struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static gboolean
application_is_in_pattern_array (MetaWindow *window,
                                 GPtrArray  *pattern_array)
{
  const char *class;
  const char *name;
  guint i;

  if (window->res_class)
    class = window->res_class;
  else
    class = "";

  if (window->res_name)
    name = window->res_name;
  else
    name = "";

  for (i = 0; pattern_array && i < pattern_array->len; i++)
    {
      GPatternSpec *pattern = (GPatternSpec *) g_ptr_array_index (pattern_array, i);

      if (g_pattern_spec_match_string (pattern, class) ||
          g_pattern_spec_match_string (pattern, name))
        return TRUE;
    }

   return FALSE;
}

static gboolean
meta_xwayland_grab_is_granted (MetaWindow *window)
{
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaSettings *settings = meta_backend_get_settings (backend);
  GPtrArray *allow_list;
  GPtrArray *deny_list;
  gboolean may_grab;


  /* Check whether the window is in the deny list */
  meta_settings_get_xwayland_grab_patterns (settings, &allow_list, &deny_list);

  if (deny_list && application_is_in_pattern_array (window, deny_list))
    return FALSE;

  /* Check if we are dealing with good citizen Xwayland client allowing itself. */
  g_object_get (G_OBJECT (window), "xwayland-may-grab-keyboard", &may_grab, NULL);
  if (may_grab)
    return TRUE;

  /* Last resort, is it in the grant list. */
  if (allow_list && application_is_in_pattern_array (window, allow_list))
    return TRUE;

  return FALSE;
}

static gboolean
meta_xwayland_grab_should_lock_focus (MetaWindow *window)
{
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaSettings *settings = meta_backend_get_settings (backend);

  /* Lock focus applies to O-R windows which never receive keyboard focus otherwise */
  if (!window->override_redirect)
    return FALSE;

  return meta_settings_are_xwayland_grabs_allowed (settings);
}

static void
meta_xwayland_keyboard_grab_activate (MetaXwaylandKeyboardActiveGrab *active_grab)
{
  MetaWaylandSurface *surface = active_grab->surface;
  MetaWindow *window = meta_wayland_surface_get_window (surface);
  MetaWaylandSeat *seat = active_grab->seat;

  if (meta_xwayland_grab_is_granted (window))
    {
      meta_topic (META_DEBUG_WAYLAND,
                  "XWayland window %s has a grab granted", window->desc);
      meta_wayland_surface_inhibit_shortcuts (surface, seat);

      if (meta_xwayland_grab_should_lock_focus (window))
        {
          MetaWaylandInput *input;

          input = meta_wayland_seat_get_input (seat);
          active_grab->handler =
            meta_wayland_input_attach_event_handler (input,
                                                     &grab_event_interface,
                                                     FALSE, active_grab);
        }
    }

  g_clear_signal_handler (&active_grab->window_associate_handler,
                          active_grab->surface->role);
}

static void
meta_xwayland_keyboard_window_associated (MetaWaylandSurfaceRole         *surface_role,
                                          MetaXwaylandKeyboardActiveGrab *active_grab)
{
  meta_xwayland_keyboard_grab_activate (active_grab);
}

static void
zwp_xwayland_keyboard_grab_manager_grab (struct wl_client   *client,
                                         struct wl_resource *resource,
                                         uint32_t            id,
                                         struct wl_resource *surface_resource,
                                         struct wl_resource *seat_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWindow *window = meta_wayland_surface_get_window (surface);
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  MetaXwaylandKeyboardActiveGrab *active_grab;
  struct wl_resource *grab_resource;

  grab_resource = wl_resource_create (client,
                                      &zwp_xwayland_keyboard_grab_manager_v1_interface,
                                      wl_resource_get_version (resource),
                                      id);

  active_grab = g_new0 (MetaXwaylandKeyboardActiveGrab, 1);
  active_grab->surface = surface;
  active_grab->resource = grab_resource;
  active_grab->seat = seat;
  active_grab->surface_destroyed_handler =
    g_signal_connect (surface, "destroy",
                      G_CALLBACK (surface_destroyed_cb),
                      active_grab);
  active_grab->shortcuts_restored_handler =
    g_signal_connect (surface, "shortcuts-restored",
                      G_CALLBACK (shortcuts_restored_cb),
                      active_grab);

  if (window)
    meta_xwayland_keyboard_grab_activate (active_grab);
  else if (surface->role)
    active_grab->window_associate_handler =
      g_signal_connect (surface->role, "window-associated",
                        G_CALLBACK (meta_xwayland_keyboard_window_associated),
                        active_grab);
  else
    g_warning ("Cannot grant Xwayland grab to surface %p", surface);

  wl_resource_set_implementation (grab_resource,
                                  &xwayland_keyboard_grab_interface,
                                  active_grab,
                                  zwp_xwayland_keyboard_grab_destructor);
}

static const struct zwp_xwayland_keyboard_grab_manager_v1_interface
  meta_keyboard_grab_manager_interface = {
    zwp_xwayland_keyboard_grab_manager_destroy,
    zwp_xwayland_keyboard_grab_manager_grab,
  };

static void
bind_keyboard_grab (struct wl_client *client,
                    void             *data,
                    uint32_t          version,
                    uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zwp_xwayland_keyboard_grab_manager_v1_interface,
                                 MIN (META_ZWP_XWAYLAND_KEYBOARD_GRAB_V1_VERSION, version),
                                 id);

  wl_resource_set_implementation (resource,
                                  &meta_keyboard_grab_manager_interface,
                                  NULL, NULL);
}

static MetaWaylandAccess
xwayland_grab_keyboard_filter (const struct wl_client *client,
                               const struct wl_global *global,
                               gpointer                user_data)
{
  MetaWaylandCompositor *compositor = user_data;
  MetaXWaylandManager *xwayland_manager = &compositor->xwayland_manager;

  if (client == xwayland_manager->client)
    return META_WAYLAND_ACCESS_ALLOWED;
  else
    return META_WAYLAND_ACCESS_DENIED;
}

gboolean
meta_xwayland_grab_keyboard_init (MetaWaylandCompositor *compositor)
{
  struct wl_global *global;
  MetaWaylandFilterManager *filter_manager;

  global = wl_global_create (compositor->wayland_display,
                             &zwp_xwayland_keyboard_grab_manager_v1_interface,
                             META_ZWP_XWAYLAND_KEYBOARD_GRAB_V1_VERSION,
                             NULL,
                             bind_keyboard_grab);

  filter_manager = meta_wayland_compositor_get_filter_manager (compositor);
  meta_wayland_filter_manager_add_global (filter_manager,
                                          global,
                                          xwayland_grab_keyboard_filter,
                                          compositor);

  return TRUE;
}
