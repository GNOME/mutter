/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2022  Red Hat Inc.
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

#include "config.h"

#include "clutter/clutter-input-only-actor.h"

#include "clutter/clutter-input-only-action.h"

struct _ClutterInputOnlyActor
{
  ClutterActor parent;
};

G_DEFINE_TYPE (ClutterInputOnlyActor, clutter_input_only_actor,
               CLUTTER_TYPE_ACTOR)

static void
clutter_input_only_actor_class_init (ClutterInputOnlyActorClass *klass)
{
}

static void
clutter_input_only_actor_init (ClutterInputOnlyActor *input_only_actor)
{
}

ClutterInputOnlyActor *
clutter_input_only_actor_new (ClutterInputOnlyHandleEvent handle_event,
                              gpointer                    user_data,
                              GDestroyNotify              user_data_destroy)
{
  ClutterInputOnlyAction *input_only_action;

  input_only_action = clutter_input_only_action_new (handle_event,
                                                     user_data,
                                                     user_data_destroy);
  return g_object_new (CLUTTER_TYPE_INPUT_ONLY_ACTOR,
                       "reactive", TRUE,
                       "actions", input_only_action,
                       NULL);
}
