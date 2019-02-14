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
 * SECTION:clutter-gesture-action
 * @Title: ClutterGestureAction
 * @Short_Description: Action for touch and mouse gestures
 *
 * #ClutterGestureAction is a sub-class of #ClutterAction that implements the
 * logic for recognizing touch and mouse gestures. It listens for low level events
 * such as #ClutterButtonEvent, #ClutterMotionEvent and #ClutterTouchEvent to raise
 * the #ClutterGestureAction::gesture-begin, #ClutterGestureAction::gesture-progress,
 * and #ClutterGestureAction::gesture-end signals.
 *
 * To use #ClutterGestureAction you just need to apply it to a #ClutterActor
 * using clutter_actor_add_action() and connect to the signals:
 *
 * |[<!-- language="C" -->
 *   ClutterAction *action = clutter_gesture_action_new ();
 *
 *   clutter_actor_add_action (actor, action);
 *
 *   g_signal_connect (action, "gesture-begin", G_CALLBACK (on_gesture_begin), NULL);
 *   g_signal_connect (action, "gesture-progress", G_CALLBACK (on_gesture_progress), NULL);
 *   g_signal_connect (action, "gesture-end", G_CALLBACK (on_gesture_end), NULL);
 *   g_signal_connect (action, "gesture-cancel", G_CALLBACK (on_gesture_cancel), NULL);
 * ]|
 *
 * ## Creating Gesture actions
 *
 * A #ClutterGestureAction uses four separate states to differentiate between the
 * phases of gesture recognition. Those states also define whether to block or
 * allow event delivery:
 *
 * - Waiting: In this state the gesture has not been recognized yet, all events
 *   get propagated until the gesture has begun. Gesture detection always starts in
 *   this state as soon as the first touchpoint or click is detected.
 *
 * - Recognized: The gesture is being recognized, delivery of all touch and
 *   pointer events is stopped.
 *
 * - Ended: The gesture was sucessfully recognized and has ended, delivery of
 *   events that belong to existing points is stopped, events that belong to points
 *   added after the gesture has ended are propagated.
 *
 * - Cancelled: The gesture was either not started at all because preconditions
 *   defined by the implementation were not fulfilled or it was cancelled while
 *   being recognized. In this state, all events are propagated.
 *
 *
 * The state can be set in the following sequences:
 *
 * - Waiting -> Recognized -> Ended
 * - Waiting -> Recognized -> Cancelled
 * - Waiting -> Cancelled
 *
 * Each #ClutterGestureAction starts in the "waiting" state and calls the
 * #ClutterGestureActionClass.gesture_prepare() virtual function as soon as the
 * required number of points is fulfilled; this function can be used to check
 * if the gesture should be started at this exact moment or later during the
 * user interaction. Returning FALSE will cause the gesture action to just ignore
 * the event while remaining in the "waiting" state, so every following event
 * will still call #ClutterGestureActionClass.gesture_prepare().
 *
 * If #ClutterGestureActionClass.gesture_prepare() returned TRUE, the "gesture-begin"
 * signal gets emitted. Now a return value of FALSE will cancel the recognition
 * process by setting the state to "cancelled" and only allow beginning the gesture
 * again after all touchpoints or buttons have been released.
 *
 * If both virtual functions and signals returned TRUE, the #ClutterGestureAction
 * will switch to the "recognized" state. In this state the "gesture-progress" signal
 * gets emitted on every motion or touch-update event; a return value of FALSE
 * for this signal means that gesture recognition is finished; the "gesture-end"
 * signal gets emitted and the state is set to "ended".
 *
 * The "gesture-end" signal also gets emitted together with a state change to "ended"
 * if the number of points drops below the required number.

 * Gesture recognition can always be cancelled while it's in the "recognized" state
 * using #clutter-gesture-action-cancel. This sets the state to "cancelled" and
 * emits the "gesture-cancel" signal.
 *
 * Since: 1.8
 */

#include "clutter-build-config.h"

#include "clutter-gesture-action-private.h"

#include "clutter-debug.h"
#include "clutter-enum-types.h"
#include "clutter-marshal.h"
#include "clutter-private.h"

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

typedef enum {
  CLUTTER_GESTURE_ACTION_STATE_WAITING,
  CLUTTER_GESTURE_ACTION_STATE_RECOGNIZED,
  CLUTTER_GESTURE_ACTION_STATE_ENDED,
  CLUTTER_GESTURE_ACTION_STATE_CANCELLED
} ClutterGestureActionState;

struct _ClutterGestureActionPrivate
{
  ClutterActor *stage;
  gboolean is_stage_gesture;

  gint requested_nb_points;
  GArray *points;

  guint actor_event_id;
  guint mapped_changed_id;

  ClutterGestureActionState state;
};

enum
{
  PROP_0,

  PROP_N_TOUCH_POINTS,

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
                        const ClutterEvent *event)
{
  ClutterGestureActionPrivate *priv = action->priv;
  GesturePoint *point = NULL;

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
  point->sequence = clutter_event_get_event_sequence (event);

  if (!priv->is_stage_gesture)
    {
      if (point->sequence != NULL)
        clutter_input_device_sequence_grab (point->device, point->sequence,
                                            clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action)));
      else
        clutter_input_device_grab (point->device,
                                   clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action)));
    }

  return point;
}

static GesturePoint *
gesture_find_point (ClutterGestureAction *action,
                    const ClutterEvent *event,
                    gint *position)
{
  ClutterGestureActionPrivate *priv = action->priv;
  GesturePoint *point = NULL;
  ClutterInputDevice *device = clutter_event_get_device (event);
  ClutterEventSequence *sequence;
  gint i;

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
  ClutterGestureActionPrivate *priv = action->priv;

  if (priv->points->len == 0)
    return;

  GesturePoint *point = &g_array_index (priv->points, GesturePoint, position);

  if (!priv->is_stage_gesture)
    {
      if (point->sequence != NULL)
        clutter_input_device_sequence_ungrab (point->device, point->sequence);
      else
        clutter_input_device_ungrab (point->device);
    }

  g_array_remove_index (priv->points, position);

  /* No more fingers on screen, reset state and disconnect stage event handler. */
  if (priv->points->len == 0)
    priv->state = CLUTTER_GESTURE_ACTION_STATE_WAITING;
}

static void
gesture_update_motion_point (GesturePoint *point,
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
gesture_update_release_point (GesturePoint *point,
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

static void
gesture_point_unset (GesturePoint *point)
{
  clutter_event_free (point->last_event);
}

static void
cancel_gesture (ClutterGestureAction *action)
{
  ClutterGestureActionPrivate *priv = action->priv;
  ClutterActor *actor;

  priv->state = CLUTTER_GESTURE_ACTION_STATE_CANCELLED;

  actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));
  g_signal_emit (action, gesture_signals[GESTURE_CANCEL], 0, actor);
}

static gboolean
begin_gesture (ClutterGestureAction *action, gint point)
{
  ClutterGestureActionPrivate *priv = action->priv;
  ClutterActor *actor;
  gboolean return_value;

  if (!clutter_actor_meta_get_enabled (CLUTTER_ACTOR_META (action)))
    return FALSE;

  actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));

  if (!CLUTTER_GESTURE_ACTION_GET_CLASS (action)->gesture_prepare (action, actor, point))
    return FALSE;

  g_signal_emit (action, gesture_signals[GESTURE_BEGIN], 0, actor, point,
                 &return_value);

  if (!return_value)
    {
      cancel_gesture (action);
      return FALSE;
    }

  priv->state = CLUTTER_GESTURE_ACTION_STATE_RECOGNIZED;

  return TRUE;
}

static void
end_gesture (ClutterGestureAction *action, gint point)
{
  ClutterGestureActionPrivate *priv = action->priv;
  ClutterActor *actor;

  priv->state = CLUTTER_GESTURE_ACTION_STATE_ENDED;

  actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));
  g_signal_emit (action, gesture_signals[GESTURE_END], 0, actor, point);

  /* We might have been cancelled inside the event handler */
  if (priv->state == CLUTTER_GESTURE_ACTION_STATE_RECOGNIZED)
    priv->state = CLUTTER_GESTURE_ACTION_STATE_ENDED;
}

static gboolean
actor_event_cb (ClutterActor         *actor,
                ClutterEvent         *event,
                ClutterGestureAction *action)
{
  ClutterGestureActionPrivate *priv = action->priv;
  ClutterEventType event_type;

  event_type = clutter_event_type (event);

  if (event_type != CLUTTER_TOUCH_BEGIN &&
      event_type != CLUTTER_TOUCH_UPDATE &&
      event_type != CLUTTER_TOUCH_END &&
      event_type != CLUTTER_TOUCH_CANCEL &&
      event_type != CLUTTER_BUTTON_PRESS &&
      event_type != CLUTTER_MOTION &&
      event_type != CLUTTER_BUTTON_RELEASE)
    return CLUTTER_EVENT_PROPAGATE;

  if (priv->stage == NULL)
    priv->stage = clutter_actor_get_stage (actor);

  return clutter_gesture_action_eval_event (action, event);
}

gboolean
clutter_gesture_action_eval_event (ClutterGestureAction *action,
                                   const ClutterEvent *event)
{
  ClutterGestureActionPrivate *priv = action->priv;
  gint position;
  GesturePoint *point;
  ClutterEventType event_type;

  event_type = clutter_event_type (event);

  switch (event_type)
    {
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_TOUCH_BEGIN:
      {
        if (priv->state == CLUTTER_GESTURE_ACTION_STATE_ENDED)
          return CLUTTER_EVENT_PROPAGATE;

        point = gesture_register_point (action, event);

        if (priv->state == CLUTTER_GESTURE_ACTION_STATE_RECOGNIZED)
          {
            if (priv->points->len > MAX_GESTURE_POINTS)
              {
                cancel_gesture (action);
                return CLUTTER_EVENT_PROPAGATE;
              }

            return CLUTTER_EVENT_STOP;
          }

        if (priv->state == CLUTTER_GESTURE_ACTION_STATE_WAITING)
          {
            if (priv->points->len >= priv->requested_nb_points &&
                begin_gesture (action, priv->points->len - 1))
              {
                return CLUTTER_EVENT_STOP;
              }
          }
      }
      break;

    case CLUTTER_MOTION:
    case CLUTTER_TOUCH_UPDATE:
        {
          if (priv->state == CLUTTER_GESTURE_ACTION_STATE_ENDED)
            {
              if (gesture_find_point (action, event, &position) == NULL)
                return CLUTTER_EVENT_PROPAGATE;

              return CLUTTER_EVENT_STOP;
            }

          if (priv->state == CLUTTER_GESTURE_ACTION_STATE_WAITING)
            {
              if (priv->points->len >= priv->requested_nb_points)
                {
                  if ((point = gesture_find_point (action, event, &position)) == NULL)
                    return CLUTTER_EVENT_PROPAGATE;

                  gesture_update_motion_point (point, event);

                  if (begin_gesture (action, position))
                    {
                      return CLUTTER_EVENT_STOP;
                    }
                }
            }

          if (priv->state == CLUTTER_GESTURE_ACTION_STATE_RECOGNIZED)
            {
              if ((point = gesture_find_point (action, event, &position)) == NULL)
                return CLUTTER_EVENT_PROPAGATE;

              gesture_update_motion_point (point, event);

              g_signal_emit (action, gesture_signals[GESTURE_PROGRESS], 0,
                             clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action)), position);

              /* We might have been cancelled inside the event handler */
              if (priv->state == CLUTTER_GESTURE_ACTION_STATE_CANCELLED)
                return CLUTTER_EVENT_PROPAGATE;

              return CLUTTER_EVENT_STOP;
            }
        }
        break;

    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_TOUCH_END:
      {
        if ((point = gesture_find_point (action, event, &position)) == NULL)
          return CLUTTER_EVENT_PROPAGATE;

        gesture_update_release_point (point, event);

        if (priv->state == CLUTTER_GESTURE_ACTION_STATE_RECOGNIZED)
          {
            if ((priv->points->len - 1) < priv->requested_nb_points)
              {
                end_gesture (action, position);

                gesture_unregister_point (action, position);
                return CLUTTER_EVENT_STOP;
              }
          }

        gesture_unregister_point (action, position);
      }
      break;

    case CLUTTER_TOUCH_CANCEL:
      {
        clutter_gesture_action_reset (action);
      }
      break;

    default:
      break;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static void
clutter_gesture_action_set_actor (ClutterActorMeta *meta,
                                  ClutterActor     *actor)
{
  ClutterGestureAction *action = CLUTTER_GESTURE_ACTION (meta);
  ClutterGestureActionPrivate *priv = action->priv;
  ClutterActorMetaClass *meta_class =
    CLUTTER_ACTOR_META_CLASS (clutter_gesture_action_parent_class);

  ClutterActor *old_actor = clutter_actor_meta_get_actor (meta);

  if (old_actor != NULL)
    {
      if (priv->actor_event_id != 0)
        {
          g_signal_handler_disconnect (old_actor, priv->actor_event_id);
          priv->actor_event_id = 0;
        }

      if (priv->mapped_changed_id != 0)
        {
          g_signal_handler_disconnect (old_actor, priv->mapped_changed_id);
          priv->mapped_changed_id = 0;
        }
    }

  if (actor != NULL)
    {
      /* If the new actor is the stage, we don't want to connect to
       * its captured-event signal. Mutter handles those events in the
       * event filter and calls clutter_gesture_action_eval_event()
       * directly from there.
       */
      if (CLUTTER_IS_STAGE (actor))
        {
          priv->is_stage_gesture = TRUE;
        }
      else
        {
          priv->is_stage_gesture = FALSE;
          priv->actor_event_id =
            g_signal_connect (actor, "event", G_CALLBACK (actor_event_cb), action);

          /* As long as a device or sequence grab doesn't guarantee us that we receive
           * every single event (X11 and filtering all events before delivering them to
           * Clutter are the biggest problems here), we have to rely on the actors mapped
           * property being toggled in those situations for now.
           */
          priv->mapped_changed_id =
            g_signal_connect_swapped (actor, "notify::mapped",
                                      G_CALLBACK (clutter_gesture_action_reset), action);
        }
    }

  meta_class->set_actor (meta, actor);
}

static gboolean
default_event_handler (ClutterGestureAction *action,
                       ClutterActor *actor,
                       gint point)
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

  switch (prop_id)
    {
    case PROP_N_TOUCH_POINTS:
      clutter_gesture_action_set_n_touch_points (self, g_value_get_int (value));
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
  ClutterGestureAction *self = CLUTTER_GESTURE_ACTION (gobject);

  switch (prop_id)
    {
    case PROP_N_TOUCH_POINTS:
      g_value_set_int (value, self->priv->requested_nb_points);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_gesture_action_finalize (GObject *gobject)
{
  ClutterGestureActionPrivate *priv = CLUTTER_GESTURE_ACTION (gobject)->priv;

  g_array_unref (priv->points);

  G_OBJECT_CLASS (clutter_gesture_action_parent_class)->finalize (gobject);
}

static void
clutter_gesture_action_class_init (ClutterGestureActionClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorMetaClass *meta_class = CLUTTER_ACTOR_META_CLASS (klass);

  gobject_class->finalize = clutter_gesture_action_finalize;
  gobject_class->set_property = clutter_gesture_action_set_property;
  gobject_class->get_property = clutter_gesture_action_get_property;

  meta_class->set_actor = clutter_gesture_action_set_actor;

  klass->gesture_prepare = default_event_handler;
  klass->gesture_begin = default_event_handler;

  /**
   * ClutterGestureAction:n-touch-points:
   *
   * Number of touch points to trigger a gesture action.
   *
   * Since: 1.16
   */
  gesture_props[PROP_N_TOUCH_POINTS] =
    g_param_spec_int ("n-touch-points",
                      P_("Number touch points"),
                      P_("Number of touch points"),
                      1, G_MAXINT, 1,
                      CLUTTER_PARAM_READWRITE);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     gesture_props);

  /**
   * ClutterGestureAction::gesture-begin:
   * @action: the #ClutterGestureAction that emitted the signal
   * @actor: the #ClutterActor attached to the @action
   * @point: the #gint of the point that caused the signal
   *
   * The ::gesture_begin signal is emitted when the #ClutterActor to which
   * a #ClutterGestureAction has been applied starts receiving a gesture.
   *
   * Return value: %TRUE if the gesture should start, and %FALSE if
   *   the gesture should be ignored.
   *
   * Since: 1.8
   */
  gesture_signals[GESTURE_BEGIN] =
    g_signal_new (I_("gesture-begin"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGestureActionClass, gesture_begin),
                  _clutter_boolean_continue_accumulator, NULL,
                  _clutter_marshal_BOOLEAN__OBJECT_INT,
                  G_TYPE_BOOLEAN, 2,
                  CLUTTER_TYPE_ACTOR,
                  G_TYPE_INT);

  /**
   * ClutterGestureAction::gesture-progress:
   * @action: the #ClutterGestureAction that emitted the signal
   * @actor: the #ClutterActor attached to the @action
   * @point: the #gint of the point that caused the signal
   *
   * The ::gesture-progress signal is emitted for each motion event after
   * the #ClutterGestureAction::gesture-begin signal has been emitted.
   *
   * Since: 1.8
   */
  gesture_signals[GESTURE_PROGRESS] =
    g_signal_new (I_("gesture-progress"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGestureActionClass, gesture_progress),
                  NULL, NULL,
                  _clutter_marshal_VOID__OBJECT_INT,
                  G_TYPE_NONE, 2,
                  CLUTTER_TYPE_ACTOR,
                  G_TYPE_INT);

  /**
   * ClutterGestureAction::gesture-end:
   * @action: the #ClutterGestureAction that emitted the signal
   * @actor: the #ClutterActor attached to the @action
   * @point: the #gint of the point that caused the signal
   *
   * The ::gesture-end signal is emitted at the end of the gesture gesture,
   * when the pointer's button is released
   *
   * This signal is emitted if and only if the #ClutterGestureAction::gesture-begin
   * signal has been emitted first.
   *
   * Since: 1.8
   */
  gesture_signals[GESTURE_END] =
    g_signal_new (I_("gesture-end"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGestureActionClass, gesture_end),
                  NULL, NULL,
                  _clutter_marshal_VOID__OBJECT_INT,
                  G_TYPE_NONE, 2,
                  CLUTTER_TYPE_ACTOR,
                  G_TYPE_INT);

  /**
   * ClutterGestureAction::gesture-cancel:
   * @action: the #ClutterGestureAction that emitted the signal
   * @actor: the #ClutterActor attached to the @action
   *
   * The ::gesture-cancel signal is emitted when the ongoing gesture gets
   * cancelled from the #ClutterGestureAction::gesture-progress signal handler.
   *
   * This signal is emitted if and only if the #ClutterGestureAction::gesture-begin
   * signal has been emitted first.
   *
   * Since: 1.8
   */
  gesture_signals[GESTURE_CANCEL] =
    g_signal_new (I_("gesture-cancel"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGestureActionClass, gesture_cancel),
                  NULL, NULL,
                  _clutter_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_ACTOR);
}

static void
clutter_gesture_action_init (ClutterGestureAction *self)
{
  self->priv = clutter_gesture_action_get_instance_private (self);

  self->priv->points = g_array_sized_new (FALSE, TRUE, sizeof (GesturePoint), 3);
  g_array_set_clear_func (self->priv->points, (GDestroyNotify) gesture_point_unset);

  self->priv->mapped_changed_id = 0;

  self->priv->requested_nb_points = 1;
  self->priv->state = CLUTTER_GESTURE_ACTION_STATE_WAITING;
}

/**
 * clutter_gesture_action_new:
 *
 * Creates a new #ClutterGestureAction instance.
 *
 * Return value: the newly created #ClutterGestureAction
 *
 * Since: 1.8
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
 *
 * Since: 1.8
 */
void
clutter_gesture_action_get_press_coords (ClutterGestureAction *action,
                                         guint                 point,
                                         gfloat               *press_x,
                                         gfloat               *press_y)
{
  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));
  g_return_if_fail (action->priv->points->len > point);

  if (press_x)
    *press_x = g_array_index (action->priv->points,
                              GesturePoint,
                              point).press_x;

  if (press_y)
    *press_y = g_array_index (action->priv->points,
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
 *
 * Since: 1.8
 */
void
clutter_gesture_action_get_motion_coords (ClutterGestureAction *action,
                                          guint                 point,
                                          gfloat               *motion_x,
                                          gfloat               *motion_y)
{
  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));
  g_return_if_fail (action->priv->points->len > point);

  if (motion_x)
    *motion_x = g_array_index (action->priv->points,
                               GesturePoint,
                               point).last_motion_x;

  if (motion_y)
    *motion_y = g_array_index (action->priv->points,
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
 *
 * Since: 1.12
 */
gfloat
clutter_gesture_action_get_motion_delta (ClutterGestureAction *action,
                                         guint                 point,
                                         gfloat               *delta_x,
                                         gfloat               *delta_y)
{
  gfloat d_x, d_y;

  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), 0);
  g_return_val_if_fail (action->priv->points->len > point, 0);

  d_x = g_array_index (action->priv->points,
                       GesturePoint,
                       point).last_delta_x;
  d_y = g_array_index (action->priv->points,
                       GesturePoint,
                       point).last_delta_y;

  if (delta_x)
    *delta_x = d_x;

  if (delta_y)
    *delta_y = d_y;

  return sqrt ((d_x * d_x) + (d_y * d_y));
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
 *
 * Since: 1.8
 */
void
clutter_gesture_action_get_release_coords (ClutterGestureAction *action,
                                           guint                 point,
                                           gfloat               *release_x,
                                           gfloat               *release_y)
{
  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));
  g_return_if_fail (action->priv->points->len > point);

  if (release_x)
    *release_x = g_array_index (action->priv->points,
                                GesturePoint,
                                point).release_x;

  if (release_y)
    *release_y = g_array_index (action->priv->points,
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
 *
 * Since: 1.12
 */
gfloat
clutter_gesture_action_get_velocity (ClutterGestureAction *action,
                                     guint                 point,
                                     gfloat               *velocity_x,
                                     gfloat               *velocity_y)
{
  gfloat d_x, d_y, distance, velocity;
  gint64 d_t;

  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), 0);
  g_return_val_if_fail (action->priv->points->len > point, 0);

  distance = clutter_gesture_action_get_motion_delta (action, point,
                                                      &d_x, &d_y);

  d_t = g_array_index (action->priv->points,
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
 *
 * Since: 1.12
 */
gint
clutter_gesture_action_get_n_touch_points (ClutterGestureAction *action)
{
  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), 0);

  return action->priv->requested_nb_points;
}

/**
 * clutter_gesture_action_set_n_touch_points:
 * @action: a #ClutterGestureAction
 * @nb_points: a number of points
 *
 * Sets the number of points needed to trigger the gesture.
 *
 * Since: 1.12
 */
void
clutter_gesture_action_set_n_touch_points (ClutterGestureAction *action,
                                           gint                  nb_points)
{
  ClutterGestureActionPrivate *priv;

  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));
  g_return_if_fail (nb_points >= 1);
  g_return_if_fail (nb_points <= MAX_GESTURE_POINTS);

  priv = action->priv;

  if (priv->requested_nb_points == nb_points)
    return;

  priv->requested_nb_points = nb_points;

  if (priv->state == CLUTTER_GESTURE_ACTION_STATE_RECOGNIZED)
    {
      if (priv->points->len < priv->requested_nb_points)
        cancel_gesture (action);
    }
  else if (priv->state == CLUTTER_GESTURE_ACTION_STATE_WAITING)
    {
      if (priv->points->len >= priv->requested_nb_points)
        begin_gesture (action, -1);
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
 *
 * Since: 1.12
 */
guint
clutter_gesture_action_get_n_current_points (ClutterGestureAction *action)
{
  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), 0);

  return action->priv->points->len;
}

/**
 * clutter_gesture_action_get_sequence:
 * @action: a #ClutterGestureAction
 * @point: index of a point currently active
 *
 * Retrieves the #ClutterEventSequence of a touch point.
 *
 * Return value: (transfer none): the #ClutterEventSequence of a touch point.
 *
 * Since: 1.12
 */
ClutterEventSequence *
clutter_gesture_action_get_sequence (ClutterGestureAction *action,
                                     guint                 point)
{
  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), NULL);
  g_return_val_if_fail (action->priv->points->len > point, NULL);

  return g_array_index (action->priv->points, GesturePoint, point).sequence;
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
 *
 * Since: 1.12
 */
ClutterInputDevice *
clutter_gesture_action_get_device (ClutterGestureAction *action,
                                   guint                 point)
{
  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), NULL);
  g_return_val_if_fail (action->priv->points->len > point, NULL);

  return g_array_index (action->priv->points, GesturePoint, point).device;
}

/**
 * clutter_gesture_action_get_last_event:
 * @action: a #ClutterGestureAction
 * @point: index of a point currently active
 *
 * Retrieves a reference to the last #ClutterEvent for a touch point. Call
 * clutter_event_copy() if you need to store the reference somewhere.
 *
 * Return value: (transfer none): the last #ClutterEvent for a touch point.
 *
 * Since: 1.14
 */
const ClutterEvent *
clutter_gesture_action_get_last_event (ClutterGestureAction *action,
                                       guint                 point)
{
  GesturePoint *gesture_point;

  g_return_val_if_fail (CLUTTER_IS_GESTURE_ACTION (action), NULL);
  g_return_val_if_fail (action->priv->points->len > point, NULL);

  gesture_point = &g_array_index (action->priv->points, GesturePoint, point);

  return gesture_point->last_event;
}

/**
 * clutter_gesture_action_end:
 * @action: a #ClutterGestureAction
 *
 * End a #ClutterGestureAction while it's being recognized.
 *
 * Since: ?
 */
void
clutter_gesture_action_end (ClutterGestureAction *action)
{
  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));

  if (action->priv->state != CLUTTER_GESTURE_ACTION_STATE_RECOGNIZED)
    return;

  end_gesture (action, -1);
}

/**
 * clutter_gesture_action_cancel:
 * @action: a #ClutterGestureAction
 *
 * Cancel a #ClutterGestureAction while it's being recognized.
 *
 * Since: 1.12
 */
void
clutter_gesture_action_cancel (ClutterGestureAction *action)
{
  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));

  if (action->priv->state != CLUTTER_GESTURE_ACTION_STATE_RECOGNIZED)
    return;

  cancel_gesture (action);
}

/**
 * clutter_gesture_action_reset:
 * @action: a #ClutterGestureAction
 *
 * Reset the gesture state by cancelling the gesture (if necessary)
 * and removing all touchpoints.
 *
 * This function is not meant to be called from implementations, but
 * only from the display server in case we won't get notified about the
 * next events.
 *
 * Since: 3.32
 */
void
clutter_gesture_action_reset (ClutterGestureAction *action)
{
  g_return_if_fail (CLUTTER_IS_GESTURE_ACTION (action));

  ClutterGestureActionPrivate *priv = action->priv;
  gint i;

  if (priv->state == CLUTTER_GESTURE_ACTION_STATE_RECOGNIZED)
    cancel_gesture (action);

  if (!priv->is_stage_gesture)
    {
      for (i = 0; i < priv->points->len; i++)
        {
          GesturePoint *point = &g_array_index (priv->points, GesturePoint, i);

          if (point->sequence != NULL)
            clutter_input_device_sequence_ungrab (point->device, point->sequence);
          else
            clutter_input_device_ungrab (point->device);
        }
    }

  g_array_set_size (priv->points, 0);
  priv->state = CLUTTER_GESTURE_ACTION_STATE_WAITING;
}
