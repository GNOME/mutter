/*
 * Copyright (C) 2020 Red Hat, Inc.
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
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

#include "test-driver-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_compositor *compositor;
static struct xdg_wm_base *xdg_wm_base;
static struct wl_shm *shm;

static struct wl_surface *surface;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *xdg_toplevel;

static gboolean running;

static void
init_surface (void)
{
  xdg_toplevel_set_title (xdg_toplevel, "bogus window geometry");
  wl_surface_commit (surface);
}

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
  draw (surface, 700, 500, 0xff00ff00);
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
handle_xdg_toplevel_close (void                *data,
                           struct xdg_toplevel *xdg_toplevel)
{
  g_assert_not_reached ();
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  handle_xdg_toplevel_configure,
  handle_xdg_toplevel_close,
};

static void
handle_xdg_surface_configure (void               *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t            serial)
{
  xdg_surface_set_window_geometry (xdg_surface, 0, 0, 0, 0);
  draw_main ();
  wl_surface_commit (surface);

  g_assert_cmpint (wl_display_roundtrip (display), !=, -1);
  running = FALSE;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
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

static void
test_empty_window_geometry (void)
{
  display = wl_display_connect (NULL);
  registry = wl_display_get_registry (display);
  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display);

  g_assert_nonnull (shm);
  g_assert_nonnull (xdg_wm_base);

  wl_display_roundtrip (display);

  surface = wl_compositor_create_surface (compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, NULL);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);

  init_surface ();

  running = TRUE;
  while (running)
    {
      if (wl_display_dispatch (display) == -1)
        return;
    }

  g_clear_pointer (&xdg_toplevel, xdg_toplevel_destroy);
  g_clear_pointer (&xdg_surface, xdg_surface_destroy);
  g_clear_pointer (&xdg_wm_base, xdg_wm_base_destroy);
  g_clear_pointer (&compositor, wl_compositor_destroy);
  g_clear_pointer (&shm, wl_shm_destroy);
  g_clear_pointer (&registry, wl_registry_destroy);
  g_clear_pointer (&display, wl_display_disconnect);
}

int
main (int    argc,
      char **argv)
{
  test_empty_window_geometry ();

  return EXIT_SUCCESS;
}
