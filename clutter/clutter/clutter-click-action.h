/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
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
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 *
 * Inspired by the StClickable class in GNOME Shell, written by:
 *   Colin Walters <walters@verbum.org>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-action.h"
#include "clutter/clutter-event.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_CLICK_ACTION (clutter_click_action_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterClickAction, clutter_click_action,
                          CLUTTER, CLICK_ACTION, ClutterAction);

typedef struct _ClutterClickActionPrivate ClutterClickActionPrivate;

/**
 * ClutterClickActionClass:
 * @clicked: class handler for the #ClutterClickAction::clicked signal
 * @long_press: class handler for the #ClutterClickAction::long-press signal
 *
 * The #ClutterClickActionClass structure
 * contains only private data
 */
struct _ClutterClickActionClass
{
  /*< private >*/
  ClutterActionClass parent_class;

  /*< public >*/
  void     (* clicked)    (ClutterClickAction    *action,
                           ClutterActor          *actor);

  gboolean (* long_press) (ClutterClickAction    *action,
                           ClutterActor          *actor,
                           ClutterLongPressState  state);
};

CLUTTER_EXPORT
ClutterAction *        clutter_click_action_new        (void);

CLUTTER_EXPORT
guint                  clutter_click_action_get_button (ClutterClickAction *action);
CLUTTER_EXPORT
ClutterModifierType    clutter_click_action_get_state  (ClutterClickAction *action);
CLUTTER_EXPORT
void                   clutter_click_action_get_coords (ClutterClickAction *action,
                                                        gfloat             *press_x,
                                                        gfloat             *press_y);

CLUTTER_EXPORT
void                   clutter_click_action_release    (ClutterClickAction *action);

G_END_DECLS
