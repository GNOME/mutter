/*
 * Copyright (C) 2022 Jonas Dreßler <verdre@v0yd.nl>
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

#include "clutter/clutter-press-gesture.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_CLICK_GESTURE (clutter_click_gesture_get_type ())

CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterClickGesture, clutter_click_gesture,
                      CLUTTER, CLICK_GESTURE, ClutterPressGesture)

CLUTTER_EXPORT
ClutterAction * clutter_click_gesture_new (void);

CLUTTER_EXPORT
unsigned int clutter_click_gesture_get_n_clicks_required (ClutterClickGesture *self);

CLUTTER_EXPORT
void clutter_click_gesture_set_n_clicks_required (ClutterClickGesture *self,
                                                  unsigned int         n_clicks_required);

CLUTTER_EXPORT
gboolean clutter_click_gesture_get_recognize_on_press (ClutterClickGesture *self);

CLUTTER_EXPORT
void clutter_click_gesture_set_recognize_on_press (ClutterClickGesture *self,
                                                   gboolean             recognize_on_press);

G_END_DECLS
