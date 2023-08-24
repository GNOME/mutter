/*
 * Copyright (C) 2022 Red Hat Inc.
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

#include "cogl/cogl.h"
#include "meta/meta-multi-texture.h"
#include "wayland/meta-wayland-types.h"

typedef struct _MetaWaylandSinglePixelBuffer MetaWaylandSinglePixelBuffer;

gboolean meta_wayland_single_pixel_buffer_attach (MetaWaylandBuffer  *buffer,
                                                  MetaMultiTexture  **texture,
                                                  GError            **error);

MetaWaylandSinglePixelBuffer * meta_wayland_single_pixel_buffer_from_buffer (MetaWaylandBuffer *buffer);

void meta_wayland_init_single_pixel_buffer_manager (MetaWaylandCompositor *compositor);

void meta_wayland_single_pixel_buffer_free (MetaWaylandSinglePixelBuffer *single_pixel_buffer);

gboolean meta_wayland_single_pixel_buffer_is_opaque_black (MetaWaylandSinglePixelBuffer *single_pixel_buffer);
