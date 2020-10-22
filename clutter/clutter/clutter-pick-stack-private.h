/*
 * Copyright (C) 2020 Endless OS Foundation, LLC
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

#ifndef CLUTTER_PICK_STACK_PRIVATE_H
#define CLUTTER_PICK_STACK_PRIVATE_H

#include <glib-object.h>

#include "clutter-macros.h"
#include "clutter-stage-view.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_PICK_STACK (clutter_pick_stack_get_type ())

typedef struct _ClutterPickStack ClutterPickStack;

GType clutter_pick_stack_get_type (void) G_GNUC_CONST;

ClutterPickStack * clutter_pick_stack_new (CoglContext *context);

ClutterPickStack * clutter_pick_stack_ref (ClutterPickStack *pick_stack);

void clutter_pick_stack_unref (ClutterPickStack *pick_stack);

void clutter_pick_stack_seal (ClutterPickStack *pick_stack);

void clutter_pick_stack_log_pick (ClutterPickStack      *pick_stack,
                                  const ClutterActorBox *box,
                                  ClutterActor          *actor);

void clutter_pick_stack_push_clip (ClutterPickStack      *pick_stack,
                                   const ClutterActorBox *box);

void clutter_pick_stack_pop_clip (ClutterPickStack *pick_stack);

void clutter_pick_stack_push_transform (ClutterPickStack        *pick_stack,
                                        const graphene_matrix_t *transform);

void clutter_pick_stack_get_transform (ClutterPickStack  *pick_stack,
                                       graphene_matrix_t *out_transform);

void clutter_pick_stack_pop_transform (ClutterPickStack *pick_stack);

ClutterActor *
clutter_pick_stack_search_actor (ClutterPickStack         *pick_stack,
                                 const graphene_point3d_t *point,
                                 const graphene_ray_t     *ray);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterPickStack, clutter_pick_stack_unref)

G_END_DECLS

#endif /* CLUTTER_PICK_STACK_PRIVATE_H */
