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
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

static WaylandDisplay *display;
static struct wl_surface *toplevel_surface, *child_surface, *grandchild_surface;
static struct wl_subsurface *child, *grandchild;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *xdg_toplevel;

static gboolean waiting_for_configure = FALSE;
static gboolean fullscreen = 0;
static uint32_t window_width = 0;
static uint32_t window_height = 0;

static void
handle_xdg_toplevel_configure (void                *data,
                               struct xdg_toplevel *xdg_toplevel,
                               int32_t              width,
                               int32_t              height,
                               struct wl_array     *states)
{
  uint32_t *p;

  fullscreen = 0;
  wl_array_for_each(p, states)
    {
      uint32_t state = *p;

      switch (state)
        {
        case XDG_TOPLEVEL_STATE_FULLSCREEN:
          fullscreen = 1;
          break;
        }
    }

  if (width > 0 && height > 0)
    {
      window_width = width;
      window_height = height;
    }
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
draw_toplevel (void)
{
  draw_surface (display, toplevel_surface,
                window_width, window_height,
                0xffffffffu);
}

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
draw_child (void)
{
  draw_surface (display, child_surface,
                window_width / 2, window_height / 2,
                0xff000000u);
}

static void
draw_grandchild (void)
{
  draw_surface (display, grandchild_surface,
                window_width / 2, window_height / 2,
                0xffff0000u);
}

static void
wait_for_configure (void)
{
  waiting_for_configure = TRUE;
  while (waiting_for_configure || window_width == 0)
    {
      if (wl_display_dispatch (display->display) == -1)
        exit (EXIT_FAILURE);
    }
}

int
main (int    argc,
      char **argv)
{
  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);

  toplevel_surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, toplevel_surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, NULL);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);
  xdg_toplevel_set_title (xdg_toplevel, "subsurface-corner-cases");

  xdg_toplevel_set_fullscreen (xdg_toplevel, NULL);
  wl_surface_commit (toplevel_surface);
  wait_for_configure ();

  draw_toplevel ();
  wl_surface_commit (toplevel_surface);
  wait_for_effects_completed (display, toplevel_surface);

  child_surface = wl_compositor_create_surface (display->compositor);
  child = wl_subcompositor_get_subsurface (display->subcompositor,
                                           child_surface,
                                           toplevel_surface);
  draw_child ();
  wl_surface_commit (child_surface);
  /* No toplevel commit → sub-surface must not be mapped yet */
  wait_for_view_verified (display, 0);

  wl_surface_commit (toplevel_surface);
  /* Toplevel commit → sub-surface must be mapped */
  wait_for_view_verified (display, 1);

  wl_subsurface_set_position (child, window_width / 2, window_height / 2);
  /* No toplevel commit → sub-surface must not have moved yet */
  wait_for_view_verified (display, 2);

  wl_surface_commit (toplevel_surface);
  /* Toplevel commit → sub-surface must have moved */
  wait_for_view_verified (display, 3);

  wl_surface_attach (child_surface, NULL, 0, 0);
  wl_surface_commit (child_surface);
  /* No toplevel commit → sub-surface must not be unmapped yet */
  wait_for_view_verified (display, 4);

  wl_surface_commit (toplevel_surface);
  /* Toplevel commit → sub-surface must be unmapped */
  wait_for_view_verified (display, 5);

  draw_child ();
  wl_surface_commit (child_surface);
  wl_subsurface_set_desync (child);
  wl_surface_attach (child_surface, NULL, 0, 0);
  wl_surface_commit (child_surface);
  /* Desync sub-surface must have been unmapped */
  wait_for_view_verified (display, 6);

  draw_child ();
  wl_surface_commit (child_surface);
  wl_subsurface_set_sync (child);
  wl_subsurface_destroy (child);
  /* Sub-surface destroyed → must be unmapped */
  wait_for_view_verified (display, 7);

  child = wl_subcompositor_get_subsurface (display->subcompositor,
                                           child_surface,
                                           toplevel_surface);
  draw_child ();
  wl_surface_commit (child_surface);
  /* No toplevel commit → sub-surface must not be mapped yet */
  wait_for_view_verified (display, 8);

  wl_surface_commit (toplevel_surface);
  /* Sub-surface position must have reset to (0, 0) */
  wait_for_view_verified (display, 9);

  wl_subsurface_place_below (child, toplevel_surface);
  /* No toplevel commit → sub-surface must still be above toplevel */
  wait_for_view_verified (display, 10);

  wl_subsurface_destroy (child);
  child = wl_subcompositor_get_subsurface (display->subcompositor,
                                           child_surface,
                                           toplevel_surface);
  draw_child ();
  wl_surface_commit (child_surface);
  wl_surface_commit (toplevel_surface);
  /* New sub-surface → placement below toplevel must not have taken effect */
  wait_for_view_verified (display, 11);

  grandchild_surface = wl_compositor_create_surface (display->compositor);
  grandchild = wl_subcompositor_get_subsurface (display->subcompositor,
                                                grandchild_surface,
                                                child_surface);
  draw_grandchild ();
  wl_subsurface_set_position (grandchild, window_width / 4, window_height / 4);
  wl_surface_commit (grandchild_surface);
  wl_surface_commit (child_surface);
  /* No toplevel commit → grand-child surface must not be mapped yet */
  wait_for_view_verified (display, 12);

  wl_surface_commit (toplevel_surface);
  /* Toplevel commit → grand-child surface must be mapped */
  wait_for_view_verified (display, 13);

  wl_subsurface_place_below (grandchild, child_surface);
  wl_surface_commit (child_surface);
  wl_subsurface_destroy (grandchild);
  grandchild = wl_subcompositor_get_subsurface (display->subcompositor,
                                                grandchild_surface,
                                                child_surface);
  draw_grandchild ();
  wl_subsurface_set_position (grandchild, window_width / 4, window_height / 4);
  wl_surface_commit (grandchild_surface);
  wl_surface_commit (child_surface);
  wl_surface_commit (toplevel_surface);
  /* New grandchild must be placed above its parent */
  wait_for_view_verified (display, 14);

  g_clear_object (&display);

  return EXIT_SUCCESS;
}
