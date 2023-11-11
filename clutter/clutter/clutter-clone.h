/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2008  Intel Corporation.
 *
 * Authored By: Robert Bragg <robert@linux.intel.com>
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
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-actor.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_CLONE              (clutter_clone_get_type ())

/**
 * ClutterCloneClass:
 *
 * The #ClutterCloneClass structure contains only private data
 */
struct _ClutterCloneClass
{
  /*< private >*/
  ClutterActorClass parent_class;
};

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterClone,
                          clutter_clone,
                          CLUTTER, CLONE,
                          ClutterActor)

CLUTTER_EXPORT
ClutterActor *  clutter_clone_new               (ClutterActor *source);
CLUTTER_EXPORT
void            clutter_clone_set_source        (ClutterClone *self,
                                                 ClutterActor *source);
CLUTTER_EXPORT
ClutterActor *  clutter_clone_get_source        (ClutterClone *self);

G_END_DECLS
