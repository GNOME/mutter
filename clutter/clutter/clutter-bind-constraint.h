/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-constraint.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_BIND_CONSTRAINT    (clutter_bind_constraint_get_type ())

CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterBindConstraint, clutter_bind_constraint,
                      CLUTTER, BIND_CONSTRAINT, ClutterConstraint)

CLUTTER_EXPORT
ClutterConstraint *   clutter_bind_constraint_new            (ClutterActor          *source,
                                                              ClutterBindCoordinate  coordinate,
                                                              gfloat                 offset);

CLUTTER_EXPORT
void                  clutter_bind_constraint_set_source     (ClutterBindConstraint *constraint,
                                                              ClutterActor          *source);
CLUTTER_EXPORT
ClutterActor *        clutter_bind_constraint_get_source     (ClutterBindConstraint *constraint);
CLUTTER_EXPORT
void                  clutter_bind_constraint_set_coordinate (ClutterBindConstraint *constraint,
                                                              ClutterBindCoordinate  coordinate);
CLUTTER_EXPORT
ClutterBindCoordinate clutter_bind_constraint_get_coordinate (ClutterBindConstraint *constraint);
CLUTTER_EXPORT
void                  clutter_bind_constraint_set_offset     (ClutterBindConstraint *constraint,
                                                              gfloat                 offset);
CLUTTER_EXPORT
gfloat                clutter_bind_constraint_get_offset     (ClutterBindConstraint *constraint);

G_END_DECLS
