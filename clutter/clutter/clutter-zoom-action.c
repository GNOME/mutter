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

/**
 * ClutterZoomAction:
 * 
 * Action enabling zooming on actors
 *
 * #ClutterZoomAction is a sub-class of [class@GestureAction] that
 * implements all the necessary logic for zooming actors using a "pinch"
 * gesture between two touch points.
 *
 * The simplest usage of [class@ZoomAction] consists in adding it to
 * a [class@Actor] and setting it as reactive; for instance, the following
 * code:
 *
 * ```c
 *   clutter_actor_add_action (actor, clutter_zoom_action_new ());
 *   clutter_actor_set_reactive (actor, TRUE);
 * ```
 *
 * will automatically result in the actor to be scale according to the
 * distance between two touch points.
 */

#include "config.h"

#include <math.h>

#include "clutter/clutter-zoom-action.h"

#include "clutter/clutter-debug.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-marshal.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-stage-private.h"

typedef struct
{
  gfloat start_x;
  gfloat start_y;
  gfloat transformed_start_x;
  gfloat transformed_start_y;

  gfloat update_x;
  gfloat update_y;
  gfloat transformed_update_x;
  gfloat transformed_update_y;
} ZoomPoint;

typedef struct _ClutterZoomActionPrivate
{
  ClutterStage *stage;

  ZoomPoint points[2];

  graphene_point_t initial_focal_point;
  graphene_point_t focal_point;
  graphene_point_t transformed_focal_point;

  gfloat initial_x;
  gfloat initial_y;
  gfloat initial_z;

  gdouble initial_scale_x;
  gdouble initial_scale_y;

  gdouble zoom_initial_distance;
} ClutterZoomActionPrivate;

enum
{
  ZOOM,

  LAST_SIGNAL
};

static guint zoom_signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_PRIVATE (ClutterZoomAction, clutter_zoom_action, CLUTTER_TYPE_GESTURE_ACTION)

static void
capture_point_initial_position (ClutterGestureAction *action,
                                ClutterActor         *actor,
                                gint                  index,
                                ZoomPoint            *point)
{
  clutter_gesture_action_get_motion_coords (action, index,
                                            &point->start_x,
                                            &point->start_y);

  point->transformed_start_x = point->update_x = point->start_x;
  point->transformed_start_y = point->update_x = point->start_y;
  clutter_actor_transform_stage_point (actor,
                                       point->start_x, point->start_y,
                                       &point->transformed_start_x,
                                       &point->transformed_start_y);
  point->transformed_update_x = point->transformed_start_x;
  point->transformed_update_y = point->transformed_start_y;
}

static void
capture_point_update_position (ClutterGestureAction *action,
                               ClutterActor         *actor,
                               gint                  index,
                               ZoomPoint            *point)
{
  clutter_gesture_action_get_motion_coords (action, index,
                                            &point->update_x,
                                            &point->update_y);

  point->transformed_update_x = point->update_x;
  point->transformed_update_y = point->update_y;
  clutter_actor_transform_stage_point (actor,
                                       point->update_x, point->update_y,
                                       &point->transformed_update_x,
                                       &point->transformed_update_y);
}

static gboolean
clutter_zoom_action_gesture_begin (ClutterGestureAction *action,
                                   ClutterActor         *actor)
{
  ClutterZoomActionPrivate *priv =
    clutter_zoom_action_get_instance_private (CLUTTER_ZOOM_ACTION (action));
  gfloat dx, dy;

  capture_point_initial_position (action, actor, 0, &priv->points[0]);
  capture_point_initial_position (action, actor, 1, &priv->points[1]);

  dx = priv->points[1].transformed_start_x - priv->points[0].transformed_start_x;
  dy = priv->points[1].transformed_start_y - priv->points[0].transformed_start_y;
  priv->zoom_initial_distance = sqrt (dx * dx + dy * dy);

  clutter_actor_get_translation (actor,
                                 &priv->initial_x,
                                 &priv->initial_y,
                                 &priv->initial_z);
  clutter_actor_get_scale (actor,
                           &priv->initial_scale_x,
                           &priv->initial_scale_y);

  priv->initial_focal_point.x = (priv->points[0].start_x + priv->points[1].start_x) / 2;
  priv->initial_focal_point.y = (priv->points[0].start_y + priv->points[1].start_y) / 2;
  clutter_actor_transform_stage_point (actor,
                                       priv->initial_focal_point.x,
                                       priv->initial_focal_point.y,
                                       &priv->transformed_focal_point.x,
                                       &priv->transformed_focal_point.y);

  clutter_actor_set_pivot_point (actor,
                                 priv->transformed_focal_point.x / clutter_actor_get_width (actor),
                                 priv->transformed_focal_point.y / clutter_actor_get_height (actor));

  return TRUE;
}

static gboolean
clutter_zoom_action_gesture_progress (ClutterGestureAction *action,
                                      ClutterActor         *actor)
{
  ClutterZoomActionPrivate *priv =
    clutter_zoom_action_get_instance_private (CLUTTER_ZOOM_ACTION (action));
  gdouble distance, new_scale;
  gfloat dx, dy;
  gboolean retval;

  capture_point_update_position (action, actor, 0, &priv->points[0]);
  capture_point_update_position (action, actor, 1, &priv->points[1]);

  dx = priv->points[1].update_x - priv->points[0].update_x;
  dy = priv->points[1].update_y - priv->points[0].update_y;
  distance = sqrt (dx * dx + dy * dy);

  if (distance == 0)
    return TRUE;

  priv->focal_point.x = (priv->points[0].update_x + priv->points[1].update_x) / 2;
  priv->focal_point.y = (priv->points[0].update_y + priv->points[1].update_y) / 2;

  new_scale = distance / priv->zoom_initial_distance;

  g_signal_emit (action, zoom_signals[ZOOM], 0,
                 actor, &priv->focal_point, new_scale,
                 &retval);

  return TRUE;
}

static void
clutter_zoom_action_gesture_cancel (ClutterGestureAction *action,
                                    ClutterActor         *actor)
{
  ClutterZoomActionPrivate *priv =
    clutter_zoom_action_get_instance_private (CLUTTER_ZOOM_ACTION (action));

  clutter_actor_set_translation (actor,
                                 priv->initial_x,
                                 priv->initial_y,
                                 priv->initial_z);
  clutter_actor_set_scale (actor, priv->initial_scale_x, priv->initial_scale_y);
}

static void
clutter_zoom_action_constructed (GObject *gobject)
{
  ClutterGestureAction *gesture;

  gesture = CLUTTER_GESTURE_ACTION (gobject);
  clutter_gesture_action_set_threshold_trigger_edge (gesture, CLUTTER_GESTURE_TRIGGER_EDGE_NONE);
}

static void
clutter_zoom_action_class_init (ClutterZoomActionClass *klass)
{
  ClutterGestureActionClass *gesture_class =
    CLUTTER_GESTURE_ACTION_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = clutter_zoom_action_constructed;

  gesture_class->gesture_begin = clutter_zoom_action_gesture_begin;
  gesture_class->gesture_progress = clutter_zoom_action_gesture_progress;
  gesture_class->gesture_cancel = clutter_zoom_action_gesture_cancel;

  /**
   * ClutterZoomAction::zoom:
   * @action: the #ClutterZoomAction that emitted the signal
   * @actor: the #ClutterActor attached to the action
   * @focal_point: the focal point of the zoom
   * @factor: the initial distance between the 2 touch points
   *
   * The signal is emitted for each series of touch events that
   * change the distance and focal point between the touch points.
   *
   * The default handler of the signal will call
   * [method@Actor.set_scale] on @actor using the ratio of the first
   * distance between the touch points and the current distance. To
   * override the default behaviour, connect to this signal and return
   * %FALSE.
   *
   * Return value: %TRUE if the zoom should continue, and %FALSE if
   *   the zoom should be cancelled.
   */
  zoom_signals[ZOOM] =
    g_signal_new (I_("zoom"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, g_signal_accumulator_true_handled, NULL,
                  _clutter_marshal_BOOLEAN__OBJECT_BOXED_DOUBLE,
                  G_TYPE_BOOLEAN, 3,
                  CLUTTER_TYPE_ACTOR,
                  GRAPHENE_TYPE_POINT,
                  G_TYPE_DOUBLE);
}

static void
clutter_zoom_action_init (ClutterZoomAction *self)
{
  clutter_gesture_action_set_n_touch_points (CLUTTER_GESTURE_ACTION (self),
                                             2);
}

/**
 * clutter_zoom_action_new:
 *
 * Creates a new #ClutterZoomAction instance
 *
 * Return value: the newly created #ClutterZoomAction
 */
ClutterAction *
clutter_zoom_action_new (void)
{
  return g_object_new (CLUTTER_TYPE_ZOOM_ACTION, NULL);
}

/**
 * clutter_zoom_action_get_focal_point:
 * @action: a #ClutterZoomAction
 * @point: (out): a #graphene_point_t
 *
 * Retrieves the focal point of the current zoom
 */
void
clutter_zoom_action_get_focal_point (ClutterZoomAction *action,
                                     graphene_point_t  *point)
{
  ClutterZoomActionPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ZOOM_ACTION (action));
  g_return_if_fail (point != NULL);

  priv = clutter_zoom_action_get_instance_private (action);

  *point = priv->focal_point;
}

/**
 * clutter_zoom_action_get_transformed_focal_point:
 * @action: a #ClutterZoomAction
 * @point: (out): a #graphene_point_t
 *
 * Retrieves the focal point relative to the actor's coordinates of
 * the current zoom
 */
void
clutter_zoom_action_get_transformed_focal_point (ClutterZoomAction *action,
                                                 graphene_point_t  *point)
{
  ClutterZoomActionPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ZOOM_ACTION (action));
  g_return_if_fail (point != NULL);

  priv = clutter_zoom_action_get_instance_private (action);

  *point = priv->transformed_focal_point;
}
