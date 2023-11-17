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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <glib-object.h>

#include "clutter-macros.h"
#include "clutter-enums.h"

#define CLUTTER_TYPE_GRAB (clutter_grab_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterGrab, clutter_grab, CLUTTER, GRAB, GObject)

CLUTTER_EXPORT
void clutter_grab_dismiss (ClutterGrab *grab);

CLUTTER_EXPORT
ClutterGrabState clutter_grab_get_seat_state (ClutterGrab *grab);

CLUTTER_EXPORT
gboolean clutter_grab_is_revoked (ClutterGrab *grab);
