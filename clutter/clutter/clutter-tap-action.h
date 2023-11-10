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
 * Based on ClutterPanAction
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

#define CLUTTER_TYPE_TAP_ACTION               (clutter_tap_action_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterTapAction,
                          clutter_tap_action,
                          CLUTTER,
                          TAP_ACTION,
                          ClutterGestureAction)

/**
 * ClutterTapActionClass:
 * @tap: class handler for the #ClutterTapAction::tap signal
 *
 * The #ClutterTapActionClass structure contains
 * only private data.
 */
struct _ClutterTapActionClass
{
  /*< private >*/
  ClutterGestureActionClass parent_class;

  /*< public >*/
  gboolean (* tap)               (ClutterTapAction    *action,
                                  ClutterActor        *actor);
};

CLUTTER_EXPORT
ClutterAction * clutter_tap_action_new   (void);
G_END_DECLS
