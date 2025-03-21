/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
 * Copyright (C) 2011  Robert Bosch Car Multimedia GmbH.
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
 *   Tomeu Vizoso <tomeu.vizoso@collabora.co.uk>
 */

/**
 * ClutterGestureAction:
 *
 * Action for gesture gestures
 *
 * #ClutterGestureAction is a sub-class of [class@Action] that implements
 * the logic for recognizing gesture gestures. It listens for low level
 * [struct@Event] events on the stage to raise
 * the [signal@GestureAction::gesture-begin], [signal@GestureAction::gesture-progress],
 * and [signal@GestureAction::gesture-end] signals.
 *
 * To use #ClutterGestureAction you just need to apply it to a [class@Actor]
 * using [method@Actor.add_action] and connect to the signals:
 *
 * ```c
 *   ClutterAction *action = clutter_gesture_action_new ();
 *
 *   clutter_actor_add_action (actor, action);
 *
 *   g_signal_connect (action, "gesture-begin", G_CALLBACK (on_gesture_begin), NULL);
 *   g_signal_connect (action, "gesture-progress", G_CALLBACK (on_gesture_progress), NULL);
 *   g_signal_connect (action, "gesture-end", G_CALLBACK (on_gesture_end), NULL);
 * ```
 *
 * ## Creating Gesture actions
 *
 * A #ClutterGestureAction provides four separate states that can be
 * used to recognize or ignore gestures when writing a new action class:
 *
 *  - Prepare -> Cancel
 *  - Prepare -> Begin -> Cancel
 *  - Prepare -> Begin -> End
 *  - Prepare -> Begin -> Progress -> Cancel
 *  - Prepare -> Begin -> Progress -> End
 *
 * Each #ClutterGestureAction starts in the "prepare" state, and calls
 * the [vfunc@GestureAction.gesture_prepare] virtual function; this
 * state can be used to reset the internal state of a #ClutterGestureAction
 * subclass, but it can also immediately cancel a gesture without going
 * through the rest of the states.
 *
 * The "begin" state follows the "prepare" state, and calls the
 * [vfunc@GestureAction.gesture_begin] virtual function. This state
 * signals the start of a gesture recognizing process. From the "begin" state
 * the gesture recognition process can successfully end, by going to the
 * "end" state; it can continue in the "progress" state, in case of a
 * continuous gesture; or it can be terminated, by moving to the "cancel"
 * state.
 *
 * In case of continuous gestures, the #ClutterGestureAction will use
 * the "progress" state, calling the [vfunc@GestureAction.gesture_progress]
 * virtual function; the "progress" state will continue until the end of the
 * gesture, in which case the "end" state will be reached, or until the
 * gesture is cancelled, in which case the "cancel" gesture will be used
 * instead.
 */

#include "config.h"


#include "clutter/clutter-debug.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-gesture-action.h"
#include "clutter/clutter-marshal.h"
#include "clutter/clutter-private.h"

#include <math.h>

#define MAX_GESTURE_POINTS (10)
#define FLOAT_EPSILON   (1e-15)

typedef struct
{
  ClutterInputDevice *device;
  ClutterEventSequence *sequence;
  ClutterEvent *last_event;

  gfloat press_x, press_y;
  gint64 last_motion_time;
  gfloat last_motion_x, last_motion_y;
  gint64 last_delta_time;
  gfloat last_delta_x, last_delta_y;
  gfloat release_x, release_y;
} GesturePoint;

struct _ClutterGestureActionPrivate
{
  ClutterActor *stage;

  gint requested_nb_points;
  GArray *points;

  ClutterGestureTriggerEdge edge;
  float distance_x, distance_y;

  guint in_gesture : 1;
};

enum
{
  PROP_0,

  PROP_N_TOUCH_POINTS,
  PROP_THRESHOLD_TRIGGER_EDGE,
  PROP_THRESHOLD_TRIGGER_DISTANCE_X,
  PROP_THRESHOLD_TRIGGER_DISTANCE_Y,

  PROP_LAST
};

enum
{
  GESTURE_BEGIN,
  GESTURE_PROGRESS,
  GESTURE_END,
  GESTURE_CANCEL,

  LAST_SIGNAL
};

static GParamSpec *gesture_props[PROP_LAST];
static guint gesture_signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_PRIVATE (ClutterGestureAction, clutter_gesture_action, CLUTTER_TYPE_ACTION)

static GesturePoint *
gesture_register_point (ClutterGestureAction *action,
                        const ClutterEvent   *event)
{
  ClutterGestureActionPrivate *priv =
    clutter_gesture_action_get_instance_private (action);
  GesturePoint *point = NULL;

  if (priv->points->len >= MAX_GESTURE_POINTS)
    return NULL;

  g_array_set_size (priv->points, priv->points->len + 1);
  point = &g_array_index (priv->points, GesturePoint, priv->points->len - 1);

  point->last_event = clutter_event_copy (event);
  point->device = clutter_event_get_device (event);

  clutter_event_get_coords (event, &point->press_x, &point->press_y);
  point->last_motion_x = point->press_x;
  point->last_motion_y = point->press_y;
  point->last_motion_time = clutter_event_get_time (event);

  point->last_delta_x = point->last_delta_y = 0;
  point->last_delta_time = 0;

  if (clutter_event_type (event) != CLUTTER_BUTTON_PRESS)
    point->sequence = clutter_event_get_event_sequence (event);
  else
    point->sequence = NULL;

  return point;
}

static GesturePoint *
gesture_find_point (ClutterGestureAction *action,
                    const ClutterEvent   *event,
                    int                  *position)
{
  ClutterGestureActionPrivate *priv =
    clutter_gesture_action_get_instance_private (action);
  GesturePoint *point = NULL;
  ClutterEventType type = clutter_event_type (event);
  ClutterInputDevice *device = clutter_event_get_device (event);
  ClutterEventSequence *sequence = NULL;
  gint i;

  if ((type != CLUTTER_BUTTON_PRESS) &&
      (type != CLUTTER_BUTTON_RELEASE) &&
      (type != CLUTTER_MOTION))
    sequence = clutter_event_get_event_sequence (event);

  for (i = 0; i < priv->points->len; i++)
    {
      if ((g_array_index (priv->points, GesturePoint, i).device == device) &&
          (g_array_index (priv->points, GesturePoint, i).sequence == sequence))
        {
          if (position != NULL)
            *position = i;
          point = &g_array_index (priv->points, GesturePoint, i);
          break;
        }
    }

  return point;
}

static void
gesture_unregister_point (ClutterGestureAction *action, gint position)
{
  ClutterGestureActionPrivate *priv =
    clutter_gesture_action_get_instance_private (action);

  if (priv->points->len == 0)
    return;

  g_array_remove_index (priv->points, position);
}

static void
gesture_update_motion_point (GesturePoint       *point,
                             const ClutterEvent *event)
{
  gfloat motion_x, motion_y;
  gint64 _time;

  clutter_event_get_coords (event, &motion_x, &motion_y);

  clutter_event_free (point->last_event);
  point->last_event = clutter_event_copy (event);

  point->last_delta_x = motion_x - point->last_motion_x;
  point->last_delta_y = motion_y - point->last_motion_y;
  point->last_motion_x = motion_x;
  point->last_motion_y = motion_y;

  _time = clutter_event_get_time (event);
  point->last_delta_time = _time - point->last_motion_time;
  point->last_motion_time = _time;
}

static void
gesture_update_release_point (GesturePoint       *point,
                              const ClutterEvent *event)
{
  gint64 _time;

  clutter_event_get_coords (event, &point->release_x, &point->release_y);

  clutter_event_free (point->last_event);
  point->last_event = clutter_event_copy (event);

  /* Treat the release event as the continuation of the last motion,
   * in case the user keeps the pointer still for a while before
   * releasing it. */
   _time = clutter_event_get_time (event);
   point->last_delta_time += _time - point->last_motion_time;
}

static gint
gesture_get_default_threshold (void)
{
  gint threshold;
  ClutterContext *context = _clutter_context_get_default ();
  ClutterSettings *settings = clutter_context_get_settings (context);
  g_object_get (settings, "dnd-drag-threshold", &threshold, NULL);
  return threshold;
}

static gboolean
gesture_point_pass_threshold (ClutterGestureAction *action,
                              GesturePoint         *point,
                              const ClutterEvent   *event)
{
  float threshold_x, threshold_y;
  gfloat motion_x, motion_y;

  clutter_event_get_coords (event, &motion_x, &motion_y);
  clutter_gesture_action_get_threshold_trigger_distance (action, &threshold_x, &threshold_y);

  if ((fabsf (point->press_y - motion_y) < threshold_y) &&
      (fabsf (point->press_x - motion_x) < threshold_x))
    return TRUE;
  return FALSE;
}

static void
gesture_point_unset (GesturePoint *point)
{
  clutter_event_free (point->last_event);
}

static void
cancel_gesture (ClutterGestureAction *action)
{
  ClutterGestureActionPrivate *priv =
    clutter_gesture_action_get_instance_private (action);
  ClutterActor *actor;

  priv->in_gesture = FALSE;

  actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));
  g_signal_emit (action, gesture_signals[GESTURE_CANCEL], 0, actor);

  g_array_set_size (priv->points, 0);
}

static gboolean
begin_gesture (ClutterGestureAction *action,
               ClutterActor         *actor)
{
  ClutterGestureActionPrivate *priv =
    clutter_gesture_action_get_instance_private (action);
  gboolean return_value;

  priv->in_gesture = TRUE;

  if (!CLUTTER_GESTURE_ACTION_GET_CLASS (action)->gesture_prepare (action, actor))
    {
      cancel_gesture (action);
      return FALSE;
    }

  /* clutter_gesture_action_cancel() may have been called during
   * gesture_prepare(), check that the gesture is still active. */
  if (!priv->in_gesture)
    return FALSE;

  g_signal_emit (action, gesture_signals[GESTURE_BEGIN], 0, actor,
                 &return_value);

  if (!return_value)
    {
      cancel_gesture (action);
      return FALSE;
    }

  return TRUE;
}

static gboolean
clutter_gesture_action_handle_event (ClutterAction      *action,
                                     const ClutterEvent *event)
{
  ClutterGestureAction *gesture_action = CLUTTER_GESTURE_ACTION (action);
  ClutterGestureActionPrivate *priv =
    clutter_gesture_action_get_instance_private (gesture_action);
  ClutterActor *actor =
    clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));
  gint position;
  float threshold_x, threshold_y;
  gboolean return_value;
  GesturePoint *point;
  ClutterEventType event_type;

  if (!clutter_actor_meta_get_enabled (CLUTTER_ACTOR_META (action)))
    return CLUTTER_EVENT_PROPAGATE;

  event_type = clutter_event_type (event);

  if (event_type == CLUTTER_BUTTON_PRESS ||
      event_type == CLUTTER_TOUCH_BEGIN)
    {
      point = gesture_register_point (gesture_action, event);
    }
  else
    {
      if ((point = gesture_find_point (gesture_action, event, &position)) == NULL)
        return CLUTTER_EVENT_PROPAGATE;
    }

  switch (event_type)
    {
    case CLUTTER_ENTER:
    case CLUTTER_LEAVE:
      return CLUTTER_EVENT_PROPAGATE;

    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_TOUCH_BEGIN:
      if (priv->stage == NULL)
        priv->stage = clutter_actor_get_stage (actor);

      /* Start the gesture immediately if the gesture has no
       * _TRIGGER_EDGE_AFTER drag threshold. */
      if ((priv->points->len >= priv->requested_nb_points) &&
          (priv->edge != CLUTTER_GESTURE_TRIGGER_EDGE_AFTER))
        begin_gesture (gesture_action, actor);

      break;
    case CLUTTER_MOTION:
      {
        ClutterModifierType mods = clutter_event_get_state (event);

        /* we might miss a button-release event in case of grabs,
         * so we need to check whether the button is still down
         * during a motion event
         */
        if (!(mods & CLUTTER_BUTTON1_MASK))
          {
            cancel_gesture (gesture_action);
            return CLUTTER_EVENT_PROPAGATE;
          }
      }
      G_GNUC_FALLTHROUGH;

    case CLUTTER_TOUCH_UPDATE:
      if (!priv->in_gesture)
        {
          if (priv->points->len < priv->requested_nb_points)
            {
              gesture_update_motion_point (point, event);
              return CLUTTER_EVENT_PROPAGATE;
            }

          /* Wait until the drag threshold has been exceeded
           * before starting _TRIGGER_EDGE_AFTER gestures. */
          if (priv->edge == CLUTTER_GESTURE_TRIGGER_EDGE_AFTER &&
              gesture_point_pass_threshold (gesture_action, point, event))
            {
              gesture_update_motion_point (point, event);
              return CLUTTER_EVENT_PROPAGATE;
            }

          gesture_update_motion_point (point, event);

          if (!begin_gesture (gesture_action, actor))
            return CLUTTER_EVENT_PROPAGATE;

          if ((point = gesture_find_point (gesture_action, event, &position)) == NULL)
            return CLUTTER_EVENT_PROPAGATE;
        }

      gesture_update_motion_point (point, event);

      g_signal_emit (gesture_action, gesture_signals[GESTURE_PROGRESS], 0, actor,
                     &return_value);
      if (!return_value)
        {
          cancel_gesture (gesture_action);
          return CLUTTER_EVENT_PROPAGATE;
        }

      /* Check if a _TRIGGER_EDGE_BEFORE gesture needs to be cancelled because
       * the drag threshold has been exceeded. */
      clutter_gesture_action_get_threshold_trigger_distance (gesture_action,
                                                             &threshold_x,
                                                             &threshold_y);
      if (priv->edge == CLUTTER_GESTURE_TRIGGER_EDGE_BEFORE &&
          ((fabsf (point->press_y - point->last_motion_y) > threshold_y) ||
           (fabsf (point->press_x - point->last_motion_x) > threshold_x)))
        {
          cancel_gesture (gesture_action);
          return CLUTTER_EVENT_PROPAGATE;
        }
      break;

    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_TOUCH_END:
      {
        gesture_update_release_point (point, event);

        if (priv->in_gesture &&
            ((priv->points->len - 1) < priv->requested_nb_points))
          {
            priv->in_gesture = FALSE;
            g_signal_emit (gesture_action, gesture_signals[GESTURE_END], 0, actor);
          }

        gesture_unregister_point (gesture_action, position);
      }
      break;

    case CLUTTER_TOUCH_CANCEL:
      {
        gesture_update_release_point (point, event);

        if (priv->in_gesture)
          {
            priv->in_gesture = FALSE;
            cancel_gesture (gesture_action);
          }

        gesture_unregister_point (gesture_action, position);
      }
      break;

    default:
      break;
    }

  return priv->in_gesture ?
    CLUTTER_EVENT_STOP :
    CLUTTER_EVENT_PROPAGATE;
}

static void
clutter_gesture_action_sequence_cancelled (ClutterAction        *action,
                                           ClutterInputDevice   *device,
                                           ClutterEventSequence *sequence)
{
  ClutterGestureAction *self = CLUTTER_GESTURE_ACTION (action);
  ClutterGestureActionPrivate *priv =
    clutter_gesture_action_get_instance_private (self);
  int i, position = -1;

  for (i = 0; i < priv->points->len; i++)
    {
      if ((g_array_index (priv->points, GesturePoint, i).device == device) &&
          (g_array_index (priv->points, GesturePoint, i).sequence == sequence))
        {
          position = i;
          break;
        }
    }

  if (position == -1)
    return;

  if (priv->in_gesture)
    {
      priv->in_gesture = FALSE;
      cancel_gesture (self);
    }

  gesture_unregister_point (self, position);
}

static void
clutter_gesture_action_set_enabled (ClutterActorMeta *meta,
                                    gboolean          is_enabled)
{
  ClutterActorMetaClass *meta_class =
    CLUTTER_ACTOR_META_CLASS (clutter_gesture_action_parent_class);
  ClutterGestureAction *gesture_action = CLUTTER_GESTURE_ACTION (meta);
  ClutterGestureActionPrivate *priv =
    clutter_gesture_action_get_instance_private (gesture_action);

  if (!is_enabled)
    {
      if (priv->in_gesture)
        cancel_gesture (gesture_action);
      else
        g_array_set_size (priv->points, 0);
    }

  meta_class->set_enabled (meta, is_enabled);
}

static gboolean
default_event_handler (ClutterGestureAction *action,
                       ClutterActor *actor)
{
  return TRUE;
}

static void
clutter_gesture_action_set_property (GObject      *gobject,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  ClutterGestureAction *self = CLUTTER_GESTURE_ACTION (gobject);
  ClutterGestureActionPrivate *priv =
    clutter_gesture_action_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_N_TOUCH_POINTS:
      clutter_gesture_action_set_n_touch_points (self, g_value_get_int (value));
      break;

    case PROP_THRESHOLD_TRIGGER_EDGE:
      clutter_gesture_action_set_threshold_trigger_edge (self, g_value_get_enum (value));
      break;

    case PROP_THRESHOLD_TRIGGER_DISTANCE_X:
      clutter_gesture_action_set_threshold_trigger_distance (self,
                                                             g_value_get_float (value),
                                                             priv->distance_y);
      break;

    case PROP_THRESHOLD_TRIGGER_DISTANCE_Y:
      clutter_gesture_action_set_threshold_trigger_distance (self,
                                                             priv->distance_x,
                                                             g_value_get_float (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_gesture_action_get_property (GObject    *gobject,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  ClutterGestureActionPrivate *priv =
    clutter_gesture_action_get_instance_private (CLUTTER_GESTURE_ACTION (gobject));

  switch (prop_id)
    {
    case PROP_N_TOUCH_POINTS:
      g_value_set_int (value, priv->requested_nb_points);
      break;

    case PROP_THRESHOLD_TRIGGER_EDGE:
      g_value_set_enum (value, priv->edge);
      break;

    case PROP_THRESHOLD_TRIGGER_DISTANCE_X:
      if (priv->distance_x > 0.0)
        g_value_set_float (value, priv->distance_x);
      else
        g_value_set_float (value, gesture_get_default_threshold ());
      break;

    case PROP_THRESHOLD_TRIGGER_DISTANCE_Y:
      if (priv->distance_y > 0.0)
        g_value_set_float (value, priv->distance_y);
      else
        g_value_set_float (value, gesture_get_default_threshold ());
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_gesture_action_finalize (GObject *gobject)
{
  ClutterGestureActionPrivate *priv =
    clutter_gesture_action_get_instance_private (CLUTTER_GESTURE_ACTION (gobject));

  g_array_unref (priv->points);

  G_OBJECT_CLASS (clutter_gesture_action_parent_class)->finalize (gobject);
}

static void
clutter_gesture_action_class_init (ClutterGestureActionClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorMetaClass *meta_class = CLUTTER_ACTOR_META_CLASS (klass);
  ClutterActionClass *action_class = CLUTTER_ACTION_CLASS (klass);

  gobject_class->finalize = clutter_gesture_action_finalize;
  gobject_class->set_property = clutter_gesture_action_set_property;
  gobject_class->get_property = clutter_gesture_action_get_property;

  meta_class->set_enabled = clutter_gesture_action_set_enabled;

  action_class->handle_event = clutter_gesture_action_handle_event;
  action_class->sequence_cancelled = clutter_gesture_action_sequence_cancelled;

  klass->gesture_begin = default_event_handler;
  klass->gesture_progress = default_event_handler;
  klass->gesture_prepare = default_event_handler;

  /**
   * ClutterGestureAction:n-touch-points:
   *
   * Number of touch points to trigger a gesture action.
   */
  gesture_props[PROP_N_TOUCH_POINTS] =
    g_param_spec_int ("n-touch-points", NULL, NULL,
                      1, G_MAXINT, 1,
                      G_PARAM_READWRITE |
                      G_PARAM_STATIC_STRINGS);

  /**
   * ClutterGestureAction:threshold-trigger-edge:
   *
   * The trigger edge to be used by the action to either emit the
   * [signal@GestureAction::gesture-begin] signal or to emit the
   * [signal@GestureAction::gesture-cancel] signal.
   */
  gesture_props[PROP_THRESHOLD_TRIGGER_EDGE] =
    g_param_spec_enum ("threshold-trigger-edge", NULL, NULL,
                       CLUTTER_TYPE_GESTURE_TRIGGER_EDGE,
                       CLUTTER_GESTURE_TRIGGER_EDGE_NONE,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterGestureAction:threshold-trigger-distance-x:
   *
   * The horizontal trigger distance to be used by the action to either
   * emit the [signal@GestureAction::gesture-begin] signal or to emit
   * the [signal@GestureAction::gesture-cancel] signal.
   *
   * A negative value will be interpreted as the default drag threshold.
   */
  gesture_props[PROP_THRESHOLD_TRIGGER_DISTANCE_X] =
    g_param_spec_float ("threshold-trigger-distance-x", NULL, NULL,
                        -1.0, G_MAXFLOAT, -1.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterGestureAction:threshold-trigger-distance-y:
   *
   * The vertical trigger distance to be used by the action to either
   * emit the [signal@GestureAction::gesture-begin] signal or to emit
   * the [signal@GestureAction::gesture-cancel] signal.
   *
   * A negative value will be interpreted as the default drag threshold.
   */
  gesture_props[PROP_THRESHOLD_TRIGGER_DISTANCE_Y] =
    g_param_spec_float ("threshold-trigger-distance-y", NULL, NULL,
                        -1.0, G_MAXFLOAT, -1.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     gesture_props);

  /**
   * ClutterGestureAction::gesture-begin:
   * @action: the #ClutterGestureAction that emitted the signal
   * @actor: the #ClutterActor attached to the @action
   *
   * The signal is emitted when the [class@Actor] to which
   * a #ClutterGestureAction has been applied starts receiving a gesture.
   *
   * Return value: %TRUE if the gesture should start, and %FALSE if
   *   the gesture should be ignored.
   */
  gesture_signals[GESTURE_BEGIN] =
    g_signal_new (I_("gesture-begin"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGestureActionClass, gesture_begin),
                  _clutter_boolean_continue_accumulator, NULL,
                  _clutter_marshal_BOOLEAN__OBJECT,
                  G_TYPE_BOOLEAN, 1,
                  CLUTTER_TYPE_ACTOR);

  /**
   * ClutterGestureAction::gesture-progress:
   * @action: the #ClutterGestureAction that emitted the signal
   * @actor: the #ClutterActor attached to the @action
   *
   * The signal is emitted for each motion event after
   * the [signal@GestureAction::gesture-begin] signal has been emitted.
   *
   * Return value: %TRUE if the gesture should continue, and %FALSE if
   *   the gesture should be cancelled.
   */
  gesture_signals[GESTURE_PROGRESS] =
    g_signal_new (I_("gesture-progress"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGestureActionClass, gesture_progress),
                  _clutter_boolean_continue_accumulator, NULL,
                  _clutter_marshal_BOOLEAN__OBJECT,
                  G_TYPE_BOOLEAN, 1,
                  CLUTTER_TYPE_ACTOR);

  /**
   * ClutterGestureAction::gesture-end:
   * @action: the #ClutterGestureAction that emitted the signal
   * @actor: the #ClutterActor attached to the @action
   *
   * The signal is emitted at the end of the gesture gesture,
   * when the pointer's button is released
   *
   * This signal is emitted if and only if the [signal@GestureAction::gesture-begin]
   * signal has been emitted first.
   */
  gesture_signals[GESTURE_END] =
    g_signal_new (I_("gesture-end"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGestureActionClass, gesture_end),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_ACTOR);

  /**
   * ClutterGestureAction::gesture-cancel:
   * @action: the #ClutterGestureAction that emitted the signal
   * @actor: the #ClutterActor attached to the @action
   *
   * The signal is emitted when the ongoing gesture gets
   * cancelled from the [signal@GestureAction::gesture-progress] signal handler.
   *
   * This signal is emitted if and only if the [signal@GestureAction::gesture-begin]
   * signal has been emitted first.
   */
  gesture_signals[GESTURE_CANCEL] =
    g_signal_new (I_("gesture-cancel"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGestureActionClass, gesture_cancel),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_ACTOR);
}

static void
clutter_gesture_action_init (ClutterGestureAction *self)
{
  ClutterGestureActionPrivate *priv =
    clutter_gesture_action_get_instance_private (self);

  priv->points = g_array_sized_new (FALSE, TRUE, sizeof (GesturePoint), 3);
  g_array_set_clear_func (priv->points, (GDestroyNotify) gesture_point_unset);

  priv->requested_nb_points = 1;
  priv->edge = CLUTTER_GESTURE_TRIGGER_EDGE_NONE;
}

/**
 * clutter_gesture_action_new:
 *
 * Creates a new #ClutterGestureAction instance.
 *
 * Return value: the newly created #ClutterGestureAction
 */
ClutterAction *
clutter_gesture_action_new (void)
{
  return g_object_new (CLUTTER_TYPE_GESTURE_ACTION, NULL);
}

/**
 * clutter_gesture_action_get_press_coords:
 * @action: a #ClutterGestureAction
 * @point: the touch point index, with 0 being the first touch
 *   point received by the action
 * @press_x: (out) (allow-none): return location for the press
 *   event's X coordinate
 * @press_y: (out) (allow-none): return location for the press
 *   event's Y coordinate
 *
 * Retrieves the coordinates, in stage space, of the press event
 * that started the dragging for a specific touch point.
 */
void
clutter_gesture_action_get_press_coords (ClutterGestureAction *action,
                                         guint                 point,
                                         gfloat               *press_x,
                                         gfloat               *press_y)
{
  ClutterGestureActionPrivate *priv;

  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));

  priv = clutter_gesture_action_get_instance_private (action);

  g_return_if_fail (priv->points->len > point);

  if (press_x)
    *press_x = g_array_index (priv->points,
                              GesturePoint,
                              point).press_x;

  if (press_y)
    *press_y = g_array_index (priv->points,
                              GesturePoint,
                              point).press_y;
}

/**
 * clutter_gesture_action_get_motion_coords:
 * @action: a #ClutterGestureAction
 * @point: the touch point index, with 0 being the first touch
 *   point received by the action
 * @motion_x: (out) (allow-none): return location for the latest motion
 *   event's X coordinate
 * @motion_y: (out) (allow-none): return location for the latest motion
 *   event's Y coordinate
 *
 * Retrieves the coordinates, in stage space, of the latest motion
 * event during the dragging.
 */
void
clutter_gesture_action_get_motion_coords (ClutterGestureAction *action,
                                          guint                 point,
                                          gfloat               *motion_x,
                                          gfloat               *motion_y)
{
  ClutterGestureActionPrivate *priv;

  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));

  priv = clutter_gesture_action_get_instance_private (action);

  g_return_if_fail (priv->points->len > point);

  if (motion_x)
    *motion_x = g_array_index (priv->points,
                               GesturePoint,
                               point).last_motion_x;

  if (motion_y)
    *motion_y = g_array_index (priv->points,
                               GesturePoint,
                               point).last_motion_y;
}

/**
 * clutter_gesture_action_get_motion_delta:
 * @action: a #ClutterGestureAction
 * @point: the touch point index, with 0 being the first touch
 *   point received by the action
 * @delta_x: (out) (allow-none): return location for the X axis
 *   component of the incremental motion delta
 * @delta_y: (out) (allow-none): return location for the Y axis
 *   component of the incremental motion delta
 *
 * Retrieves the incremental delta since the last motion event
 * during the dragging.
 *
 * Return value: the distance since last motion event
 */
gfloat
clutter_gesture_action_get_motion_delta (ClutterGestureAction *action,
                                         guint                 point,
                                         gfloat               *delta_x,
                                         gfloat               *delta_y)
{
  ClutterGestureActionPrivate *priv;
  gfloat d_x, d_y;

  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), 0);

  priv = clutter_gesture_action_get_instance_private (action);

  g_return_val_if_fail (priv->points->len > point, 0);

  d_x = g_array_index (priv->points,
                       GesturePoint,
                       point).last_delta_x;
  d_y = g_array_index (priv->points,
                       GesturePoint,
                       point).last_delta_y;

  if (delta_x)
    *delta_x = d_x;

  if (delta_y)
    *delta_y = d_y;

  return sqrtf ((d_x * d_x) + (d_y * d_y));
}

/**
 * clutter_gesture_action_get_release_coords:
 * @action: a #ClutterGestureAction
 * @point: the touch point index, with 0 being the first touch
 *   point received by the action
 * @release_x: (out) (allow-none): return location for the X coordinate of
 *   the last release
 * @release_y: (out) (allow-none): return location for the Y coordinate of
 *   the last release
 *
 * Retrieves the coordinates, in stage space, where the touch point was
 * last released.
 */
void
clutter_gesture_action_get_release_coords (ClutterGestureAction *action,
                                           guint                 point,
                                           gfloat               *release_x,
                                           gfloat               *release_y)
{
  ClutterGestureActionPrivate *priv;

  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));

  priv = clutter_gesture_action_get_instance_private (action);

  g_return_if_fail (priv->points->len > point);

  if (release_x)
    *release_x = g_array_index (priv->points,
                                GesturePoint,
                                point).release_x;

  if (release_y)
    *release_y = g_array_index (priv->points,
                                GesturePoint,
                                point).release_y;
}

/**
 * clutter_gesture_action_get_velocity:
 * @action: a #ClutterGestureAction
 * @point: the touch point index, with 0 being the first touch
 *   point received by the action
 * @velocity_x: (out) (allow-none): return location for the latest motion
 *   event's X velocity
 * @velocity_y: (out) (allow-none): return location for the latest motion
 *   event's Y velocity
 *
 * Retrieves the velocity, in stage pixels per millisecond, of the
 * latest motion event during the dragging.
 */
gfloat
clutter_gesture_action_get_velocity (ClutterGestureAction *action,
                                     guint                 point,
                                     gfloat               *velocity_x,
                                     gfloat               *velocity_y)
{
  ClutterGestureActionPrivate *priv;
  gfloat d_x, d_y, distance, velocity;
  gint64 d_t;

  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), 0);

  priv = clutter_gesture_action_get_instance_private (action);

  g_return_val_if_fail (priv->points->len > point, 0);

  distance = clutter_gesture_action_get_motion_delta (action, point,
                                                      &d_x, &d_y);

  d_t = g_array_index (priv->points,
                       GesturePoint,
                       point).last_delta_time;

  if (velocity_x)
    *velocity_x = d_t > FLOAT_EPSILON ? d_x / d_t : 0;

  if (velocity_y)
    *velocity_y = d_t > FLOAT_EPSILON ? d_y / d_t : 0;

  velocity = d_t > FLOAT_EPSILON ? distance / d_t : 0;
  return velocity;
}

/**
 * clutter_gesture_action_get_n_touch_points:
 * @action: a #ClutterGestureAction
 *
 * Retrieves the number of requested points to trigger the gesture.
 *
 * Return value: the number of points to trigger the gesture.
 */
gint
clutter_gesture_action_get_n_touch_points (ClutterGestureAction *action)
{
  ClutterGestureActionPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), 0);

  priv = clutter_gesture_action_get_instance_private (action);

  return priv->requested_nb_points;
}

/**
 * clutter_gesture_action_set_n_touch_points:
 * @action: a #ClutterGestureAction
 * @nb_points: a number of points
 *
 * Sets the number of points needed to trigger the gesture.
 */
void
clutter_gesture_action_set_n_touch_points (ClutterGestureAction *action,
                                           gint                  nb_points)
{
  ClutterGestureActionPrivate *priv;

  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));
  g_return_if_fail (nb_points >= 1);

  priv = clutter_gesture_action_get_instance_private (action);

  if (priv->requested_nb_points == nb_points)
    return;

  priv->requested_nb_points = nb_points;

  if (priv->in_gesture)
    {
      if (priv->points->len < priv->requested_nb_points)
        cancel_gesture (action);
    }
  else if (priv->edge == CLUTTER_GESTURE_TRIGGER_EDGE_AFTER)
    {
      if (priv->points->len >= priv->requested_nb_points)
        {
          ClutterActor *actor =
            clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));
          gint i;
          float threshold_x, threshold_y;

          clutter_gesture_action_get_threshold_trigger_distance (action, &threshold_x, &threshold_y);

          for (i = 0; i < priv->points->len; i++)
            {
              GesturePoint *point = &g_array_index (priv->points, GesturePoint, i);

              if ((fabsf (point->press_y - point->last_motion_y) >= threshold_y) ||
                  (fabsf (point->press_x - point->last_motion_x) >= threshold_x))
                {
                  begin_gesture (action, actor);
                  break;
                }
            }
        }
    }

  g_object_notify_by_pspec (G_OBJECT (action),
                            gesture_props[PROP_N_TOUCH_POINTS]);
}

/**
 * clutter_gesture_action_get_n_current_points:
 * @action: a #ClutterGestureAction
 *
 * Retrieves the number of points currently active.
 *
 * Return value: the number of points currently active.
 */
guint
clutter_gesture_action_get_n_current_points (ClutterGestureAction *action)
{
  ClutterGestureActionPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), 0);

  priv = clutter_gesture_action_get_instance_private (action);

  return priv->points->len;
}

/**
 * clutter_gesture_action_get_sequence:
 * @action: a #ClutterGestureAction
 * @point: index of a point currently active
 *
 * Retrieves the #ClutterEventSequence of a touch point.
 *
 * Return value: (transfer none): the #ClutterEventSequence of a touch point.
 */
ClutterEventSequence *
clutter_gesture_action_get_sequence (ClutterGestureAction *action,
                                     guint                 point)
{
  ClutterGestureActionPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), NULL);

  priv = clutter_gesture_action_get_instance_private (action);

  g_return_val_if_fail (priv->points->len > point, NULL);

  return g_array_index (priv->points, GesturePoint, point).sequence;
}

/**
 * clutter_gesture_action_get_device:
 * @action: a #ClutterGestureAction
 * @point: the touch point index, with 0 being the first touch
 *   point received by the action
 *
 * Retrieves the #ClutterInputDevice of a touch point.
 *
 * Return value: (transfer none): the #ClutterInputDevice of a touch point.
 */
ClutterInputDevice *
clutter_gesture_action_get_device (ClutterGestureAction *action,
                                   guint                 point)
{
  ClutterGestureActionPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), NULL);

  priv = clutter_gesture_action_get_instance_private (action);

  g_return_val_if_fail (priv->points->len > point, NULL);

  return g_array_index (priv->points, GesturePoint, point).device;
}

/**
 * clutter_gesture_action_get_last_event:
 * @action: a #ClutterGestureAction
 * @point: index of a point currently active
 *
 * Retrieves a reference to the last #ClutterEvent for a touch point. Call
 * [method@Event.copy] if you need to store the reference somewhere.
 *
 * Return value: (transfer none): the last #ClutterEvent for a touch point.
 */
const ClutterEvent *
clutter_gesture_action_get_last_event (ClutterGestureAction *action,
                                       guint                 point)
{
  GesturePoint *gesture_point;
  ClutterGestureActionPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), NULL);

  priv = clutter_gesture_action_get_instance_private (action);

  g_return_val_if_fail (priv->points->len > point, NULL);

  gesture_point = &g_array_index (priv->points, GesturePoint, point);

  return gesture_point->last_event;
}

/**
 * clutter_gesture_action_cancel:
 * @action: a #ClutterGestureAction
 *
 * Cancel a #ClutterGestureAction before it begins
 */
void
clutter_gesture_action_cancel (ClutterGestureAction *action)
{
  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));

  cancel_gesture (action);
}

/**
 * clutter_gesture_action_set_threshold_trigger_edge:
 * @action: a #ClutterGestureAction
 * @edge: the %ClutterGestureTriggerEdge
 *
 * Sets the edge trigger for the gesture drag threshold, if any.
 *
 * This function should only be called by sub-classes of
 * #ClutterGestureAction during their construction phase.
 */
void
clutter_gesture_action_set_threshold_trigger_edge (ClutterGestureAction      *action,
                                                   ClutterGestureTriggerEdge  edge)
{
  ClutterGestureActionPrivate *priv;

  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));

  priv = clutter_gesture_action_get_instance_private (action);

  if (priv->edge == edge)
    return;

  priv->edge = edge;

  g_object_notify_by_pspec (G_OBJECT (action), gesture_props[PROP_THRESHOLD_TRIGGER_EDGE]);
}

/**
 * clutter_gesture_action_get_threshold_trigger_edge:
 * @action: a #ClutterGestureAction
 *
 * Retrieves the edge trigger of the gesture @action, as set using
 * [method@GestureAction.set_threshold_trigger_edge].
 *
 * Return value: the edge trigger0
 */
ClutterGestureTriggerEdge
clutter_gesture_action_get_threshold_trigger_edge (ClutterGestureAction *action)
{
  ClutterGestureActionPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action),
                        CLUTTER_GESTURE_TRIGGER_EDGE_NONE);

  priv = clutter_gesture_action_get_instance_private (action);

  return priv->edge;
}

/**
 * clutter_gesture_action_set_threshold_trigger_distance:
 * @action: a #ClutterGestureAction
 * @x: the distance on the horizontal axis
 * @y: the distance on the vertical axis
 *
 * Sets the threshold trigger distance for the gesture drag threshold, if any.
 *
 * This function should only be called by sub-classes of
 * #ClutterGestureAction during their construction phase.
 */
void
clutter_gesture_action_set_threshold_trigger_distance (ClutterGestureAction      *action,
                                                       float                      x,
                                                       float                      y)
{
  ClutterGestureActionPrivate *priv;

  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));

  priv = clutter_gesture_action_get_instance_private (action);

  if (fabsf (x - priv->distance_x) > FLOAT_EPSILON)
    {
      priv->distance_x = x;
      g_object_notify_by_pspec (G_OBJECT (action), gesture_props[PROP_THRESHOLD_TRIGGER_DISTANCE_X]);
    }

  if (fabsf (y - priv->distance_y) > FLOAT_EPSILON)
    {
      priv->distance_y = y;
      g_object_notify_by_pspec (G_OBJECT (action), gesture_props[PROP_THRESHOLD_TRIGGER_DISTANCE_Y]);
    }
}

/**
 * clutter_gesture_action_get_threshold_trigger_distance:
 * @action: a #ClutterGestureAction
 * @x: (out) (allow-none): The return location for the horizontal distance, or %NULL
 * @y: (out) (allow-none): The return location for the vertical distance, or %NULL
 *
 * Retrieves the threshold trigger distance of the gesture @action,
 * as set using [method@GestureAction.set_threshold_trigger_distance].
 */
void
clutter_gesture_action_get_threshold_trigger_distance (ClutterGestureAction *action,
                                                       float                *x,
                                                       float                *y)
{
  ClutterGestureActionPrivate *priv;

  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));

  priv = clutter_gesture_action_get_instance_private (action);

  if (x != NULL)
    {
      if (priv->distance_x > 0.0)
        *x = priv->distance_x;
      else
        *x = gesture_get_default_threshold ();
    }
  if (y != NULL)
    {
      if (priv->distance_y > 0.0)
        *y = priv->distance_y;
      else
        *y = gesture_get_default_threshold ();
    }
}
