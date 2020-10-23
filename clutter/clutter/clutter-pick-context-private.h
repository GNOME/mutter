/*
 * Copyright (C) 2019 Red Hat Inc.
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

#ifndef CLUTTER_PICK_CONTEXT_PRIVATE_H
#define CLUTTER_PICK_CONTEXT_PRIVATE_H

#include "clutter-pick-context.h"
#include "clutter-pick-stack-private.h"

ClutterPickContext *
clutter_pick_context_new_for_view (ClutterStageView         *view,
                                   ClutterPickMode           mode,
                                   const graphene_point3d_t *point,
                                   const graphene_ray_t     *ray);

ClutterPickStack *
clutter_pick_context_steal_stack (ClutterPickContext *pick_context);

gboolean
clutter_pick_context_intersects_box (ClutterPickContext   *pick_context,
                                     const graphene_box_t *box);

#endif /* CLUTTER_PICK_CONTEXT_PRIVATE_H */
