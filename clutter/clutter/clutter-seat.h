/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */
#ifndef CLUTTER_SEAT_H
#define CLUTTER_SEAT_H

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"

#define CLUTTER_TYPE_SEAT (clutter_seat_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterSeat, clutter_seat,
			  CLUTTER, SEAT, GObject)

typedef struct _ClutterSeatClass ClutterSeatClass;

struct _ClutterSeatClass
{
  GObjectClass parent_class;

  ClutterInputDevice * (* get_pointer)  (ClutterSeat *seat);
  ClutterInputDevice * (* get_keyboard) (ClutterSeat *seat);

  GList * (* list_devices) (ClutterSeat *seat);

  void (* bell_notify) (ClutterSeat *seat);
};

CLUTTER_EXPORT
ClutterInputDevice * clutter_seat_get_pointer  (ClutterSeat *seat);
CLUTTER_EXPORT
ClutterInputDevice * clutter_seat_get_keyboard (ClutterSeat *seat);
CLUTTER_EXPORT
GList * clutter_seat_list_devices (ClutterSeat *seat);
CLUTTER_EXPORT
void clutter_seat_bell_notify (ClutterSeat *seat);

#endif /* CLUTTER_SEAT_H */
