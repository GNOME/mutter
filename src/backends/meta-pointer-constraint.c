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

/**
 * MetaPointerConstraint:
 *
 * Pointer client constraints.
 *
 * A MetaPointerConstraint can be used to implement any kind of pointer
 * constraint as requested by a client, such as cursor lock.
 *
 * Examples of pointer constraints are "pointer confinement" and "pointer
 * locking" (as defined in the wayland pointer constraint protocol extension),
 * which restrict movement in relation to a given client.
 */

#include "config.h"

#include "backends/meta-pointer-constraint.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-pointer-constraint-native.h"
#endif

#include <glib-object.h>

struct _MetaPointerConstraint
{
  GObject parent_instance;
  MtkRegion *region;
  double min_edge_distance;
};

G_DEFINE_TYPE (MetaPointerConstraint, meta_pointer_constraint, G_TYPE_OBJECT);

G_DEFINE_TYPE (MetaPointerConstraintImpl, meta_pointer_constraint_impl,
               G_TYPE_OBJECT);

static void
meta_pointer_constraint_finalize (GObject *object)
{
  MetaPointerConstraint *constraint = META_POINTER_CONSTRAINT (object);

  g_clear_pointer (&constraint->region, mtk_region_unref);

  G_OBJECT_CLASS (meta_pointer_constraint_parent_class)->finalize (object);
}

static void
meta_pointer_constraint_init (MetaPointerConstraint *constraint)
{
}

static void
meta_pointer_constraint_class_init (MetaPointerConstraintClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_pointer_constraint_finalize;
}


MetaPointerConstraint *
meta_pointer_constraint_new (const MtkRegion *region,
                             double           min_edge_distance)
{
  MetaPointerConstraint *constraint;

  constraint = g_object_new (META_TYPE_POINTER_CONSTRAINT, NULL);
  constraint->region = mtk_region_copy (region);
  constraint->min_edge_distance = min_edge_distance;

  return constraint;
}

MtkRegion *
meta_pointer_constraint_get_region (MetaPointerConstraint *constraint)
{
  return constraint->region;
}

double
meta_pointer_constraint_get_min_edge_distance (MetaPointerConstraint *constraint)
{
  return constraint->min_edge_distance;
}

static void
meta_pointer_constraint_impl_init (MetaPointerConstraintImpl *constraint_impl)
{
}

static void
meta_pointer_constraint_impl_class_init (MetaPointerConstraintImplClass *klass)
{
}

/**
 * meta_pointer_constraint_impl_constrain:
 * @constraint_impl: a #MetaPointerConstraintImpl.
 * @device; the device of the pointer.
 * @time: the timestamp (in ms) of the event.
 * @prev_x: X-coordinate of the previous pointer position.
 * @prev_y: Y-coordinate of the previous pointer position.
 * @x: The modifiable X-coordinate to which the pointer would like to go to.
 * @y: The modifiable Y-coordinate to which the pointer would like to go to.
 *
 * Constrains the pointer movement from point (@prev_x, @prev_y) to (@x, @y),
 * if needed.
 */
void
meta_pointer_constraint_impl_constrain (MetaPointerConstraintImpl *constraint_impl,
                                        ClutterInputDevice        *device,
                                        uint32_t                   time,
                                        float                      prev_x,
                                        float                      prev_y,
                                        float                     *x,
                                        float                     *y)
{
  META_POINTER_CONSTRAINT_IMPL_GET_CLASS (constraint_impl)->constrain (constraint_impl,
                                                                       device,
                                                                       time,
                                                                       prev_x, prev_y,
                                                                       x, y);
}

void
meta_pointer_constraint_impl_ensure_constrained (MetaPointerConstraintImpl *constraint_impl,
                                                 ClutterInputDevice        *device)
{
  META_POINTER_CONSTRAINT_IMPL_GET_CLASS (constraint_impl)->ensure_constrained (constraint_impl,
                                                                                device);
}
