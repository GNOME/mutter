/*
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

#ifndef META_WAYLAND_SURFACE_HELPER_H
#define META_WAYLAND_SURFACE_HELPER_H

#include "wayland/meta-wayland-surface.h"

cairo_region_t * meta_wayland_surface_helper_surface_to_buffer_region (MetaWaylandSurface *surface,
                                                                       cairo_region_t     *region);

MetaMonitorTransform meta_wayland_surface_helper_transform_from_wl_output_transform (int32_t transform_value);

#endif
