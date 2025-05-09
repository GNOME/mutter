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

#pragma once

#include <glib.h>
#include <stdint.h>

#include "core/window-private.h"
#include "meta/meta-window-config.h"
#include "wayland/meta-wayland-types.h"

struct _MetaWaylandWindowConfiguration
{
  grefcount ref_count;

  uint32_t serial;

  gboolean has_position;
  int x;
  int y;

  gboolean has_relative_position;
  int rel_x;
  int rel_y;

  gboolean has_size;
  gboolean is_resizing;
  int width;
  int height;

  int scale;
  MetaGravity gravity;
  MetaMoveResizeFlags flags;

  int bounds_width;
  int bounds_height;

  gboolean is_fullscreen;
  gboolean is_floating;
  gboolean is_suspended;
};

MetaWaylandWindowConfiguration * meta_wayland_window_configuration_new (MetaWindow          *window,
                                                                        MtkRectangle         rect,
                                                                        int                  max_width,
                                                                        int                  max_height,
                                                                        int                  scale,
                                                                        MetaMoveResizeFlags  flags,
                                                                        MetaGravity          gravity);

MetaWaylandWindowConfiguration * meta_wayland_window_configuration_new_relative (MetaWindow *window,
                                                                                 int         rel_x,
                                                                                 int         rel_y,
                                                                                 int         width,
                                                                                 int         height,
                                                                                 int         scale);

MetaWaylandWindowConfiguration * meta_wayland_window_configuration_new_empty (int bounds_width,
                                                                              int bounds_height,
                                                                              int scale);

MetaWaylandWindowConfiguration * meta_wayland_window_configuration_new_from_other (MetaWaylandWindowConfiguration *other);

MetaWaylandWindowConfiguration * meta_wayland_window_configuration_ref (MetaWaylandWindowConfiguration *configuration);

void meta_wayland_window_configuration_unref (MetaWaylandWindowConfiguration *configuration);

gboolean meta_wayland_window_configuration_is_equivalent (MetaWaylandWindowConfiguration *configuration,
                                                          MetaWaylandWindowConfiguration *other);

MetaWindowConfig * meta_window_config_new_from_wayland_window_configuration (MetaWindow                     *window,
                                                                             MetaWaylandWindowConfiguration *configuration);

MetaWaylandWindowConfiguration * meta_wayland_window_configuration_apply_window_config (MetaWindow                     *window,
                                                                                        MetaWaylandWindowConfiguration *configuration,
                                                                                        MetaWindowConfig               *window_config);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaWaylandWindowConfiguration,
                               meta_wayland_window_configuration_unref)
