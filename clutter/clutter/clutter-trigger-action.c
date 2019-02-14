/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Jonas Dreßler <verdre@v0yd.nl>
 */

/**
 * SECTION:clutter-trigger-action
 * @Title: ClutterTriggerAction
 * @Short_Description: Action to trigger gestures after a threshold is reached
 *
 * #ClutterTriggerAction is a sub-class of #ClutterGestureAction that implements
 * the logic for recognizing swipe gestures.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "clutter-build-config.h"

#include "clutter-trigger-action.h"

#include "clutter-debug.h"
#include "clutter-enum-types.h"
#include "clutter-marshal.h"
#include "clutter-private.h"

#include <math.h>

#define FLOAT_EPSILON   (1e-15)

struct _ClutterTriggerActionPrivate
{
  ClutterTriggerEdge trigger_edge;

  float distance_x;
  float distance_y;
};

enum
{
  PROP_0,

  PROP_TRIGGER_EDGE,
  PROP_THRESHOLD_DISTANCE_X,
  PROP_THRESHOLD_DISTANCE_Y,

  PROP_LAST
};

static GParamSpec *trigger_props[PROP_LAST];

G_DEFINE_TYPE_WITH_PRIVATE (ClutterTriggerAction, clutter_trigger_action, CLUTTER_TYPE_GESTURE_ACTION)

static void
get_threshold_distance (ClutterTriggerAction *action,
                        float                *distance_x,
                        float                *distance_y)
{
  gint default_threshold;
  ClutterSettings *settings = clutter_settings_get_default ();

  g_object_get (settings, "dnd-drag-threshold", &default_threshold, NULL);

  if (distance_x != NULL)
    {
			if (action->priv->distance_x > 0.0)
				*distance_x = action->priv->distance_x;
			else
				*distance_x = default_threshold;
    }

  if (distance_y != NULL)
    {
			if (action->priv->distance_y > 0.0)
				*distance_y = action->priv->distance_y;
			else
				*distance_y = default_threshold;
    }
}

static gboolean
point_inside_threshold (ClutterTriggerAction *action,
                        float                 old_x,
                        float                 old_y,
                        float                 new_x,
                        float                 new_y)
{
  float threshold_x, threshold_y;

  get_threshold_distance (action, &threshold_x, &threshold_y);

  if ((fabsf (old_x - new_x) < threshold_x) &&
      (fabsf (old_y - new_y) < threshold_y))
    return TRUE;

  return FALSE;
}

static gboolean
gesture_prepare (ClutterGestureAction  *action,
                 ClutterActor          *actor,
                 gint                   point)
{
  ClutterTriggerActionPrivate *priv = CLUTTER_TRIGGER_ACTION (action)->priv;

  if (priv->trigger_edge != CLUTTER_TRIGGER_EDGE_AFTER)
    return TRUE;

  float press_x, press_y;
  float motion_x, motion_y;
  uint current_n_points;
  gboolean triggered = FALSE;
  uint i;

  current_n_points = clutter_gesture_action_get_n_current_points (action);

  for (i = 0; i < current_n_points; i++)
    {
      clutter_gesture_action_get_press_coords (action, i, &press_x, &press_y);
      clutter_gesture_action_get_motion_coords (action, i, &motion_x, &motion_y);

      if (!point_inside_threshold (CLUTTER_TRIGGER_ACTION (action),
                                   press_x, press_y,
                                   motion_x, motion_y))
        {
          triggered = TRUE;
          break;
        }
    }

  // TODO: Stop the signal emission here?

  return triggered;
}

static void
gesture_progress (ClutterGestureAction *action,
                  ClutterActor         *actor,
                  gint                  point)
{
  ClutterTriggerActionPrivate *priv = CLUTTER_TRIGGER_ACTION (action)->priv;

  if (priv->trigger_edge != CLUTTER_TRIGGER_EDGE_BEFORE)
    return;

  float press_x, press_y;
  float motion_x, motion_y;
  gboolean triggered = FALSE;

  clutter_gesture_action_get_press_coords (action, point, &press_x, &press_y);
  clutter_gesture_action_get_motion_coords (action, point, &motion_x, &motion_y);

  if (!point_inside_threshold (CLUTTER_TRIGGER_ACTION (action),
                               press_x, press_y,
                               motion_x, motion_y))
    {
      triggered = TRUE;
    }

  // TODO: Stop the signal emission here?

  if (triggered)
    clutter_gesture_action_cancel (action);
}

static void
clutter_trigger_action_set_property (GObject      *gobject,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  ClutterTriggerAction *self = CLUTTER_TRIGGER_ACTION (gobject);

  switch (prop_id)
    {
    case PROP_TRIGGER_EDGE:
      clutter_trigger_action_set_trigger_edge (self, g_value_get_enum (value));
      break;

    case PROP_THRESHOLD_DISTANCE_X:
      clutter_trigger_action_set_threshold_distance (self, g_value_get_float (value), self->priv->distance_y);
      break;

    case PROP_THRESHOLD_DISTANCE_Y:
      clutter_trigger_action_set_threshold_distance (self, self->priv->distance_x, g_value_get_float (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_trigger_action_get_property (GObject    *gobject,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  ClutterTriggerAction *self = CLUTTER_TRIGGER_ACTION (gobject);
  float distance_x, distance_y;

  switch (prop_id)
    {
    case PROP_TRIGGER_EDGE:
      g_value_set_enum (value, self->priv->trigger_edge);
      break;

    case PROP_THRESHOLD_DISTANCE_X:
      get_threshold_distance (self, &distance_x, NULL);
      g_value_set_float (value, distance_x);
      break;

    case PROP_THRESHOLD_DISTANCE_Y:
      get_threshold_distance (self, NULL, &distance_y);
      g_value_set_float (value, distance_x);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_trigger_action_class_init (ClutterTriggerActionClass *klass)
{
  ClutterGestureActionClass *gesture_class =
      CLUTTER_GESTURE_ACTION_CLASS (klass);
  GObjectClass *gobject_class =
      G_OBJECT_CLASS (klass);

  gobject_class->set_property = clutter_trigger_action_set_property;
  gobject_class->get_property = clutter_trigger_action_get_property;

  gesture_class->gesture_prepare = gesture_prepare;
  gesture_class->gesture_progress = gesture_progress;

  /**
   * ClutterTriggerAction:trigger-edge:
   *
   * The trigger edge to be used by the action to either emit the
   * #ClutterGestureAction::gesture-begin signal or to emit the
   * #ClutterGestureAction::gesture-cancel signal.
   */
  trigger_props[PROP_TRIGGER_EDGE] =
    g_param_spec_enum ("trigger-edge",
                       P_("Trigger Edge"),
                       P_("The trigger edge used by the action"),
                       CLUTTER_TYPE_TRIGGER_EDGE,
                       CLUTTER_TRIGGER_EDGE_NONE,
                       CLUTTER_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterTriggerAction:threshold-distance-x:
   *
   * The horizontal trigger distance to be used by the action to either
   * emit the #ClutterTriggerAction::gesture-begin signal or to emit
   * the #ClutterTriggerAction::gesture-cancel signal.
   *
   * A negative value will be interpreted as the default drag threshold.
   */
  trigger_props[PROP_THRESHOLD_DISTANCE_X] =
    g_param_spec_float ("threshold-distance-x",
                        P_("Threshold Trigger Horizontal Distance"),
                        P_("The horizontal trigger distance used by the action"),
                        -1.0, G_MAXFLOAT, -1.0,
                        CLUTTER_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterTriggerAction:threshold-distance-y:
   *
   * The vertical trigger distance to be used by the action to either
   * emit the #ClutterGestureAction::gesture-begin signal or to emit
   * the #ClutterGestureAction::gesture-cancel signal.
   *
   * A negative value will be interpreted as the default drag threshold.
   */
  trigger_props[PROP_THRESHOLD_DISTANCE_Y] =
    g_param_spec_float ("threshold-distance-y",
                        P_("Threshold Trigger Vertical Distance"),
                        P_("The vertical trigger distance used by the action"),
                        -1.0, G_MAXFLOAT, -1.0,
                        CLUTTER_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     trigger_props);
}

static void
clutter_trigger_action_init (ClutterTriggerAction *self)
{
  self->priv = clutter_trigger_action_get_instance_private (self);

  self->priv->trigger_edge = CLUTTER_TRIGGER_EDGE_NONE;
}

/**
 * clutter_trigger_action_new:
 *
 * Creates a new #ClutterTriggerAction instance
 *
 * Returns: the newly created #ClutterTriggerAction
 */
ClutterAction *
clutter_trigger_action_new (void)
{
  return g_object_new (CLUTTER_TYPE_TRIGGER_ACTION, NULL);
}

/**
 * clutter_trigger_action_set_trigger_edge:
 * @action: a #ClutterTriggerAction
 * @edge: the %ClutterTriggerEdge
 *
 * Sets the edge trigger for the gesture drag threshold, if any.
 *
 * This function should only be called by sub-classes of
 * #ClutterGestureAction during their construction phase.
 */
void
clutter_trigger_action_set_trigger_edge (ClutterTriggerAction *action,
                                         ClutterTriggerEdge    edge)
{
  g_return_if_fail (CLUTTER_IS_TRIGGER_ACTION (action));

  if (action->priv->trigger_edge == edge)
    return;

  action->priv->trigger_edge = edge;

  g_object_notify_by_pspec (G_OBJECT (action), trigger_props[PROP_TRIGGER_EDGE]);
}

/**
 * clutter_trigger_action_get_trigger_edge:
 * @action: a #ClutterTriggerAction
 *
 * Retrieves the edge trigger of the trigger @action, as set using
 * clutter_trigger_action_set_trigger_edge().
 *
 * Returns: the edge trigger
 */
ClutterTriggerEdge
clutter_trigger_action_get_trigger_edge (ClutterTriggerAction *action)
{
  g_return_val_if_fail (CLUTTER_IS_TRIGGER_ACTION (action),
                        CLUTTER_TRIGGER_EDGE_NONE);

  return action->priv->trigger_edge;
}

/**
 * clutter_trigger_action_set_threshold_distance:
 * @action: a #ClutterTriggerAction
 * @x: the distance on the horizontal axis
 * @y: the distance on the vertical axis
 *
 * Sets the threshold trigger distance for the gesture drag threshold, if any.
 *
 * This function should only be called by sub-classes of
 * #ClutterTriggerAction during their construction phase.
 */
void
clutter_trigger_action_set_threshold_distance (ClutterTriggerAction      *action,
                                               float                      x,
                                               float                      y)
{
  g_return_if_fail (CLUTTER_IS_TRIGGER_ACTION (action));

  if (fabsf (x - action->priv->distance_x) > FLOAT_EPSILON)
    {
      action->priv->distance_x = x;
      g_object_notify_by_pspec (G_OBJECT (action), trigger_props[PROP_THRESHOLD_DISTANCE_X]);
    }

  if (fabsf (y - action->priv->distance_y) > FLOAT_EPSILON)
    {
      action->priv->distance_y = y;
      g_object_notify_by_pspec (G_OBJECT (action), trigger_props[PROP_THRESHOLD_DISTANCE_Y]);
    }
}

/**
 * clutter_trigger_action_get_threshold_distance:
 * @action: a #ClutterTriggerAction
 * @x: (out) (allow-none): The return location for the horizontal distance, or %NULL
 * @y: (out) (allow-none): The return location for the vertical distance, or %NULL
 *
 * Retrieves the threshold trigger distance of the gesture @action,
 * as set using clutter_trigger_action_set_threshold_distance().
 */
void
clutter_trigger_action_get_threshold_distance (ClutterTriggerAction *action,
                                               float                *x,
                                               float                *y)
{
  g_return_if_fail (CLUTTER_IS_TRIGGER_ACTION (action));

  get_threshold_distance (action, x, y);
}
