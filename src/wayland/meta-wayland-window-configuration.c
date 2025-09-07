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

#include "compositor/compositor-private.h"
#include "core/meta-window-config-private.h"
#include "core/window-private.h"
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
  int x, y;

  configuration = g_new0 (MetaWaylandWindowConfiguration, 1);
  *configuration = (MetaWaylandWindowConfiguration) {
    .ref_count = G_REF_COUNT_INIT,

    .serial = ++global_serial_counter,

    .bounds_width = bounds_width,
    .bounds_height = bounds_height,

    .scale = scale,
    .gravity = gravity,
    .flags = flags,

    .config = meta_window_config_new_from (window->config),
    .is_suspended = meta_window_is_suspended (window),
  };

  meta_window_config_get_position (window->config, &x, &y);
  if (flags & META_MOVE_RESIZE_MOVE_ACTION ||
      x != rect.x ||
      y != rect.y ||
      !meta_window_config_is_floating (configuration->config))
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
    .ref_count = G_REF_COUNT_INIT,

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
    .ref_count = G_REF_COUNT_INIT,
    .serial = ++global_serial_counter,
    .scale = scale,
    .bounds_width = bounds_width,
    .bounds_height = bounds_height,
  };

  return configuration;
}

MetaWaylandWindowConfiguration *
meta_wayland_window_configuration_new_from_other (MetaWaylandWindowConfiguration *other)
{
  MetaWaylandWindowConfiguration *configuration;

  configuration = g_new0 (MetaWaylandWindowConfiguration, 1);
  *configuration = (MetaWaylandWindowConfiguration) {
    .ref_count = G_REF_COUNT_INIT,
    .serial = ++global_serial_counter,

    .has_position = other->has_position,
    .x = other->x,
    .y = other->y,
    .has_relative_position = other->has_relative_position,
    .rel_x = other->rel_x,
    .rel_y = other->rel_y,
    .has_size = other->has_size,
    .is_resizing = other->is_resizing,
    .width = other->width,
    .height = other->height,
    .scale = other->scale,
    .gravity = other->gravity,
    .flags = other->flags,
    .bounds_width = other->bounds_width,
    .bounds_height = other->bounds_height,
    .config = meta_window_config_new_from (other->config),
    .is_suspended = other->is_suspended,
  };

  return configuration;
}

MetaWaylandWindowConfiguration *
meta_wayland_window_configuration_ref (MetaWaylandWindowConfiguration *configuration)
{
  g_ref_count_inc (&configuration->ref_count);
  return configuration;
}

void
meta_wayland_window_configuration_unref (MetaWaylandWindowConfiguration *configuration)
{
  if (g_ref_count_dec (&configuration->ref_count))
    {
      g_clear_object (&configuration->config);
      g_clear_object (&configuration->window_drag);
      g_free (configuration);
    }
}

gboolean
meta_wayland_window_configuration_is_equivalent (MetaWaylandWindowConfiguration *configuration,
                                                 MetaWaylandWindowConfiguration *other)
{
  g_return_val_if_fail (configuration, FALSE);

  if (!other)
    return FALSE;

  return (configuration->has_position == other->has_position &&
          configuration->x == other->x &&
          configuration->y == other->y &&
          configuration->has_relative_position == other->has_relative_position &&
          configuration->rel_x == other->rel_x &&
          configuration->rel_y == other->rel_y &&
          configuration->has_size == other->has_size &&
          configuration->is_resizing == other->is_resizing &&
          configuration->width == other->width &&
          configuration->height == other->height &&
          configuration->scale == other->scale &&
          configuration->gravity == other->gravity &&
          configuration->flags == other->flags &&
          configuration->bounds_width == other->bounds_width &&
          configuration->bounds_height == other->bounds_height &&
          configuration->is_suspended == other->is_suspended &&
          meta_window_config_is_equivalent (configuration->config,
                                            other->config));
}
