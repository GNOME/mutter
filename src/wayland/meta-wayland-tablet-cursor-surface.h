/*
 * Wayland Support
 *
 * Copyright (C) 2016 Red Hat, Inc.
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

#pragma once

#include "wayland/meta-wayland-cursor-surface.h"

#define META_TYPE_WAYLAND_TABLET_CURSOR_SURFACE (meta_wayland_tablet_cursor_surface_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandTabletCursorSurface,
                      meta_wayland_tablet_cursor_surface,
                      META, WAYLAND_TABLET_CURSOR_SURFACE,
                      MetaWaylandCursorSurface)
