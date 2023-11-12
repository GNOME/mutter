/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012  Intel Corporation.
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
 *   Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-event.h"
#include "clutter/clutter-gesture-action.h"
#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_ZOOM_ACTION                (clutter_zoom_action_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterZoomAction,
                          clutter_zoom_action,
                          CLUTTER,
                          ZOOM_ACTION,
                          ClutterGestureAction)

/**
 * ClutterZoomActionClass:
 * @zoom: class handler of the #ClutterZoomAction::zoom signal
 *
 * The #ClutterZoomActionClass structure contains
 * only private data
 */
struct _ClutterZoomActionClass
{
  /*< private >*/
  ClutterGestureActionClass parent_class;
};

CLUTTER_EXPORT
ClutterAction * clutter_zoom_action_new                         (void);

CLUTTER_EXPORT
void            clutter_zoom_action_get_focal_point             (ClutterZoomAction *action,
                                                                 graphene_point_t  *point);
CLUTTER_EXPORT
void            clutter_zoom_action_get_transformed_focal_point (ClutterZoomAction *action,
                                                                 graphene_point_t  *point);

G_END_DECLS
