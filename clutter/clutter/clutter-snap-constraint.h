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

#define CLUTTER_TYPE_SNAP_CONSTRAINT    (clutter_snap_constraint_get_type ())

CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterSnapConstraint, clutter_snap_constraint,
                      CLUTTER, SNAP_CONSTRAINT, ClutterConstraint)

CLUTTER_EXPORT
ClutterConstraint *     clutter_snap_constraint_new             (ClutterActor          *source,
                                                                 ClutterSnapEdge        from_edge,
                                                                 ClutterSnapEdge        to_edge,
                                                                 gfloat                 offset);

CLUTTER_EXPORT
void                    clutter_snap_constraint_set_source      (ClutterSnapConstraint *constraint,
                                                                 ClutterActor          *source);
CLUTTER_EXPORT
ClutterActor *          clutter_snap_constraint_get_source      (ClutterSnapConstraint *constraint);
CLUTTER_EXPORT
void                    clutter_snap_constraint_set_edges       (ClutterSnapConstraint *constraint,
                                                                 ClutterSnapEdge        from_edge,
                                                                 ClutterSnapEdge        to_edge);
CLUTTER_EXPORT
void                    clutter_snap_constraint_get_edges       (ClutterSnapConstraint *constraint,
                                                                 ClutterSnapEdge       *from_edge,
                                                                 ClutterSnapEdge       *to_edge);
CLUTTER_EXPORT
void                    clutter_snap_constraint_set_offset      (ClutterSnapConstraint *constraint,
                                                                 gfloat                 offset);
CLUTTER_EXPORT
gfloat                  clutter_snap_constraint_get_offset      (ClutterSnapConstraint *constraint);

G_END_DECLS
