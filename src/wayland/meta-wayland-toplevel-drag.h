/*
 * Copyright (C) 2024 Igalia, S.L.
 *
 * Author: Nick Yamane <nickdiego@igalia.com>
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

#include "compositor/meta-window-drag.h"
#include "mtk/mtk.h"
#include "wayland/meta-wayland-types.h"
#include "wayland/meta-wayland-data-source.h"
#include "wayland/meta-wayland-input.h"

struct _MetaWaylandToplevelDrag
{
  struct wl_resource *resource;

  MetaWaylandDataSource *data_source;
  MetaWaylandSurface *dragged_surface;
  int32_t x_offset, y_offset;

  MetaWindowDrag *window_drag;
  MetaWaylandEventHandler *handler;
  gulong window_unmanaging_handler_id;
  gulong window_shown_handler_id;
  gulong drag_ended_handler_id;
  gulong source_destroyed_handler_id;
};

void
meta_wayland_init_toplevel_drag (MetaWaylandCompositor *compositor);

gboolean
meta_wayland_toplevel_drag_calc_origin_for_dragged_window (MetaWaylandToplevelDrag *drag,
                                                           MtkRectangle            *bounds_out);

void
meta_wayland_toplevel_drag_end (MetaWaylandToplevelDrag *drag);
