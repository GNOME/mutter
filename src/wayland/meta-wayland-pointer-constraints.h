/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#pragma once

#include <wayland-server.h>

#include "meta/window.h"
#include "wayland/meta-wayland-types.h"

#define META_TYPE_WAYLAND_POINTER_CONSTRAINT (meta_wayland_pointer_constraint_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandPointerConstraint,
                      meta_wayland_pointer_constraint,
                      META, WAYLAND_POINTER_CONSTRAINT,
                      GObject);

void meta_wayland_pointer_constraints_init (MetaWaylandCompositor *compositor);

MtkRegion * meta_wayland_pointer_constraint_calculate_effective_region (MetaWaylandPointerConstraint *constraint);

MetaWaylandSurface * meta_wayland_pointer_constraint_get_surface (MetaWaylandPointerConstraint *constraint);

MetaWaylandCompositor * meta_wayland_pointer_constraint_get_compositor (MetaWaylandPointerConstraint *constraint);
