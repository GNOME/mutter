/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#pragma once

#include "core/window-private.h"
#include "meta/window.h"
#include "wayland/meta-wayland-types.h"

G_BEGIN_DECLS

#define META_TYPE_WINDOW_WAYLAND (meta_window_wayland_get_type())
META_EXPORT_TEST
G_DECLARE_FINAL_TYPE (MetaWindowWayland, meta_window_wayland,
                      META, WINDOW_WAYLAND,
                      MetaWindow)

MetaWindow * meta_window_wayland_new       (MetaDisplay        *display,
                                            MetaWaylandSurface *surface);

void meta_window_wayland_finish_move_resize (MetaWindow              *window,
                                             MtkRectangle             new_geom,
                                             MetaWaylandSurfaceState *pending);

int meta_window_wayland_get_geometry_scale (MetaWindow *window);

void meta_window_place_with_placement_rule (MetaWindow        *window,
                                            MetaPlacementRule *placement_rule);

void meta_window_update_placement_rule (MetaWindow        *window,
                                        MetaPlacementRule *placement_rule);

META_EXPORT_TEST
MetaWaylandWindowConfiguration *
  meta_window_wayland_peek_configuration (MetaWindowWayland *wl_window,
                                          uint32_t           serial);

void meta_window_wayland_configure (MetaWindowWayland              *wl_window,
                                    MetaWaylandWindowConfiguration *configuration);

void meta_window_wayland_set_min_size (MetaWindow *window,
                                       int         width,
                                       int         height);

void meta_window_wayland_set_max_size (MetaWindow *window,
                                       int         width,
                                       int         height);

void meta_window_wayland_get_min_size (MetaWindow *window,
                                       int        *width,
                                       int        *height);


void meta_window_wayland_get_max_size (MetaWindow *window,
                                       int        *width,
                                       int        *height);

gboolean meta_window_wayland_is_resize (MetaWindowWayland *wl_window,
                                        int                width,
                                        int                height);

META_EXPORT_TEST
gboolean meta_window_wayland_is_acked_fullscreen (MetaWindowWayland *wl_window);

META_EXPORT_TEST
gboolean meta_window_wayland_get_pending_serial (MetaWindowWayland *wl_window,
                                                 uint32_t          *serial);

MetaWaylandClient * meta_window_wayland_get_client (MetaWindowWayland *wl_window);
