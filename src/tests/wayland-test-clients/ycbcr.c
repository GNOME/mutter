/*
 * Copyright (C) 2023 Red Hat, Inc.
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

static struct wl_surface *surface;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *xdg_toplevel;

static gboolean waiting_for_configure = FALSE;

static void
shader_color_gradient (float  x,
                       float  y,
                       float *out_luma,
                       float *out_cb,
                       float *out_cr)
{
  *out_luma = 1.0;
  *out_cb = x;
  *out_cr = y;
}

static void
shader_luma_gradient (float  x,
                      float  y,
                      float *out_luma,
                      float *out_cb,
                      float *out_cr)
{
  *out_luma = (x + y) / 2;
  *out_cb = 0.5;
  *out_cr = 0.5;
}

typedef void (*ShaderFunc) (float  x,
                            float  y,
                            float *out_luma,
                            float *out_cb,
                            float *out_cr);

static void
draw (WaylandDisplay *display,
      uint32_t        drm_format,
      ShaderFunc      shader)
{
  WaylandBuffer *buffer;
  uint8_t *planes[4];
  size_t strides[4];
  int x, y;

#define BUFFER_WIDTH 64
#define BUFFER_HEIGHT 64

  buffer = wayland_buffer_create (display, NULL,
                                  BUFFER_WIDTH, BUFFER_HEIGHT,
                                  drm_format,
                                  NULL, 0,
                                  GBM_BO_USE_LINEAR);
  if (!buffer)
    g_error ("Failed to create buffer");

  switch (drm_format)
    {
    case DRM_FORMAT_YUYV:
      planes[0] = wayland_buffer_mmap_plane (buffer, 0, &strides[0]);
      break;
    case DRM_FORMAT_YUV420:
      planes[0] = wayland_buffer_mmap_plane (buffer, 0, &strides[0]);
      planes[1] = wayland_buffer_mmap_plane (buffer, 1, &strides[1]);
      planes[2] = wayland_buffer_mmap_plane (buffer, 2, &strides[2]);
      break;
    }

  for (y = 0; y < BUFFER_WIDTH; y++)
    {
      for (x = 0; x < BUFFER_WIDTH; x++)
        {
          uint8_t *pixel;
          float luma;
          float cb;
          float cr;

          shader (x / (BUFFER_WIDTH - 1.0),
                  y / (BUFFER_HEIGHT - 1.0),
                  &luma,
                  &cb,
                  &cr);

          switch (drm_format)
            {
            case DRM_FORMAT_YUYV:
              /* packed [31:0] Cr0:Y1:Cb0:Y0 8:8:8:8 little endian */
              pixel = planes[0] + (y * strides[0]) + (x * 2);
              pixel[0] = luma * 255;
              if (x % 2 == 0)
                pixel[1] = cb * 255;
              else
                pixel[1] = cr * 255;
              break;
            case DRM_FORMAT_YUV420:
              /*
               3 plane YCbCr
               index 0: Y plane, [7:0] Y
               index 1: Cb plane, [7:0] Cb
               index 2: Cr plane, [7:0] Cr
               2x2 subsampled Cb (1) and Cr (2) planes */
              pixel = planes[0] + (y * strides[0]) + x;
              pixel[0] = luma * 255;
              pixel = planes[1] + (y / 2 * strides[1]) + x / 2;
              pixel[0] = cb * 255;
              pixel = planes[2] + (y / 2 * strides[2]) + x / 2;
              pixel[0] = cr * 255;
              break;
            default:
              g_assert_not_reached ();
            }
        }
    }

  wl_surface_damage_buffer (surface, 0, 0, BUFFER_WIDTH, BUFFER_HEIGHT);
  wl_surface_attach (surface, wayland_buffer_get_wl_buffer (buffer), 0, 0);
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

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);

  surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, NULL);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);
  xdg_toplevel_set_title (xdg_toplevel, "ycbcr");
  xdg_toplevel_set_fullscreen (xdg_toplevel, NULL);
  wl_surface_commit (surface);

  wait_for_configure (display);

  draw (display, DRM_FORMAT_YUYV, shader_luma_gradient);
  wl_surface_commit (surface);
  wait_for_effects_completed (display, surface);
  wait_for_view_verified (display, 0);

  draw (display, DRM_FORMAT_YUYV, shader_color_gradient);
  wl_surface_commit (surface);
  wait_for_view_verified (display, 1);

  draw (display, DRM_FORMAT_YUV420, shader_luma_gradient);
  wl_surface_commit (surface);
  wait_for_view_verified (display, 2);

  draw (display, DRM_FORMAT_YUV420, shader_color_gradient);
  wl_surface_commit (surface);
  wait_for_view_verified (display, 3);

  g_clear_pointer (&xdg_toplevel, xdg_toplevel_destroy);
  g_clear_pointer (&xdg_surface, xdg_surface_destroy);
}
