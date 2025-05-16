/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2021 Red Hat Inc.
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
 */

#pragma once

#include "clutter/clutter-types.h"

CLUTTER_EXPORT
void clutter_seat_destroy (ClutterSeat *seat);

ClutterGrabState clutter_seat_grab (ClutterSeat *seat,
                                    uint32_t     time);
void clutter_seat_ungrab (ClutterSeat *seat,
                          uint32_t     time);

CLUTTER_EXPORT
void clutter_seat_init_pointer_position (ClutterSeat *seat,
                                         float        x,
                                         float        y);

CLUTTER_EXPORT
ClutterInputDevice * clutter_seat_get_pointer (ClutterSeat *seat);

CLUTTER_EXPORT
ClutterInputDevice * clutter_seat_get_keyboard (ClutterSeat *seat);
