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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#include "meta/meta-context.h"

G_BEGIN_DECLS

#define META_TYPE_WAYLAND_COMPOSITOR (meta_wayland_compositor_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaWaylandCompositor,
                      meta_wayland_compositor,
                      META, WAYLAND_COMPOSITOR,
                      GObject)

META_EXPORT
MetaWaylandCompositor *meta_context_get_wayland_compositor (MetaContext *context);

META_EXPORT
struct wl_display *meta_wayland_compositor_get_wayland_display (MetaWaylandCompositor *compositor);

G_END_DECLS
