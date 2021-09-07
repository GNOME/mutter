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
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

#include "test-driver-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_compositor *compositor;
static struct wl_subcompositor *subcompositor;
static struct xdg_wm_base *xdg_wm_base;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct wl_pointer *pointer;
static struct test_driver *test_driver;

static struct wl_surface *toplevel_surface;
static struct xdg_surface *toplevel_xdg_surface;
static struct xdg_toplevel *xdg_toplevel;

static struct wl_surface *popup_surface;
static struct xdg_surface *popup_xdg_surface;
static struct xdg_popup *xdg_popup;

static struct wl_surface *subsurface_surface;
static struct wl_subsurface *subsurface;

static void
handle_buffer_release (void             *data,
                       struct wl_buffer *buffer)
{
  wl_buffer_destroy (buffer);
}

static const struct wl_buffer_listener buffer_listener = {
  handle_buffer_release
};

static gboolean
create_shm_buffer (int                width,
                   int                height,
                   struct wl_buffer **out_buffer,
                   void             **out_data,
                   int               *out_size)
{
  struct wl_shm_pool *pool;
  static struct wl_buffer *buffer;
  int fd, size, stride;
  int bytes_per_pixel;
  void *data;

  bytes_per_pixel = 4;
  stride = width * bytes_per_pixel;
  size = stride * height;

  fd = create_anonymous_file (size);
  if (fd < 0)
    {
      fprintf (stderr, "Creating a buffer file for %d B failed: %m\n",
               size);
      return FALSE;
    }

  data = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED)
    {
      fprintf (stderr, "mmap failed: %m\n");
      close (fd);
      return FALSE;
    }

  pool = wl_shm_create_pool (shm, fd, size);
  buffer = wl_shm_pool_create_buffer (pool, 0,
                                      width, height,
                                      stride,
                                      WL_SHM_FORMAT_ARGB8888);
  wl_buffer_add_listener (buffer, &buffer_listener, buffer);
  wl_shm_pool_destroy (pool);
  close (fd);

  *out_buffer = buffer;
  *out_data = data;
  *out_size = size;

  return TRUE;
}

static void
fill (void    *buffer_data,
      int      width,
      int      height,
      uint32_t color)
{
  uint32_t *pixels = buffer_data;
  int x, y;

  for (y = 0; y < height; y++)
    {
      for (x = 0; x < width; x++)
        pixels[y * width + x] = color;
    }
}

static void
draw (struct wl_surface *surface,
      int                width,
      int                height,
      uint32_t           color)
{
  struct wl_buffer *buffer;
  void *buffer_data;
  int size;

  if (!create_shm_buffer (width, height,
                          &buffer, &buffer_data, &size))
    g_error ("Failed to create shm buffer");

  fill (buffer_data, width, height, color);

  wl_surface_attach (surface, buffer, 0, 0);
}

static void
draw_main (void)
{
  draw (toplevel_surface, 200, 200, 0xff00ffff);
}

static void
draw_popup (void)
{
  draw (popup_surface, 100, 100, 0xff005500);
}

static void
draw_subsurface (void)
{
  draw (subsurface_surface, 100, 50, 0xff001f00);
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
  xdg_surface_ack_configure (xdg_surface, serial);
  draw_main ();
  wl_surface_commit (toplevel_surface);
  wl_display_flush (display);
}

static const struct xdg_surface_listener toplevel_xdg_surface_listener = {
  handle_toplevel_xdg_surface_configure,
};

static void
handle_xdg_wm_base_ping (void               *data,
                         struct xdg_wm_base *xdg_wm_base,
                         uint32_t            serial)
{
  xdg_wm_base_pong (xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
  handle_xdg_wm_base_ping,
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
  wl_callback_destroy (callback);
  test_driver_sync_point (test_driver, 0, popup_surface);
}

static const struct wl_callback_listener frame_listener = {
  handle_popup_frame_callback,
};

static void
handle_popup_xdg_surface_configure (void               *data,
                                    struct xdg_surface *xdg_surface,
                                    uint32_t            serial)
{
  struct wl_callback *frame_callback;

  draw_popup ();

  draw_subsurface ();
  wl_surface_commit (subsurface_surface);

  xdg_surface_ack_configure (xdg_surface, serial);
  frame_callback = wl_surface_frame (popup_surface);
  wl_callback_add_listener (frame_callback, &frame_listener, NULL);
  wl_surface_commit (popup_surface);
  wl_display_flush (display);
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
  struct xdg_positioner *positioner;
  static int click_count = 0;

  if (button != BTN_LEFT || state != 1)
    return;

  /* Create a grabbing popup surface */
  popup_xdg_surface = xdg_wm_base_get_xdg_surface (xdg_wm_base,
                                                   popup_surface);
  xdg_surface_add_listener (popup_xdg_surface,
                            &popup_xdg_surface_listener, NULL);
  positioner = xdg_wm_base_create_positioner (xdg_wm_base);
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
      wl_display_roundtrip (display);
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
  if (caps & WL_SEAT_CAPABILITY_POINTER)
    {
      pointer = wl_seat_get_pointer (wl_seat);
      wl_pointer_add_listener (pointer, &pointer_listener, NULL);
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
test_driver_handle_sync_event (void               *data,
                               struct test_driver *test_driver,
                               uint32_t            serial)
{
  g_assert (serial == 0);

  /* Sync event 0 is sent when the popup window actor is destryed;
   * prepare for opening a popup for the same wl_surface.
   */
  wl_surface_attach (popup_surface, NULL, 0, 0);
  wl_surface_commit (popup_surface);
  g_clear_pointer (&xdg_popup, xdg_popup_destroy);
  g_clear_pointer (&popup_xdg_surface, xdg_surface_destroy);

  /* This will trigger a click again, opening the popup a second time. */
  test_driver_sync_point (test_driver, 1, toplevel_surface);
}

static const struct test_driver_listener test_driver_listener = {
  test_driver_handle_sync_event,
};

static void
handle_registry_global (void               *data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  if (strcmp (interface, "wl_compositor") == 0)
    {
      compositor = wl_registry_bind (registry, id, &wl_compositor_interface, 1);
    }
  else if (strcmp (interface, "wl_subcompositor") == 0)
    {
      subcompositor = wl_registry_bind (registry,
                                        id, &wl_subcompositor_interface, 1);
    }
  else if (strcmp (interface, "xdg_wm_base") == 0)
    {
      xdg_wm_base = wl_registry_bind (registry, id,
                                      &xdg_wm_base_interface, 1);
      xdg_wm_base_add_listener (xdg_wm_base, &xdg_wm_base_listener, NULL);
    }
  else if (strcmp (interface, "wl_shm") == 0)
    {
      shm = wl_registry_bind (registry,
                              id, &wl_shm_interface, 1);
    }
  else if (strcmp (interface, "wl_seat") == 0)
    {
      seat = wl_registry_bind (registry, id, &wl_seat_interface, 1);
      wl_seat_add_listener (seat, &seat_listener, NULL);
    }
  else if (strcmp (interface, "test_driver") == 0)
    {
      test_driver = wl_registry_bind (registry, id, &test_driver_interface, 1);
      test_driver_add_listener (test_driver, &test_driver_listener, NULL);
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
  display = wl_display_connect (NULL);
  registry = wl_display_get_registry (display);
  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display);

  if (!shm)
    {
      fprintf (stderr, "No wl_shm global\n");
      return EXIT_FAILURE;
    }

  if (!xdg_wm_base)
    {
      fprintf (stderr, "No xdg_wm_base global\n");
      return EXIT_FAILURE;
    }

  wl_display_roundtrip (display);

  g_assert_nonnull (test_driver);

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

  toplevel_surface = wl_compositor_create_surface (compositor);
  toplevel_xdg_surface = xdg_wm_base_get_xdg_surface (xdg_wm_base,
                                                      toplevel_surface);
  xdg_surface_add_listener (toplevel_xdg_surface,
                            &toplevel_xdg_surface_listener, NULL);
  xdg_toplevel = xdg_surface_get_toplevel (toplevel_xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);
  xdg_toplevel_set_title (xdg_toplevel, "subsurface-parent-unmapped");
  wl_surface_commit (toplevel_surface);

  popup_surface = wl_compositor_create_surface (compositor);
  subsurface_surface = wl_compositor_create_surface (compositor);
  subsurface = wl_subcompositor_get_subsurface (subcompositor,
                                                subsurface_surface,
                                                popup_surface);
  wl_subsurface_set_position (subsurface, 0, 0);
  wl_subsurface_set_desync (subsurface);

  while (TRUE)
    {
      if (wl_display_dispatch (display) == -1)
        return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
