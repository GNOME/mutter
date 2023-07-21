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

#pragma once

#include "clutter/clutter.h"
#include "clutter/clutter-stage-private.h"

#define CLUTTER_TYPE_INPUT_ONLY_ACTOR (clutter_input_only_actor_get_type ())
G_DECLARE_FINAL_TYPE (ClutterInputOnlyActor, clutter_input_only_actor,
                      CLUTTER, INPUT_ONLY_ACTOR, ClutterActor)

ClutterInputOnlyActor * clutter_input_only_actor_new (ClutterEventHandler event_handler,
                                                      gpointer            user_data,
                                                      GDestroyNotify      destroy);
