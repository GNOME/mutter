/*
 * Copyright (C) 2023 Jonas Dreßler <verdre@v0yd.nl>
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

#include "clutter/clutter-gesture.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_PRESS_GESTURE (clutter_press_gesture_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterPressGesture, clutter_press_gesture,
                          CLUTTER, PRESS_GESTURE, ClutterGesture)

struct _ClutterPressGestureClass
{
  ClutterGestureClass parent_class;

  void (* press) (ClutterPressGesture *self);
  void (* release) (ClutterPressGesture *self);
  void (* long_press) (ClutterPressGesture *self);
};

CLUTTER_EXPORT
gboolean clutter_press_gesture_get_pressed (ClutterPressGesture *self);

CLUTTER_EXPORT
int clutter_press_gesture_get_cancel_threshold (ClutterPressGesture *self);

CLUTTER_EXPORT
void clutter_press_gesture_set_cancel_threshold (ClutterPressGesture *self,
                                                 int                  cancel_threshold);

CLUTTER_EXPORT
int clutter_press_gesture_get_long_press_duration_ms (ClutterPressGesture *self);

CLUTTER_EXPORT
void clutter_press_gesture_set_long_press_duration_ms (ClutterPressGesture *self,
                                                       int                  long_press_duration_ms);

CLUTTER_EXPORT
unsigned int clutter_press_gesture_get_button (ClutterPressGesture *self);

CLUTTER_EXPORT
ClutterModifierType clutter_press_gesture_get_state (ClutterPressGesture *self);

CLUTTER_EXPORT
void clutter_press_gesture_get_coords (ClutterPressGesture *self,
                                       graphene_point_t    *coords_out);

CLUTTER_EXPORT
void clutter_press_gesture_get_coords_abs (ClutterPressGesture *self,
                                           graphene_point_t    *coords_out);

CLUTTER_EXPORT
unsigned int clutter_press_gesture_get_n_presses (ClutterPressGesture *self);

CLUTTER_EXPORT
unsigned int clutter_press_gesture_get_required_button (ClutterPressGesture *self);

CLUTTER_EXPORT
void clutter_press_gesture_set_required_button (ClutterPressGesture *self,
                                                unsigned int         required_button);

G_END_DECLS
