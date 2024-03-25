/*
 * Copyright (C) 2019 Red Hat, Inc.
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

#include <glib.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

static struct wl_seat *seat;
static struct wl_pointer *pointer;

static struct wl_surface *toplevel_surface;
static struct xdg_surface *toplevel_xdg_surface;

static struct wl_surface *popup_surface;
static struct xdg_surface *popup_xdg_surface;
static struct xdg_popup *xdg_popup;

static struct wl_surface *subsurface_surface;

static void
draw_main (WaylandDisplay *display)
{
  draw_surface (display, toplevel_surface, 200, 200, 0xff00ffff);
}

static void
draw_popup (WaylandDisplay *display)
{
  draw_surface (display, popup_surface, 100, 100, 0xff005500);
}

static void
draw_subsurface (WaylandDisplay *display)
{
  draw_surface (display, subsurface_surface, 100, 50, 0xff001f00);
}

static void
handle_xdg_toplevel_configure (void                *data,
                               struct xdg_toplevel *xdg_toplevel,
                               int32_t              width,
                               int32_t              height,
                               struct wl_array     *state)
{
}

static void
handle_xdg_toplevel_close(void                *data,
                          struct xdg_toplevel *xdg_toplevel)
{
  g_assert_not_reached ();
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  handle_xdg_toplevel_configure,
  handle_xdg_toplevel_close,
};

static void
handle_toplevel_xdg_surface_configure (void               *data,
                                       struct xdg_surface *xdg_surface,
                                       uint32_t            serial)
{
  WaylandDisplay *display = data;

  xdg_surface_ack_configure (xdg_surface, serial);
  draw_main (display);
  wl_surface_commit (toplevel_surface);
  wl_display_flush (display->display);
}

static const struct xdg_surface_listener toplevel_xdg_surface_listener = {
  handle_toplevel_xdg_surface_configure,
};

static void
pointer_handle_enter (void              *data,
                      struct wl_pointer *pointer,
                      uint32_t           serial,
                      struct wl_surface *surface,
                      wl_fixed_t         sx,
                      wl_fixed_t         sy)
{
}

static void
pointer_handle_leave (void              *data,
                      struct wl_pointer *pointer,
                      uint32_t           serial,
                      struct wl_surface *surface)
{
}

static void
pointer_handle_motion (void              *data,
                       struct wl_pointer *pointer,
                       uint32_t           time,
                       wl_fixed_t         sx,
                       wl_fixed_t         sy)
{
}

static void
handle_popup_frame_callback (void               *data,
                             struct wl_callback *callback,
                             uint32_t            time)
{
  WaylandDisplay *display = data;

  wl_callback_destroy (callback);
  test_driver_sync_point (display->test_driver, 0, popup_surface);
}

static const struct wl_callback_listener frame_listener = {
  handle_popup_frame_callback,
};

static void
handle_popup_xdg_surface_configure (void               *data,
                                    struct xdg_surface *xdg_surface,
                                    uint32_t            serial)
{
  WaylandDisplay *display = data;
  struct wl_callback *frame_callback;

  draw_popup (display);

  draw_subsurface (display);
  wl_surface_commit (subsurface_surface);

  xdg_surface_ack_configure (xdg_surface, serial);
  frame_callback = wl_surface_frame (popup_surface);
  wl_callback_add_listener (frame_callback, &frame_listener, display);
  wl_surface_commit (popup_surface);
  wl_display_flush (display->display);
}

static const struct xdg_surface_listener popup_xdg_surface_listener = {
  handle_popup_xdg_surface_configure,
};

static void
pointer_handle_button (void              *data,
                       struct wl_pointer *pointer,
                       uint32_t           serial,
                       uint32_t           time,
                       uint32_t           button,
                       uint32_t           state)
{
  WaylandDisplay *display = data;
  struct xdg_positioner *positioner;
  static int click_count = 0;

  if (button != BTN_LEFT || state != 1)
    return;

  /* Create a grabbing popup surface */
  popup_xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base,
                                                   popup_surface);
  xdg_surface_add_listener (popup_xdg_surface,
                            &popup_xdg_surface_listener, display);
  positioner = xdg_wm_base_create_positioner (display->xdg_wm_base);
  xdg_positioner_set_size (positioner, 100, 100);
  xdg_positioner_set_anchor_rect (positioner, 0, 0, 1, 1);
  xdg_popup = xdg_surface_get_popup (popup_xdg_surface, toplevel_xdg_surface,
                                     positioner);
  xdg_positioner_destroy (positioner);
  xdg_popup_grab (xdg_popup, seat, serial);
  wl_surface_commit (popup_surface);

  if (click_count == 1)
    {
      /* This ensure that the second time the popup is opened, the commit
       * is handled accurately. This passing verifies we don't reproduce
       * https://gitlab.gnome.org/GNOME/mutter/-/issues/1828.
       */
      wl_display_roundtrip (display->display);
      exit (EXIT_SUCCESS);
    }

  click_count++;
}

static void
pointer_handle_axis (void              *data,
                     struct wl_pointer *pointer,
                     uint32_t           time,
                     uint32_t           axis,
                     wl_fixed_t         value)
{
}

static const struct wl_pointer_listener pointer_listener = {
  pointer_handle_enter,
  pointer_handle_leave,
  pointer_handle_motion,
  pointer_handle_button,
  pointer_handle_axis,
};

static void
seat_handle_capabilities (void                    *data,
                          struct wl_seat          *wl_seat,
                          enum wl_seat_capability  caps)
{
  WaylandDisplay *display = data;

  if (caps & WL_SEAT_CAPABILITY_POINTER)
    {
      pointer = wl_seat_get_pointer (wl_seat);
      wl_pointer_add_listener (pointer, &pointer_listener, display);
    }
}

static void
seat_handle_name (void           *data,
                  struct wl_seat *seat,
                  const char     *name)
{
}

static const struct wl_seat_listener seat_listener = {
  seat_handle_capabilities,
  seat_handle_name,
};

static void
on_sync_event (WaylandDisplay *display,
               uint32_t        serial)
{
  g_assert (serial == 0);

  /* Sync event 0 is sent when the popup window actor is destroyed;
   * prepare for opening a popup for the same wl_surface.
   */
  wl_surface_attach (popup_surface, NULL, 0, 0);
  wl_surface_commit (popup_surface);
  g_clear_pointer (&xdg_popup, xdg_popup_destroy);
  g_clear_pointer (&popup_xdg_surface, xdg_surface_destroy);

  /* This will trigger a click again, opening the popup a second time. */
  test_driver_sync_point (display->test_driver, 1, toplevel_surface);
}

static void
handle_registry_global (void               *data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  WaylandDisplay *display = data;

  if (strcmp (interface, "wl_seat") == 0)
    {
      seat = wl_registry_bind (registry, id, &wl_seat_interface, 1);
      wl_seat_add_listener (seat, &seat_listener, display);
    }
}

static void
handle_registry_global_remove (void               *data,
                               struct wl_registry *registry,
                               uint32_t            name)
{
}

static const struct wl_registry_listener registry_listener = {
  handle_registry_global,
  handle_registry_global_remove
};

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct wl_registry *registry;
  struct xdg_toplevel *xdg_toplevel;
  struct wl_subsurface *subsurface;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);

  g_signal_connect (display, "sync-event", G_CALLBACK (on_sync_event), NULL);

  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, display);
  wl_display_roundtrip (display->display);
  wl_display_roundtrip (display->display);

  /*
   * This test case does the following:
   *
   *  1) Open a toplevel
   *  2) Open a popup in response to a pointer click
   *  3) Place a subsurface on that popup
   *  4) After painting, get the popup dismissed by the compositor
   *  5) Once the popup window actor is destroyed, trigger a new pointer click
   *  6) Open the popup again using the same wl_surface, thus with the same
   *     subsurface association set up.
   */

  toplevel_surface = wl_compositor_create_surface (display->compositor);
  toplevel_xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base,
                                                      toplevel_surface);
  xdg_surface_add_listener (toplevel_xdg_surface,
                            &toplevel_xdg_surface_listener, display);
  xdg_toplevel = xdg_surface_get_toplevel (toplevel_xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);
  xdg_toplevel_set_title (xdg_toplevel, "subsurface-parent-unmapped");
  wl_surface_commit (toplevel_surface);

  popup_surface = wl_compositor_create_surface (display->compositor);
  subsurface_surface = wl_compositor_create_surface (display->compositor);
  subsurface = wl_subcompositor_get_subsurface (display->subcompositor,
                                                subsurface_surface,
                                                popup_surface);
  wl_subsurface_set_position (subsurface, 0, 0);
  wl_subsurface_set_desync (subsurface);

  while (TRUE)
    wayland_display_dispatch (display);

  return EXIT_SUCCESS;
}
