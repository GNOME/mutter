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

#include "clutter/clutter-gesture.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_PAN_GESTURE (clutter_pan_gesture_get_type ())

CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterPanGesture, clutter_pan_gesture,
                      CLUTTER, PAN_GESTURE, ClutterGesture)

CLUTTER_EXPORT
ClutterAction * clutter_pan_gesture_new (void);

CLUTTER_EXPORT
unsigned int clutter_pan_gesture_get_begin_threshold (ClutterPanGesture *self);

CLUTTER_EXPORT
void clutter_pan_gesture_set_begin_threshold (ClutterPanGesture *self,
                                              unsigned int       begin_threshold);

CLUTTER_EXPORT
ClutterPanAxis clutter_pan_gesture_get_pan_axis (ClutterPanGesture *self);

CLUTTER_EXPORT
void clutter_pan_gesture_set_pan_axis (ClutterPanGesture *self,
                                       ClutterPanAxis     axis);

CLUTTER_EXPORT
unsigned int clutter_pan_gesture_get_min_n_points (ClutterPanGesture *self);

CLUTTER_EXPORT
void clutter_pan_gesture_set_min_n_points (ClutterPanGesture *self,
                                           unsigned int       min_n_points);

CLUTTER_EXPORT
unsigned int clutter_pan_gesture_get_max_n_points (ClutterPanGesture *self);

CLUTTER_EXPORT
void clutter_pan_gesture_set_max_n_points (ClutterPanGesture *self,
                                           unsigned int       max_n_points);

CLUTTER_EXPORT
void clutter_pan_gesture_get_begin_centroid (ClutterPanGesture *self,
                                             graphene_point_t  *centroid_out);

CLUTTER_EXPORT
void clutter_pan_gesture_get_begin_centroid_abs (ClutterPanGesture *self,
                                                 graphene_point_t  *centroid_out);

CLUTTER_EXPORT
void clutter_pan_gesture_get_centroid (ClutterPanGesture *self,
                                       graphene_point_t  *centroid_out);

CLUTTER_EXPORT
void clutter_pan_gesture_get_centroid_abs (ClutterPanGesture *self,
                                           graphene_point_t  *centroid_out);

CLUTTER_EXPORT
void clutter_pan_gesture_get_velocity (ClutterPanGesture *self,
                                       graphene_vec2_t   *velocity_out);

CLUTTER_EXPORT
void clutter_pan_gesture_get_velocity_abs (ClutterPanGesture *self,
                                           graphene_vec2_t   *velocity_out);

CLUTTER_EXPORT
void clutter_pan_gesture_get_delta (ClutterPanGesture *self,
                                    graphene_vec2_t   *delta_out);

CLUTTER_EXPORT
void clutter_pan_gesture_get_accumulated_delta (ClutterPanGesture *self,
                                                graphene_vec2_t   *accumulated_delta_out);

CLUTTER_EXPORT
void clutter_pan_gesture_get_delta_abs (ClutterPanGesture *self,
                                        graphene_vec2_t   *delta_out);

CLUTTER_EXPORT
void clutter_pan_gesture_get_accumulated_delta_abs (ClutterPanGesture *self,
                                                    graphene_vec2_t   *accumulated_delta_out);

G_END_DECLS
