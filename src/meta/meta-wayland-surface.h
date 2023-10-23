/*
 * Copyright 2023 Collabora Ltd.
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

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define META_TYPE_WAYLAND_SURFACE (meta_wayland_surface_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaWaylandSurface,
                      meta_wayland_surface,
                      META, WAYLAND_SURFACE,
                      GObject);

META_EXPORT
MetaWindow *meta_wayland_surface_get_window (MetaWaylandSurface *surface);

G_END_DECLS
