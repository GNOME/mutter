/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
 * Copyright (C) 2011  Robert Bosch Car Multimedia GmbH.
 * Copyright (C) 2012  Collabora Ltd.
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
 * Author:
 *   Emanuele Aina <emanuele.aina@collabora.com>
 *
 * Based on ClutterDragAction, ClutterSwipeAction, and MxKineticScrollView,
 * written by:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 *   Tomeu Vizoso <tomeu.vizoso@collabora.co.uk>
 *   Chris Lord <chris@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-gesture-action.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_PAN_ACTION               (clutter_pan_action_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterPanAction,
                          clutter_pan_action,
                          CLUTTER,
                          PAN_ACTION,
                          ClutterGestureAction)

/**
 * ClutterPanActionClass:
 * @pan: class handler for the #ClutterPanAction::pan signal
 * @pan_stopped: class handler for the #ClutterPanAction::pan-stopped signal
 *
 * The #ClutterPanActionClass structure contains
 * only private data.
 */
struct _ClutterPanActionClass
{
  /*< private >*/
  ClutterGestureActionClass parent_class;

  /*< public >*/
  void     (* pan_stopped)       (ClutterPanAction    *action,
                                  ClutterActor        *actor);
};

CLUTTER_EXPORT
ClutterAction * clutter_pan_action_new                      (void);
CLUTTER_EXPORT
void            clutter_pan_action_set_pan_axis             (ClutterPanAction *self,
                                                             ClutterPanAxis    axis);
CLUTTER_EXPORT
ClutterPanAxis clutter_pan_action_get_pan_axis              (ClutterPanAction *self);
CLUTTER_EXPORT
void            clutter_pan_action_set_interpolate          (ClutterPanAction *self,
                                                             gboolean          should_interpolate);
CLUTTER_EXPORT
gboolean        clutter_pan_action_get_interpolate          (ClutterPanAction *self);
CLUTTER_EXPORT
void            clutter_pan_action_set_deceleration         (ClutterPanAction *self,
                                                             gdouble           rate);
CLUTTER_EXPORT
gdouble         clutter_pan_action_get_deceleration         (ClutterPanAction *self);
CLUTTER_EXPORT
void            clutter_pan_action_set_acceleration_factor  (ClutterPanAction *self,
                                                             gdouble           factor);
CLUTTER_EXPORT
gdouble         clutter_pan_action_get_acceleration_factor  (ClutterPanAction *self);
CLUTTER_EXPORT
void            clutter_pan_action_get_interpolated_coords  (ClutterPanAction *self,
                                                             gfloat           *interpolated_x,
                                                             gfloat           *interpolated_y);
CLUTTER_EXPORT
gfloat          clutter_pan_action_get_interpolated_delta   (ClutterPanAction *self,
                                                             gfloat           *delta_x,
                                                             gfloat           *delta_y);
CLUTTER_EXPORT
gfloat          clutter_pan_action_get_motion_delta         (ClutterPanAction *self,
                                                             guint             point,
                                                             gfloat           *delta_x,
                                                             gfloat           *delta_y);
CLUTTER_EXPORT
void            clutter_pan_action_get_motion_coords        (ClutterPanAction *self,
                                                             guint             point,
                                                             gfloat           *motion_x,
                                                             gfloat           *motion_y);
CLUTTER_EXPORT
gfloat          clutter_pan_action_get_constrained_motion_delta (ClutterPanAction *self,
                                                                 guint             point,
                                                                 gfloat           *delta_x,
                                                                 gfloat           *delta_y);
G_END_DECLS
