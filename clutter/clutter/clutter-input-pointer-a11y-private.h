/*
 * Copyright (C) 2019 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Olivier Fourdan <ofourdan@redhat.com>
 */

#pragma once

#include "clutter/clutter-types.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-seat.h"

G_BEGIN_DECLS

CLUTTER_EXPORT
void _clutter_seat_init_a11y (ClutterSeat *seat);

CLUTTER_EXPORT
void _clutter_seat_shutdown_a11y (ClutterSeat *seat);

CLUTTER_EXPORT
void _clutter_seat_a11y_on_motion_event (ClutterSeat *seat,
                                         float        x,
                                         float        y);
CLUTTER_EXPORT
void _clutter_seat_a11y_on_button_event (ClutterSeat *seat,
                                         int          button,
                                         gboolean     pressed);
CLUTTER_EXPORT
gboolean _clutter_seat_is_pointer_a11y_enabled (ClutterSeat *seat);

CLUTTER_EXPORT
void clutter_seat_a11y_update (ClutterSeat        *seat,
                               const ClutterEvent *event);

G_END_DECLS
