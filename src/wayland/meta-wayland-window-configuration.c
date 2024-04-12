/*
 * Copyright (C) 2019 Red Hat
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
 */

#include "config.h"

#include "wayland/meta-wayland-window-configuration.h"
#include "wayland/meta-window-wayland.h"

static uint32_t global_serial_counter = 0;

MetaWaylandWindowConfiguration *
meta_wayland_window_configuration_new (MetaWindow          *window,
                                       MtkRectangle         rect,
                                       int                  bounds_width,
                                       int                  bounds_height,
                                       int                  scale,
                                       MetaMoveResizeFlags  flags,
                                       MetaGravity          gravity)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaWaylandWindowConfiguration *configuration;

  configuration = g_new0 (MetaWaylandWindowConfiguration, 1);
  *configuration = (MetaWaylandWindowConfiguration) {
    .serial = ++global_serial_counter,

    .bounds_width = bounds_width,
    .bounds_height = bounds_height,

    .scale = scale,
    .gravity = gravity,
    .flags = flags,

    .is_fullscreen = meta_window_is_fullscreen (window),
    .is_suspended = meta_window_is_suspended (window),
  };

  if (flags & META_MOVE_RESIZE_MOVE_ACTION ||
      window->rect.x != rect.x ||
      window->rect.y != rect.y)
    {
      configuration->has_position = TRUE;
      configuration->x = rect.x;
      configuration->y = rect.y;
    }

  configuration->has_size = (rect.width != 0 && rect.height != 0);
  configuration->is_resizing = flags & META_MOVE_RESIZE_RESIZE_ACTION ||
    meta_window_wayland_is_resize (wl_window, rect.width, rect.height);
  configuration->width = rect.width;
  configuration->height = rect.height;

  return configuration;
}

MetaWaylandWindowConfiguration *
meta_wayland_window_configuration_new_relative (MetaWindow *window,
                                                int         rel_x,
                                                int         rel_y,
                                                int         width,
                                                int         height,
                                                int         scale)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaWaylandWindowConfiguration *configuration;

  configuration = g_new0 (MetaWaylandWindowConfiguration, 1);
  *configuration = (MetaWaylandWindowConfiguration) {
    .serial = ++global_serial_counter,

    .has_relative_position = TRUE,
    .rel_x = rel_x,
    .rel_y = rel_y,

    .has_size = (width != 0 && height != 0),
    .is_resizing = meta_window_wayland_is_resize (wl_window, width, height),
    .width = width,
    .height = height,

    .scale = scale,
  };

  return configuration;
}

MetaWaylandWindowConfiguration *
meta_wayland_window_configuration_new_empty (int bounds_width,
                                             int bounds_height,
                                             int scale)
{
  MetaWaylandWindowConfiguration *configuration;

  configuration = g_new0 (MetaWaylandWindowConfiguration, 1);
  *configuration = (MetaWaylandWindowConfiguration) {
    .serial = ++global_serial_counter,
    .scale = scale,
    .bounds_width = bounds_width,
    .bounds_height = bounds_height,
  };

  return configuration;
}

void
meta_wayland_window_configuration_free (MetaWaylandWindowConfiguration *configuration)
{
  g_free (configuration);
}
