/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012 Intel Corporation
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
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"
#include "clutter/clutter-transition.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_TRANSITION_GROUP                   (clutter_transition_group_get_type ())

CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterTransitionGroup,
                      clutter_transition_group,
                      CLUTTER,
                      TRANSITION_GROUP,
                      ClutterTransition)

CLUTTER_EXPORT
ClutterTransition *     clutter_transition_group_new            (void);

CLUTTER_EXPORT
void                    clutter_transition_group_add_transition         (ClutterTransitionGroup *group,
                                                                         ClutterTransition      *transition);
CLUTTER_EXPORT
void                    clutter_transition_group_remove_transition      (ClutterTransitionGroup *group,
                                                                         ClutterTransition      *transition);
CLUTTER_EXPORT
void                    clutter_transition_group_remove_all             (ClutterTransitionGroup *group);

G_END_DECLS
