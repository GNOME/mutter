/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
 * Copyright (C) 2011  Robert Bosch Car Multimedia GmbH.
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
 *   Tomeu Vizoso <tomeu.vizoso@collabora.co.uk>
 *
 * Based on ClutterDragAction, written by:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-gesture-action.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_SWIPE_ACTION               (clutter_swipe_action_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterSwipeAction,
                          clutter_swipe_action,
                          CLUTTER,
                          SWIPE_ACTION,
                          ClutterGestureAction)

/**
 * ClutterSwipeActionClass:
 * @swipe: class handler for the #ClutterSwipeAction::swipe signal
 *
 * The #ClutterSwipeActionClass structure contains
 * only private data.
 */
struct _ClutterSwipeActionClass
{
  /*< private >*/
  ClutterGestureActionClass parent_class;

  /*< public >*/
  void (* swipe)  (ClutterSwipeAction    *action,
                   ClutterActor          *actor,
                   ClutterSwipeDirection  direction);
};

CLUTTER_EXPORT
ClutterAction * clutter_swipe_action_new        (void);

G_END_DECLS
