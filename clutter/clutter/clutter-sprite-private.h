/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 Red Hat Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-sprite.h"

CLUTTER_EXPORT
ClutterInputDevice * clutter_sprite_get_device (ClutterSprite *sprite);

CLUTTER_EXPORT
ClutterEventSequence * clutter_sprite_get_sequence (ClutterSprite *sprite);

void clutter_sprite_update (ClutterSprite    *sprite,
                            graphene_point_t  coords,
                            MtkRegion        *clear_area);

void clutter_sprite_update_coords (ClutterSprite    *sprite,
                                   graphene_point_t  coords);

gboolean clutter_sprite_point_in_clear_area (ClutterSprite    *sprite,
                                             graphene_point_t  point);

void clutter_sprite_maybe_break_implicit_grab (ClutterSprite *sprite,
                                               ClutterActor  *actor);

void clutter_sprite_maybe_lost_implicit_grab (ClutterSprite *sprite);

void clutter_sprite_remove_all_actors_from_chain (ClutterSprite *sprite);
