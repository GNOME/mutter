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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#ifndef META_POINTER_CONFINEMENT_WAYLAND_H
#define META_POINTER_CONFINEMENT_WAYLAND_H

#include <glib-object.h>

#include "backends/meta-pointer-constraint.h"
#include "wayland/meta-wayland-pointer-constraints.h"

G_BEGIN_DECLS

#define META_TYPE_POINTER_CONFINEMENT_WAYLAND (meta_pointer_confinement_wayland_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaPointerConfinementWayland,
                          meta_pointer_confinement_wayland,
                          META, POINTER_CONFINEMENT_WAYLAND,
                          GObject)

struct _MetaPointerConfinementWaylandClass
{
  GObjectClass parent_class;

  MetaPointerConstraint * (*create_constraint) (MetaPointerConfinementWayland *confinement);
};

MetaPointerConfinementWayland *meta_pointer_confinement_wayland_new (MetaWaylandPointerConstraint *constraint);
MetaWaylandPointerConstraint *
  meta_pointer_confinement_wayland_get_wayland_pointer_constraint (MetaPointerConfinementWayland *confinement);
void meta_pointer_confinement_wayland_enable (MetaPointerConfinementWayland *confinement);
void meta_pointer_confinement_wayland_disable (MetaPointerConfinementWayland *confinement);

G_END_DECLS

#endif /* META_CONFINEMENT_WAYLAND_H */
