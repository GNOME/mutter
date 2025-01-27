/*
 * Copyright (C) 2025 Red Hat, Inc.
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

#define N_IMAGES 2
#define FORMAT WL_SHM_FORMAT_ARGB8888
#define FORMAT_BPP 4
#define HEIGHT 100
#define WIDTH 100
#define STRIDE (FORMAT_BPP * WIDTH)
#define IMAGE_SIZE (STRIDE * HEIGHT)
#define BUFFER_SIZE (IMAGE_SIZE * N_IMAGES)

static gboolean waiting_for_configure = FALSE;

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
  xdg_surface_ack_configure (xdg_surface, serial);

  waiting_for_configure = FALSE;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
wait_for_configure (WaylandDisplay *display)
{
  waiting_for_configure = TRUE;
  while (waiting_for_configure)
    wayland_display_dispatch (display);
}

static void
draw (char     *data,
      uint32_t  color)
{
  uint32_t *pixels = (uint32_t *)data;

  for (int i = 0; i < HEIGHT; i++)
    {
      for (int j = 0; j < WIDTH; j++)
        {
          *(pixels + ((STRIDE / 4) * i) + j) = color;
        }
    }
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  struct wl_surface *subsurface_surface;
  struct wl_subsurface *subsurface;
  struct wl_buffer *buffer1, *buffer2;
  struct wl_shm_pool *pool;
  char *data;
  int fd;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);

  surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, NULL);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);
  xdg_toplevel_set_title (xdg_toplevel, "shm-destroy-before-release");
  xdg_toplevel_set_fullscreen (xdg_toplevel, NULL);
  wl_surface_commit (surface);

  wait_for_configure (display);

  subsurface_surface = wl_compositor_create_surface (display->compositor);
  subsurface = wl_subcompositor_get_subsurface (display->subcompositor,
                                                subsurface_surface,
                                                surface);
  wl_subsurface_set_sync (subsurface);
  wl_subsurface_set_position (subsurface, 0, 0);

  fd = create_anonymous_file (BUFFER_SIZE);
  g_assert_cmpint (fd, >=, 0);

  data = mmap (NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  g_assert_nonnull (data);

  /* create a pool which can hold one image, and draw a white main surface */
  pool = wl_shm_create_pool (display->shm, fd, IMAGE_SIZE);
  buffer1 = wl_shm_pool_create_buffer (pool, IMAGE_SIZE * 0,
                                       WIDTH, HEIGHT, STRIDE, FORMAT);
  draw (data + (IMAGE_SIZE * 0), 0xffffffff);
  wl_surface_attach (surface, buffer1, 0, 0);
  wl_surface_damage_buffer (surface, 0, 0, WIDTH, HEIGHT);
  wl_surface_commit (surface);

  /* resize shm pool to hold two images; the compositor already has a buffer
   * in that pool and we will create a buffer for the subsurface from it. */
  wl_shm_pool_resize (pool, IMAGE_SIZE * 2);
  buffer2 = wl_shm_pool_create_buffer (pool, IMAGE_SIZE * 1,
                                       WIDTH, HEIGHT, STRIDE, FORMAT);
  draw (data + (IMAGE_SIZE * 1), 0xff0000ff);
  wl_surface_attach (subsurface_surface, buffer2, 0, 0);
  wl_surface_damage_buffer (subsurface_surface, 0, 0, WIDTH, HEIGHT);
  wl_surface_commit (subsurface_surface);
  wl_surface_commit (surface);

  /* ensure everything is as expected */
  wait_for_effects_completed (display, surface);
  wait_for_view_verified (display, 0);

  /* update the subsurface color */
  draw (data + (IMAGE_SIZE * 1), 0xff00ffff);
  wl_surface_attach (subsurface_surface, buffer2, 0, 0);
  wl_surface_damage_buffer (subsurface_surface, 0, 0, WIDTH, HEIGHT);
  wl_surface_commit (subsurface_surface);

  /* We now have a commit on the subsurface which is queued until the main
   * surface gets committed. Destroying the buffer resource is now valid, but
   * the compositor will have to make sure that applying it later will work. */
  wl_buffer_destroy (buffer2);
  test_driver_sync_point (display->test_driver, 0, NULL);
  wait_for_sync_event (display, 0);
  wl_surface_commit (surface);

  wait_for_effects_completed (display, surface);
  wait_for_view_verified (display, 1);

  /* cleanup */
  munmap (data, BUFFER_SIZE);
  close (fd);
}
