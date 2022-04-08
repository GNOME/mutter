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

#include <clutter/clutter-action.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_GESTURE (clutter_gesture_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterGesture, clutter_gesture,
                          CLUTTER, GESTURE, ClutterAction)

struct _ClutterGestureClass
{
  ClutterActionClass parent_class;

  /**
   * ClutterGestureClass::should_handle_sequence: (skip)
   */
  gboolean (* should_handle_sequence) (ClutterGesture     *self,
                                       const ClutterEvent *sequence_begin_event);

  /**
   * ClutterGestureClass::point_began: (skip)
   */
  void (* point_began) (ClutterGesture *self,
                        unsigned int    sequence_index);

  /**
   * ClutterGestureClass::point_moved: (skip)
   */
  void (* point_moved) (ClutterGesture *self,
                        unsigned int    sequence_index);

  /**
   * ClutterGestureClass::point_ended: (skip)
   */
  void (* point_ended) (ClutterGesture *self,
                        unsigned int    sequence_index);

  /**
   * ClutterGestureClass::sequences_cancelled: (skip)
   */
  void (* sequences_cancelled) (ClutterGesture *self,
                                unsigned int   *sequences,
                                unsigned int    n_sequences);

  /**
   * ClutterGestureClass::state_changed: (skip)
   */
  void (* state_changed) (ClutterGesture      *self,
                          ClutterGestureState  old_state,
                          ClutterGestureState  new_state);

  /**
   * ClutterGestureClass::crossing_event: (skip)
   */
  void (* crossing_event) (ClutterGesture    *self,
                           unsigned int       sequence_index,
                           ClutterEventType   type,
                           uint32_t           time,
                           ClutterEventFlags  flags,
                           ClutterActor      *source_actor,
                           ClutterActor      *related_actor);

  /**
   * ClutterGestureClass::may_recognize: (skip)
   */
  gboolean (* may_recognize) (ClutterGesture *self);

  /**
   * ClutterGestureClass::should_influence: (skip)
   */
  void (* should_influence) (ClutterGesture *self,
                             ClutterGesture *other_gesture,
                             gboolean       *cancel_on_recognizing);

  /**
   * ClutterGestureClass::should_be_influenced_by: (skip)
   */
  void (* should_be_influenced_by) (ClutterGesture *self,
                                    ClutterGesture *other_gesture,
                                    gboolean       *cancelled_on_recognizing);
};

CLUTTER_EXPORT
void clutter_gesture_set_state (ClutterGesture      *self,
                                ClutterGestureState  state);

CLUTTER_EXPORT
ClutterGestureState clutter_gesture_get_state (ClutterGesture *self);

CLUTTER_EXPORT
void clutter_gesture_cancel (ClutterGesture *self);

CLUTTER_EXPORT
void clutter_gesture_reset_state_machine (ClutterGesture *self);

CLUTTER_EXPORT
unsigned int clutter_gesture_get_n_points (ClutterGesture *self);

CLUTTER_EXPORT
unsigned int * clutter_gesture_get_points (ClutterGesture *self,
                                           size_t         *n_points);

CLUTTER_EXPORT
void clutter_gesture_get_point_coords (ClutterGesture   *self,
                                       int               point_index,
                                       graphene_point_t *coords_out);

CLUTTER_EXPORT
void clutter_gesture_get_point_coords_abs (ClutterGesture   *self,
                                           int               point_index,
                                           graphene_point_t *coords_out);

CLUTTER_EXPORT
void clutter_gesture_get_point_begin_coords (ClutterGesture   *self,
                                             int               point_index,
                                             graphene_point_t *coords_out);

CLUTTER_EXPORT
void clutter_gesture_get_point_begin_coords_abs (ClutterGesture   *self,
                                                 int               point_index,
                                                 graphene_point_t *coords_out);

CLUTTER_EXPORT
void clutter_gesture_get_point_previous_coords (ClutterGesture   *self,
                                                int               point_index,
                                                graphene_point_t *coords_out);

CLUTTER_EXPORT
void clutter_gesture_get_point_previous_coords_abs (ClutterGesture   *self,
                                                    int               point_index,
                                                    graphene_point_t *coords_out);

CLUTTER_EXPORT
const ClutterEvent * clutter_gesture_get_point_event (ClutterGesture  *self,
                                                      int              point_index);

CLUTTER_EXPORT
void clutter_gesture_can_not_cancel (ClutterGesture *self,
                                     ClutterGesture *other_gesture);

G_END_DECLS
