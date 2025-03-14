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
 */

/**
 * ClutterClickAction:
 *
 * Action for clickable actors
 *
 * #ClutterClickAction is a sub-class of [class@Action] that implements
 * the logic for clickable actors, by using the low level events of
 * [class@Actor], such as [signal@Actor::button-press-event] and
 * [signal@Actor::button-release-event], to synthesize the high level
 * [signal@ClickAction::clicked] signal.
 *
 * To use #ClutterClickAction you just need to apply it to a [class@Actor]
 * using [method@Actor.add_action] and connect to the
 * [signal@ClickAction::clicked] signal:
 *
 * ```c
 *   ClutterAction *action = clutter_click_action_new ();
 *
 *   clutter_actor_add_action (actor, action);
 *
 *   g_signal_connect (action, "clicked", G_CALLBACK (on_clicked), NULL);
 * ```
 *
 * #ClutterClickAction also supports long press gestures: a long press is
 * activated if the pointer remains pressed within a certain threshold (as
 * defined by the [property@ClickAction:long-press-threshold] property) for a
 * minimum amount of time (as the defined by the
 * [property@ClickAction:long-press-duration] property).
 * The [signal@ClickAction::long-press] signal is emitted multiple times,
 * using different [enum@LongPressState] values; to handle long presses
 * you should connect to the [signal@ClickAction::long-press] signal and
 * handle the different states:
 *
 * ```c
 *   static gboolean
 *   on_long_press (ClutterClickAction    *action,
 *                  ClutterActor          *actor,
 *                  ClutterLongPressState  state)
 *   {
 *     switch (state)
 *       {
 *       case CLUTTER_LONG_PRESS_QUERY:
 *         // return TRUE if the actor should support long press
 *         // gestures, and FALSE otherwise; this state will be
 *         // emitted on button presses
 *         return TRUE;
 *
 *       case CLUTTER_LONG_PRESS_ACTIVATE:
 *         // this state is emitted if the minimum duration has
 *         // been reached without the gesture being cancelled.
 *         // the return value is not used
 *         return TRUE;
 *
 *       case CLUTTER_LONG_PRESS_CANCEL:
 *         // this state is emitted if the long press was cancelled;
 *         // for instance, the pointer went outside the actor or the
 *         // allowed threshold, or the button was released before
 *         // the minimum duration was reached. the return value is
 *         // not used
 *         return FALSE;
 *       }
 *   }
 * ```
 */

#include "config.h"

#include "clutter/clutter-click-action.h"

#include "clutter/clutter-debug.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-marshal.h"
#include "clutter/clutter-private.h"

struct _ClutterClickActionPrivate
{
  ClutterActor *stage;

  guint long_press_id;

  gint long_press_threshold;
  gint long_press_duration;
  gint drag_threshold;

  guint press_button;
  ClutterInputDevice *press_device;
  ClutterEventSequence *press_sequence;
  ClutterModifierType modifier_state;
  gfloat press_x;
  gfloat press_y;

  guint is_held    : 1;
  guint is_pressed : 1;
};

enum
{
  PROP_0,

  PROP_HELD,
  PROP_PRESSED,
  PROP_LONG_PRESS_THRESHOLD,
  PROP_LONG_PRESS_DURATION,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };

enum
{
  CLICKED,
  LONG_PRESS,

  LAST_SIGNAL
};

static guint click_signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_PRIVATE (ClutterClickAction, clutter_click_action, CLUTTER_TYPE_ACTION)

static inline void
click_action_set_pressed (ClutterClickAction *action,
                          gboolean            is_pressed)
{
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (action);

  is_pressed = !!is_pressed;

  if (priv->is_pressed == is_pressed)
    return;

  priv->is_pressed = is_pressed;
  g_object_notify_by_pspec (G_OBJECT (action), obj_props[PROP_PRESSED]);
}

static inline void
click_action_set_held (ClutterClickAction *action,
                       gboolean            is_held)
{
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (action);

  is_held = !!is_held;

  if (priv->is_held == is_held)
    return;

  priv->is_held = is_held;
  g_object_notify_by_pspec (G_OBJECT (action), obj_props[PROP_HELD]);
}

static gboolean
click_action_emit_long_press (gpointer data)
{
  ClutterClickAction *action = data;
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (action);
  ClutterActor *actor;
  gboolean result;

  priv->long_press_id = 0;

  actor = clutter_actor_meta_get_actor (data);

  g_signal_emit (action, click_signals[LONG_PRESS], 0,
                 actor,
                 CLUTTER_LONG_PRESS_ACTIVATE,
                 &result);

  click_action_set_pressed (action, FALSE);
  click_action_set_held (action, FALSE);

  return FALSE;
}

static inline void
click_action_query_long_press (ClutterClickAction *action)
{
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (action);
  ClutterActor *actor =
    clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));
  ClutterContext *context = clutter_actor_get_context (actor);
  gboolean result = FALSE;
  gint timeout;

  if (priv->long_press_duration < 0)
    {
      ClutterSettings *settings = clutter_context_get_settings (context);

      g_object_get (settings,
                    "long-press-duration", &timeout,
                    NULL);
    }
  else
    timeout = priv->long_press_duration;


  g_signal_emit (action, click_signals[LONG_PRESS], 0,
                 actor,
                 CLUTTER_LONG_PRESS_QUERY,
                 &result);

  if (result)
    {
      g_clear_handle_id (&priv->long_press_id, g_source_remove);
      priv->long_press_id = g_timeout_add (timeout,
                                           click_action_emit_long_press,
                                           action);
    }
}

static inline void
click_action_cancel_long_press (ClutterClickAction *action)
{
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (action);

  if (priv->long_press_id != 0)
    {
      ClutterActor *actor;
      gboolean result;

      actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));

      g_clear_handle_id (&priv->long_press_id, g_source_remove);

      g_signal_emit (action, click_signals[LONG_PRESS], 0,
                     actor,
                     CLUTTER_LONG_PRESS_CANCEL,
                     &result);
    }
}

static inline gboolean
event_within_drag_threshold (ClutterClickAction *click_action,
                             const ClutterEvent *event)
{
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (click_action);
  float motion_x, motion_y;
  float delta_x, delta_y;

  clutter_event_get_coords (event, &motion_x, &motion_y);

  delta_x = ABS (motion_x - priv->press_x);
  delta_y = ABS (motion_y - priv->press_y);

  return delta_x <= priv->drag_threshold && delta_y <= priv->drag_threshold;
}

static gboolean
clutter_click_action_handle_event (ClutterAction      *action,
                                   const ClutterEvent *event)
{
  ClutterClickAction *click_action = CLUTTER_CLICK_ACTION (action);
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (click_action);
  ClutterActor *actor =
    clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));
  ClutterContext *context = clutter_actor_get_context (actor);
  gboolean has_button = TRUE;
  ClutterModifierType modifier_state;
  ClutterActor *target;

  if (!clutter_actor_meta_get_enabled (CLUTTER_ACTOR_META (action)))
    return CLUTTER_EVENT_PROPAGATE;

  if (priv->press_sequence != NULL &&
      clutter_event_get_event_sequence (event) != priv->press_sequence)
    {
      click_action_set_held (click_action, FALSE);
      click_action_cancel_long_press (click_action);
      return CLUTTER_EVENT_PROPAGATE;
    }

  switch (clutter_event_type (event))
    {
    case CLUTTER_TOUCH_BEGIN:
      has_button = FALSE;

      G_GNUC_FALLTHROUGH;
    case CLUTTER_BUTTON_PRESS:
      if (priv->is_held)
        return CLUTTER_EVENT_STOP;

      target = clutter_stage_get_device_actor (CLUTTER_STAGE (clutter_actor_get_stage (actor)),
                                               clutter_event_get_device (event),
                                               clutter_event_get_event_sequence (event));

      if (!clutter_actor_contains (actor, target))
        return CLUTTER_EVENT_PROPAGATE;

      priv->press_button = has_button ? clutter_event_get_button (event) : 0;
      priv->press_device = clutter_event_get_device (event);
      priv->press_sequence = clutter_event_get_event_sequence (event);
      priv->modifier_state = clutter_event_get_state (event);
      clutter_event_get_coords (event, &priv->press_x, &priv->press_y);

      if (priv->long_press_threshold < 0)
        {
          ClutterSettings *settings = clutter_context_get_settings (context);

          g_object_get (settings,
                        "dnd-drag-threshold", &priv->drag_threshold,
                        NULL);
        }
      else
        priv->drag_threshold = priv->long_press_threshold;

      if (priv->stage == NULL)
        priv->stage = clutter_actor_get_stage (actor);

      click_action_set_pressed (click_action, TRUE);
      click_action_set_held (click_action, TRUE);
      click_action_query_long_press (click_action);
      break;

    case CLUTTER_ENTER:
      click_action_set_pressed (click_action, priv->is_held);
      return CLUTTER_EVENT_PROPAGATE;

    case CLUTTER_LEAVE:
      click_action_set_pressed (click_action, FALSE);
      click_action_cancel_long_press (click_action);
      return CLUTTER_EVENT_PROPAGATE;

    case CLUTTER_TOUCH_CANCEL:
      clutter_click_action_release (click_action);
      break;

    case CLUTTER_TOUCH_END:
      has_button = FALSE;

      G_GNUC_FALLTHROUGH;
    case CLUTTER_BUTTON_RELEASE:
      if (!priv->is_held)
        return CLUTTER_EVENT_PROPAGATE;

      if ((has_button && clutter_event_get_button (event) != priv->press_button) ||
          clutter_event_get_device (event) != priv->press_device ||
          clutter_event_get_event_sequence (event) != priv->press_sequence)
        return CLUTTER_EVENT_PROPAGATE;

      click_action_set_held (click_action, FALSE);
      click_action_cancel_long_press (click_action);

      g_clear_handle_id (&priv->long_press_id, g_source_remove);

      target = clutter_stage_get_device_actor (CLUTTER_STAGE (clutter_actor_get_stage (actor)),
                                               clutter_event_get_device (event),
                                               clutter_event_get_event_sequence (event));

      if (!clutter_actor_contains (actor, target))
        return CLUTTER_EVENT_PROPAGATE;

      /* exclude any button-mask so that we can compare
       * the press and release states properly */
      modifier_state = clutter_event_get_state (event) &
                       ~(CLUTTER_BUTTON1_MASK |
                         CLUTTER_BUTTON2_MASK |
                         CLUTTER_BUTTON3_MASK |
                         CLUTTER_BUTTON4_MASK |
                         CLUTTER_BUTTON5_MASK);

      /* if press and release states don't match we
       * simply ignore modifier keys. i.e. modifier keys
       * are expected to be pressed throughout the whole
       * click */
      if (modifier_state != priv->modifier_state)
        priv->modifier_state = 0;

      click_action_set_pressed (click_action, FALSE);

      if (event_within_drag_threshold (click_action, event))
        g_signal_emit (click_action, click_signals[CLICKED], 0, actor);
      break;

    case CLUTTER_MOTION:
    case CLUTTER_TOUCH_UPDATE:
      {
        if (clutter_event_get_device (event) != priv->press_device ||
            clutter_event_get_event_sequence (event) != priv->press_sequence)
          return CLUTTER_EVENT_PROPAGATE;

        if (!priv->is_held)
          return CLUTTER_EVENT_PROPAGATE;

        if (!event_within_drag_threshold (click_action, event))
          clutter_click_action_release (click_action);
      }
      break;

    default:
      break;
    }

  return priv->is_held ? CLUTTER_EVENT_STOP : CLUTTER_EVENT_PROPAGATE;
}

static void
clutter_click_action_sequence_cancelled (ClutterAction        *action,
                                         ClutterInputDevice   *device,
                                         ClutterEventSequence *sequence)
{
  ClutterClickAction *self = CLUTTER_CLICK_ACTION (action);
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (self);

  if (priv->press_device == device && priv->press_sequence == sequence)
    clutter_click_action_release (self);
}

static void
clutter_click_action_set_actor (ClutterActorMeta *meta,
                                ClutterActor     *actor)
{
  ClutterClickAction *action = CLUTTER_CLICK_ACTION (meta);
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (action);

  g_clear_handle_id (&priv->long_press_id, g_source_remove);

  click_action_set_pressed (action, FALSE);
  click_action_set_held (action, FALSE);

  CLUTTER_ACTOR_META_CLASS (clutter_click_action_parent_class)->set_actor (meta, actor);
}

static void
clutter_click_action_set_enabled (ClutterActorMeta *meta,
                                  gboolean          is_enabled)
{
  ClutterClickAction *click_action = CLUTTER_CLICK_ACTION (meta);
  ClutterActorMetaClass *parent_class =
    CLUTTER_ACTOR_META_CLASS (clutter_click_action_parent_class);

  if (!is_enabled)
    clutter_click_action_release (click_action);

  parent_class->set_enabled (meta, is_enabled);
}

static void
clutter_click_action_set_property (GObject      *gobject,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (CLUTTER_CLICK_ACTION (gobject));

  switch (prop_id)
    {
    case PROP_LONG_PRESS_DURATION:
      priv->long_press_duration = g_value_get_int (value);
      break;

    case PROP_LONG_PRESS_THRESHOLD:
      priv->long_press_threshold = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_click_action_get_property (GObject    *gobject,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (CLUTTER_CLICK_ACTION (gobject));

  switch (prop_id)
    {
    case PROP_HELD:
      g_value_set_boolean (value, priv->is_held);
      break;

    case PROP_PRESSED:
      g_value_set_boolean (value, priv->is_pressed);
      break;

    case PROP_LONG_PRESS_DURATION:
      g_value_set_int (value, priv->long_press_duration);
      break;

    case PROP_LONG_PRESS_THRESHOLD:
      g_value_set_int (value, priv->long_press_threshold);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_click_action_dispose (GObject *gobject)
{
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (CLUTTER_CLICK_ACTION (gobject));

  g_clear_handle_id (&priv->long_press_id, g_source_remove);

  G_OBJECT_CLASS (clutter_click_action_parent_class)->dispose (gobject);
}

static void
clutter_click_action_class_init (ClutterClickActionClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorMetaClass *meta_class = CLUTTER_ACTOR_META_CLASS (klass);
  ClutterActionClass *action_class = CLUTTER_ACTION_CLASS (klass);

  action_class->handle_event = clutter_click_action_handle_event;
  action_class->sequence_cancelled = clutter_click_action_sequence_cancelled;

  meta_class->set_actor = clutter_click_action_set_actor;
  meta_class->set_enabled = clutter_click_action_set_enabled;

  gobject_class->dispose = clutter_click_action_dispose;
  gobject_class->set_property = clutter_click_action_set_property;
  gobject_class->get_property = clutter_click_action_get_property;

  /**
   * ClutterClickAction:pressed:
   *
   * Whether the clickable actor should be in "pressed" state
   */
  obj_props[PROP_PRESSED] =
    g_param_spec_boolean ("pressed", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

  /**
   * ClutterClickAction:held:
   *
   * Whether the clickable actor has the pointer grabbed
   */
  obj_props[PROP_HELD] =
    g_param_spec_boolean ("held", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

  /**
   * ClutterClickAction:long-press-duration:
   *
   * The minimum duration of a press for it to be recognized as a long
   * press gesture, in milliseconds.
   *
   * A value of -1 will make the #ClutterClickAction use the value of
   * the [property@Settings:long-press-duration] property.
   */
  obj_props[PROP_LONG_PRESS_DURATION] =
    g_param_spec_int ("long-press-duration", NULL, NULL,
                      -1, G_MAXINT,
                      -1,
                      G_PARAM_READWRITE |
                      G_PARAM_STATIC_STRINGS);

  /**
   * ClutterClickAction:long-press-threshold:
   *
   * The maximum allowed distance that can be covered (on both axes) before
   * a long press gesture is cancelled, in pixels.
   *
   * A value of -1 will make the #ClutterClickAction use the value of
   * the [property@Settings:dnd-drag-threshold] property.
   */
  obj_props[PROP_LONG_PRESS_THRESHOLD] =
    g_param_spec_int ("long-press-threshold", NULL, NULL,
                      -1, G_MAXINT,
                      -1,
                      G_PARAM_READWRITE |
                      G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     obj_props);

  /**
   * ClutterClickAction::clicked:
   * @action: the #ClutterClickAction that emitted the signal
   * @actor: the #ClutterActor attached to the @action
   *
   * The signal is emitted when the [class@Actor] to which
   * a #ClutterClickAction has been applied should respond to a
   * pointer button press and release events
   */
  click_signals[CLICKED] =
    g_signal_new (I_("clicked"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterClickActionClass, clicked),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_ACTOR);

  /**
   * ClutterClickAction::long-press:
   * @action: the #ClutterClickAction that emitted the signal
   * @actor: the #ClutterActor attached to the @action
   * @state: the long press state
   *
   * The signal is emitted during the long press gesture
   * handling.
   *
   * This signal can be emitted multiple times with different states.
   *
   * The %CLUTTER_LONG_PRESS_QUERY state will be emitted on button presses,
   * and its return value will determine whether the long press handling
   * should be initiated. If the signal handlers will return %TRUE, the
   * %CLUTTER_LONG_PRESS_QUERY state will be followed either by a signal
   * emission with the %CLUTTER_LONG_PRESS_ACTIVATE state if the long press
   * constraints were respected, or by a signal emission with the
   * %CLUTTER_LONG_PRESS_CANCEL state if the long press was cancelled.
   *
   * It is possible to forcibly cancel a long press detection using
   * [method@ClickAction.release].
   *
   * Return value: Only the %CLUTTER_LONG_PRESS_QUERY state uses the
   *   returned value of the handler; other states will ignore it
   */
  click_signals[LONG_PRESS] =
    g_signal_new (I_("long-press"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterClickActionClass, long_press),
                  NULL, NULL,
                  _clutter_marshal_BOOLEAN__OBJECT_ENUM,
                  G_TYPE_BOOLEAN, 2,
                  CLUTTER_TYPE_ACTOR,
                  CLUTTER_TYPE_LONG_PRESS_STATE);
}

static void
clutter_click_action_init (ClutterClickAction *self)
{
  ClutterClickActionPrivate *priv =
    clutter_click_action_get_instance_private (self);

  priv->long_press_threshold = -1;
  priv->long_press_duration = -1;
}

/**
 * clutter_click_action_new:
 *
 * Creates a new #ClutterClickAction instance
 *
 * Return value: the newly created #ClutterClickAction
 */
ClutterAction *
clutter_click_action_new (void)
{
  return g_object_new (CLUTTER_TYPE_CLICK_ACTION, NULL);
}

/**
 * clutter_click_action_release:
 * @action: a #ClutterClickAction
 *
 * Emulates a release of the pointer button, which ungrabs the pointer
 * and unsets the [property@ClickAction:pressed] state.
 *
 * This function will also cancel the long press gesture if one was
 * initiated.
 *
 * This function is useful to break a grab, for instance after a certain
 * amount of time has passed.
 */
void
clutter_click_action_release (ClutterClickAction *action)
{
  ClutterClickActionPrivate *priv;

  g_return_if_fail (CLUTTER_IS_CLICK_ACTION (action));

  priv = clutter_click_action_get_instance_private (action);

  if (!priv->is_held)
    return;

  click_action_cancel_long_press (action);
  click_action_set_held (action, FALSE);
  click_action_set_pressed (action, FALSE);
}

/**
 * clutter_click_action_get_button:
 * @action: a #ClutterClickAction
 *
 * Retrieves the button that was pressed.
 *
 * Return value: the button value
 */
guint
clutter_click_action_get_button (ClutterClickAction *action)
{
  ClutterClickActionPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_CLICK_ACTION (action), 0);

  priv = clutter_click_action_get_instance_private (action);

  return priv->press_button;
}

/**
 * clutter_click_action_get_state:
 * @action: a #ClutterClickAction
 *
 * Retrieves the modifier state of the click action.
 *
 * Return value: the modifier state parameter, or 0
 */
ClutterModifierType
clutter_click_action_get_state (ClutterClickAction *action)
{
  ClutterClickActionPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_CLICK_ACTION (action), 0);

  priv = clutter_click_action_get_instance_private (action);

  return priv->modifier_state;
}

/**
 * clutter_click_action_get_coords:
 * @action: a #ClutterClickAction
 * @press_x: (out): return location for the X coordinate, or %NULL
 * @press_y: (out): return location for the Y coordinate, or %NULL
 *
 * Retrieves the screen coordinates of the button press.
 */
void
clutter_click_action_get_coords (ClutterClickAction *action,
                                 gfloat             *press_x,
                                 gfloat             *press_y)
{
  ClutterClickActionPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTION (action));

  priv = clutter_click_action_get_instance_private (action);

  if (press_x != NULL)
    *press_x = priv->press_x;

  if (press_y != NULL)
    *press_y = priv->press_y;
}
