/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012  Intel Corporation
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

#include "clutter/clutter-types.h"
#include "clutter/clutter-actor.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_SCROLL_ACTOR               (clutter_scroll_actor_get_type ())

/**
 * ClutterScrollActorClass:
 *
 * The #ClutterScrollActor structure contains only
 * private data.
 */
struct _ClutterScrollActorClass
{
  /*< private >*/
  ClutterActorClass parent_instance;
};

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterScrollActor,
                          clutter_scroll_actor,
                          CLUTTER, SCROLL_ACTOR,
                          ClutterActor)

CLUTTER_EXPORT
ClutterActor *          clutter_scroll_actor_new                (void);

CLUTTER_EXPORT
void                    clutter_scroll_actor_set_scroll_mode    (ClutterScrollActor *actor,
                                                                 ClutterScrollMode   mode);
CLUTTER_EXPORT
ClutterScrollMode       clutter_scroll_actor_get_scroll_mode    (ClutterScrollActor *actor);

CLUTTER_EXPORT
void                    clutter_scroll_actor_scroll_to_point    (ClutterScrollActor     *actor,
                                                                 const graphene_point_t *point);
CLUTTER_EXPORT
void                    clutter_scroll_actor_scroll_to_rect     (ClutterScrollActor    *actor,
                                                                 const graphene_rect_t *rect);

G_END_DECLS
