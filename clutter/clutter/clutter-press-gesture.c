/*
 * Copyright (C) 2023 Jonas Dreßler <verdre@v0yd.nl>
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
 */

/**
 * ClutterPressGesture:
 *
 * An abstract #ClutterGesture subclass building the base for recognizing press
 * gestures
 */

#include "clutter/clutter-press-gesture.h"

#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-private.h"

#define DEFAULT_CANCEL_THRESHOLD 36

typedef struct _ClutterPressGesturePrivate ClutterPressGesturePrivate;

struct _ClutterPressGesturePrivate
{
  gboolean pressed;

  int cancel_threshold;

  int long_press_duration_ms;
  unsigned int long_press_timeout_id;

  unsigned int n_presses_happened;
  unsigned int next_press_timeout_id;

  unsigned int required_button;

  gboolean is_touch;

  graphene_point_t press_coords;
  unsigned int press_button;
  ClutterModifierType modifier_state;
};

enum
{
  PROP_0,

  PROP_CANCEL_THRESHOLD,
  PROP_LONG_PRESS_DURATION_MS,
  PROP_PRESSED,
  PROP_REQUIRED_BUTTON,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterPressGesture,
                                     clutter_press_gesture,
                                     CLUTTER_TYPE_GESTURE)

static unsigned int
get_default_long_press_duration (void)
{
  ClutterContext *context = _clutter_context_get_default ();
  ClutterSettings *settings = clutter_context_get_settings (context);
  int long_press_duration;

  g_object_get (settings,
                "long-press-duration", &long_press_duration,
                NULL);

  return long_press_duration;
}

static unsigned int
get_next_press_timeout_ms (void)
{
  ClutterContext *context = _clutter_context_get_default ();
  ClutterSettings *settings = clutter_context_get_settings (context);
  int double_click_time;

  g_object_get (settings,
                "double-click-time", &double_click_time,
                NULL);

  return double_click_time;
}

static void
long_press_cb (gpointer user_data)
{
  ClutterPressGesture *self = user_data;
  ClutterPressGesturePrivate *priv =
    clutter_press_gesture_get_instance_private (self);

  if (CLUTTER_PRESS_GESTURE_GET_CLASS (self)->long_press)
    CLUTTER_PRESS_GESTURE_GET_CLASS (self)->long_press (self);

  priv->long_press_timeout_id = 0;
}

static void
set_pressed (ClutterPressGesture *self,
             gboolean             pressed)
{
  ClutterPressGesturePrivate *priv =
    clutter_press_gesture_get_instance_private (self);

  if (priv->pressed == pressed)
    return;

  priv->pressed = pressed;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_PRESSED]);
}

static void
reset_n_presses (ClutterPressGesture *self)
{
  ClutterPressGesturePrivate *priv =
    clutter_press_gesture_get_instance_private (self);

  priv->n_presses_happened = 0;
  priv->press_coords.x = 0;
  priv->press_coords.y = 0;
  priv->press_button = 0;
}

static void
next_press_timed_out (gpointer user_data)
{
  ClutterPressGesture *self = user_data;
  ClutterGesture *gesture = user_data;
  ClutterPressGesturePrivate *priv =
    clutter_press_gesture_get_instance_private (self);
  unsigned int active_n_points = clutter_gesture_get_n_points (gesture);

  if (active_n_points == 0)
    clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_CANCELLED);

  reset_n_presses (self);

  priv->next_press_timeout_id = 0;
}

static gboolean
clutter_press_gesture_should_handle_sequence (ClutterGesture     *gesture,
                                              const ClutterEvent *sequence_begin_event)
{
  ClutterEventType event_type = clutter_event_type (sequence_begin_event);

  if (event_type == CLUTTER_BUTTON_PRESS ||
      event_type == CLUTTER_TOUCH_BEGIN)
    return TRUE;

  return FALSE;
}

static void
clutter_press_gesture_point_began (ClutterGesture *gesture,
                                   unsigned int    sequence)
{
  ClutterPressGesture *self = CLUTTER_PRESS_GESTURE (gesture);
  ClutterPressGesturePrivate *priv =
    clutter_press_gesture_get_instance_private (self);
  unsigned int active_n_points = clutter_gesture_get_n_points (gesture);
  const ClutterEvent *event;
  gboolean is_touch;
  unsigned int press_button;
  ClutterModifierType modifier_state;
  graphene_point_t coords;
  unsigned int long_press_duration_ms;

  if (active_n_points != 1)
    {
      clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_CANCELLED);
      return;
    }

  event = clutter_gesture_get_point_event (gesture, sequence);

  is_touch = clutter_event_type (event) == CLUTTER_TOUCH_BEGIN;
  press_button = is_touch ? CLUTTER_BUTTON_PRIMARY : clutter_event_get_button (event);
  modifier_state = clutter_event_get_state (event);
  clutter_gesture_get_point_coords_abs (gesture, sequence, &coords);

  if (priv->required_button != 0 &&
      press_button != priv->required_button)
    {
      clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_CANCELLED);
      return;
    }

  priv->n_presses_happened += 1;

  if (priv->n_presses_happened == 1)
    {
      g_assert (priv->next_press_timeout_id == 0);

      priv->is_touch = is_touch;
      priv->press_button = press_button;
      priv->modifier_state = modifier_state;
      priv->press_coords = coords;
    }
  else
    {
      float distance =
        graphene_point_distance (&priv->press_coords, &coords, NULL, NULL);

      g_assert (priv->next_press_timeout_id > 0);
      g_clear_handle_id (&priv->next_press_timeout_id, g_source_remove);

      if (priv->is_touch != is_touch ||
          priv->press_button != press_button ||
          (priv->cancel_threshold >= 0 && distance > priv->cancel_threshold))
        {
          /* Instead of cancelling the gesture and throwing the point away, leave
           * it RECOGNIZING and treat the point like the first one. It would be
           * neat to cancel and then immediately recognize for the same point
           * but that's not possible due to ClutterGesture clearing points on
           * move to WAITING.
           */

          priv->n_presses_happened = 1;

          priv->is_touch = is_touch;
          priv->press_button = press_button;
          priv->modifier_state = modifier_state;
          priv->press_coords = coords;
        }
    }

  priv->next_press_timeout_id =
    g_timeout_add_once (get_next_press_timeout_ms (), next_press_timed_out, self);

  long_press_duration_ms = priv->long_press_duration_ms == -1
    ? get_default_long_press_duration ()
    : priv->long_press_duration_ms;

  g_assert (priv->long_press_timeout_id == 0);
  priv->long_press_timeout_id =
    g_timeout_add_once (long_press_duration_ms, long_press_cb, self);

  set_pressed (self, TRUE);

  if (CLUTTER_PRESS_GESTURE_GET_CLASS (self)->press)
    CLUTTER_PRESS_GESTURE_GET_CLASS (self)->press (self);
}

static void
clutter_press_gesture_point_moved (ClutterGesture *gesture,
                                   unsigned int    sequence)
{
  ClutterPressGesture *self = CLUTTER_PRESS_GESTURE (gesture);
  ClutterPressGesturePrivate *priv =
    clutter_press_gesture_get_instance_private (self);
  graphene_point_t coords;

  clutter_gesture_get_point_coords_abs (gesture, sequence, &coords);

  float distance =
    graphene_point_distance (&coords, &priv->press_coords, NULL, NULL);

  if (priv->cancel_threshold >= 0 && distance > priv->cancel_threshold)
    clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_CANCELLED);
}

static void
clutter_press_gesture_point_ended (ClutterGesture *gesture,
                                   unsigned int    sequence)
{
  ClutterPressGesture *self = CLUTTER_PRESS_GESTURE (gesture);
  ClutterPressGesturePrivate *priv =
    clutter_press_gesture_get_instance_private (self);
  const ClutterEvent *event;
  ClutterModifierType modifier_state;

  g_clear_handle_id (&priv->long_press_timeout_id, g_source_remove);

  /* Exclude any button-mask so that we can compare
   * the press and release states properly
   */
  event = clutter_gesture_get_point_event (gesture, sequence);
  modifier_state = clutter_event_get_state (event) &
                   CLUTTER_MODIFIER_MASK &
                   ~(CLUTTER_BUTTON1_MASK | CLUTTER_BUTTON2_MASK | CLUTTER_BUTTON3_MASK |
                     CLUTTER_BUTTON4_MASK | CLUTTER_BUTTON5_MASK);

  /* if press and release states don't match we
   * simply ignore modifier keys. i.e. modifier keys
   * are expected to be pressed throughout the whole
   * click
   */
  if (modifier_state != priv->modifier_state)
    priv->modifier_state = 0;

  if (CLUTTER_PRESS_GESTURE_GET_CLASS (self)->release)
    CLUTTER_PRESS_GESTURE_GET_CLASS (self)->release (self);

  set_pressed (self, FALSE);

  /* If the next press has already timed out, we can cancel now. If it hasn't
   * timed out yet, we'll cancel on the timeout.
   */
  if (clutter_gesture_get_state (gesture) != CLUTTER_GESTURE_STATE_COMPLETED &&
      clutter_gesture_get_state (gesture) != CLUTTER_GESTURE_STATE_CANCELLED &&
      priv->next_press_timeout_id == 0)
    clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_CANCELLED);
}

static void
clutter_press_gesture_crossing_event (ClutterGesture    *gesture,
                                      unsigned int       point,
                                      ClutterEventType   type,
                                      uint32_t           time,
                                      ClutterEventFlags  flags,
                                      ClutterActor      *source_actor,
                                      ClutterActor      *related_actor)
{
  ClutterPressGesture *self = CLUTTER_PRESS_GESTURE (gesture);
  ClutterGestureState state = clutter_gesture_get_state (gesture);

  if ((state == CLUTTER_GESTURE_STATE_POSSIBLE || state == CLUTTER_GESTURE_STATE_RECOGNIZING) &&
      source_actor == clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self)))
    set_pressed (self, type == CLUTTER_ENTER);
}

static void
clutter_press_gesture_state_changed (ClutterGesture      *gesture,
                                     ClutterGestureState  old_state,
                                     ClutterGestureState  new_state)
{
  ClutterPressGesture *self = CLUTTER_PRESS_GESTURE (gesture);
  ClutterPressGesturePrivate *priv =
    clutter_press_gesture_get_instance_private (self);

  if (new_state == CLUTTER_GESTURE_STATE_COMPLETED ||
      new_state == CLUTTER_GESTURE_STATE_CANCELLED)
    {
      set_pressed (self, FALSE);
      g_clear_handle_id (&priv->long_press_timeout_id, g_source_remove);
    }

  if (new_state == CLUTTER_GESTURE_STATE_CANCELLED)
    {
      g_clear_handle_id (&priv->next_press_timeout_id, g_source_remove);
      reset_n_presses (self);
    }

  if (new_state == CLUTTER_GESTURE_STATE_WAITING)
    {
      priv->modifier_state = 0;
    }
}

static void
clutter_press_gesture_set_property (GObject      *gobject,
                                    unsigned int  prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  ClutterPressGesture *self = CLUTTER_PRESS_GESTURE (gobject);

  switch (prop_id)
    {
    case PROP_CANCEL_THRESHOLD:
      clutter_press_gesture_set_cancel_threshold (self, g_value_get_int (value));
      break;

    case PROP_LONG_PRESS_DURATION_MS:
      clutter_press_gesture_set_long_press_duration_ms (self, g_value_get_int (value));
      break;

    case PROP_REQUIRED_BUTTON:
      clutter_press_gesture_set_required_button (self, g_value_get_uint (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_press_gesture_get_property (GObject      *gobject,
                                    unsigned int  prop_id,
                                    GValue       *value,
                                    GParamSpec   *pspec)
{
  ClutterPressGesture *self = CLUTTER_PRESS_GESTURE (gobject);

  switch (prop_id)
    {
    case PROP_CANCEL_THRESHOLD:
      g_value_set_int (value, clutter_press_gesture_get_cancel_threshold (self));
      break;

    case PROP_LONG_PRESS_DURATION_MS:
      g_value_set_int (value, clutter_press_gesture_get_long_press_duration_ms (self));
      break;

    case PROP_PRESSED:
      g_value_set_boolean (value, clutter_press_gesture_get_pressed (self));
      break;

    case PROP_REQUIRED_BUTTON:
      g_value_set_uint (value, clutter_press_gesture_get_required_button (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_press_gesture_class_init (ClutterPressGestureClass *klass)
{
  ClutterGestureClass *gesture_class = CLUTTER_GESTURE_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = clutter_press_gesture_set_property;
  gobject_class->get_property = clutter_press_gesture_get_property;

  gesture_class->should_handle_sequence = clutter_press_gesture_should_handle_sequence;
  gesture_class->point_began = clutter_press_gesture_point_began;
  gesture_class->point_moved = clutter_press_gesture_point_moved;
  gesture_class->point_ended = clutter_press_gesture_point_ended;
  gesture_class->crossing_event = clutter_press_gesture_crossing_event;
  gesture_class->state_changed = clutter_press_gesture_state_changed;

  /**
   * ClutterPressGesture:cancel-threshold:
   *
   * Threshold in pixels to cancel the gesture, use -1 to disable the threshold.
   */
  obj_props[PROP_CANCEL_THRESHOLD] =
    g_param_spec_int ("cancel-threshold", NULL, NULL,
                      -1, G_MAXINT, 0,
                      G_PARAM_READWRITE |
                      G_PARAM_STATIC_STRINGS |
                      G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterPressGesture:long-press-duration-ms:
   *
   * The minimum duration of a press in milliseconds for it to be recognized
   * as a long press gesture.
   *
   * A value of -1 (default) will make the gesture use the value of the
   * #ClutterSettings:long-press-duration property.
   */
  obj_props[PROP_LONG_PRESS_DURATION_MS] =
    g_param_spec_int ("long-press-duration-ms", NULL, NULL,
                      -1, G_MAXINT, -1,
                      G_PARAM_READWRITE |
                      G_PARAM_STATIC_STRINGS |
                      G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterPressGesture:pressed:
   *
   * Whether the clickable actor should be in "pressed" state
   */
  obj_props[PROP_PRESSED] =
    g_param_spec_boolean ("pressed", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterPressGesture:required-button:
   *
   * The mouse button required for the press gesture to recognize.
   * Pass 0 to allow any button. Touch input is always handled as a press
   * of the primary button.
   */
  obj_props[PROP_REQUIRED_BUTTON] =
    g_param_spec_uint ("required-button", NULL, NULL,
                       0, G_MAXUINT, 0,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
}

static void
clutter_press_gesture_init (ClutterPressGesture *self)
{
  ClutterPressGesturePrivate *priv =
    clutter_press_gesture_get_instance_private (self);

  priv->cancel_threshold = DEFAULT_CANCEL_THRESHOLD;
  priv->long_press_duration_ms = -1;
}

/**
 * clutter_press_gesture_get_pressed:
 * @self: a #ClutterPressGesture
 *
 * Gets whether the press gesture actor should be in the "pressed" state.
 *
 * Returns: The "pressed" state
 */
gboolean
clutter_press_gesture_get_pressed (ClutterPressGesture *self)
{
  ClutterPressGesturePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PRESS_GESTURE (self), FALSE);

  priv = clutter_press_gesture_get_instance_private (self);

  return priv->pressed;
}

/**
 * clutter_press_gesture_get_cancel_threshold:
 * @self: a #ClutterPressGesture
 *
 * Gets the movement threshold in pixels that cancels the press gesture.
 *
 * Returns: The cancel threshold in pixels
 */
int
clutter_press_gesture_get_cancel_threshold (ClutterPressGesture *self)
{
  ClutterPressGesturePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PRESS_GESTURE (self), -1);

  priv = clutter_press_gesture_get_instance_private (self);

  return priv->cancel_threshold;
}

/**
 * clutter_press_gesture_set_cancel_threshold:
 * @self: a #ClutterPressGesture
 * @cancel_threshold: the threshold in pixels, or -1 to disable the threshold
 *
 * Sets the movement threshold in pixels that cancels the press gesture.
 *
 * See also #ClutterPressGesture:cancel-threshold.
 */
void
clutter_press_gesture_set_cancel_threshold (ClutterPressGesture *self,
                                            int                  cancel_threshold)
{
  ClutterPressGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_PRESS_GESTURE (self));

  priv = clutter_press_gesture_get_instance_private (self);

  if (priv->cancel_threshold == cancel_threshold)
    return;

  priv->cancel_threshold = cancel_threshold;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CANCEL_THRESHOLD]);
}

/**
 * clutter_press_gesture_get_long_press_duration_ms:
 * @self: a #ClutterPressGesture
 *
 * Gets the minimum duration is milliseconds that's necessary for a long press
 * to recognize. A value of -1 means the default from
 * #ClutterSettings:long-press-duration is used.
 */
int
clutter_press_gesture_get_long_press_duration_ms (ClutterPressGesture *self)
{
  ClutterPressGesturePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PRESS_GESTURE (self), -1);

  priv = clutter_press_gesture_get_instance_private (self);

  return priv->long_press_duration_ms;
}

/**
 * clutter_press_gesture_set_long_press_duration_ms:
 * @self: a #ClutterPressGesture
 * @long_press_duration_ms: minimum duration for long press to recognize
 *
 * Sets the minimum duration is milliseconds that's necessary for a long press
 * to recognize.
 *
 * Pass -1 to use the default from #ClutterSettings:long-press-duration.
 */
void
clutter_press_gesture_set_long_press_duration_ms (ClutterPressGesture *self,
                                                  int                  long_press_duration_ms)
{
  ClutterPressGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_PRESS_GESTURE (self));

  priv = clutter_press_gesture_get_instance_private (self);

  if (priv->long_press_duration_ms == long_press_duration_ms)
    return;

  priv->long_press_duration_ms = long_press_duration_ms;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_LONG_PRESS_DURATION_MS]);
}

/**
 * clutter_press_gesture_get_button:
 * @self: a #ClutterPressGesture
 *
 * Retrieves the button that was pressed.
 *
 * Returns: the button value
 */
unsigned int
clutter_press_gesture_get_button (ClutterPressGesture *self)
{
  ClutterPressGesturePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PRESS_GESTURE (self), 0);

  priv = clutter_press_gesture_get_instance_private (self);

  return priv->press_button;
}

/**
 * clutter_press_gesture_get_state:
 * @self: a #ClutterPressGesture
 *
 * Retrieves the modifier state of the press gesture.
 *
 * Returns: the modifier state parameter, or 0
 */
ClutterModifierType
clutter_press_gesture_get_state (ClutterPressGesture *self)
{
  ClutterPressGesturePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PRESS_GESTURE (self), 0);

  priv = clutter_press_gesture_get_instance_private (self);

  return priv->modifier_state;
}

/**
 * clutter_press_gesture_get_coords:
 * @self: a #ClutterPressGesture
 * @coords_out: (out): a #graphene_point_t
 *
 * Retrieves the coordinates of the press.
 */
void
clutter_press_gesture_get_coords (ClutterPressGesture *self,
                                  graphene_point_t    *coords_out)
{
  g_return_if_fail (CLUTTER_IS_PRESS_GESTURE (self));
  g_return_if_fail (coords_out != NULL);

  clutter_gesture_get_point_begin_coords (CLUTTER_GESTURE (self),
                                          0, coords_out);
}

/**
 * clutter_press_gesture_get_coords_abs:
 * @self: a #ClutterPressGesture
 * @coords_out: (out): a #graphene_point_t
 *
 * Retrieves the coordinates of the press in absolute coordinates.
 */
void
clutter_press_gesture_get_coords_abs (ClutterPressGesture *self,
                                      graphene_point_t    *coords_out)
{
  g_return_if_fail (CLUTTER_IS_PRESS_GESTURE (self));
  g_return_if_fail (coords_out != NULL);

  clutter_gesture_get_point_begin_coords_abs (CLUTTER_GESTURE (self),
                                              0, coords_out);
}

/**
 * clutter_press_gesture_get_n_presses:
 * @self: a #ClutterPressGesture
 *
 * Retrieves the number of presses that happened on the gesture.
 *
 * Returns: The number of presses
 */
unsigned int
clutter_press_gesture_get_n_presses (ClutterPressGesture *self)
{
  ClutterPressGesturePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PRESS_GESTURE (self), 0);

  priv = clutter_press_gesture_get_instance_private (self);

  return priv->n_presses_happened;
}

/**
 * clutter_press_gesture_get_required_button:
 * @self: a #ClutterPressGesture
 *
 * Gets the mouse button required for the press gesture to recognize.
 *
 * Returns: The mouse button required to recognize
 */
unsigned int
clutter_press_gesture_get_required_button (ClutterPressGesture *self)
{
  ClutterPressGesturePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_PRESS_GESTURE (self), 0);

  priv = clutter_press_gesture_get_instance_private (self);

  return priv->required_button;
}

/**
 * clutter_press_gesture_set_required_button:
 * @self: a #ClutterPressGesture
 * @required_button: mouse button required for the gesture to recognize
 *
 * Sets the mouse button required for the press gesture to recognize.
 * Pass 0 to allow any button. Touch input is always handled as a press
 * of the primary button.
 */
void
clutter_press_gesture_set_required_button (ClutterPressGesture *self,
                                           unsigned int         required_button)
{
  ClutterPressGesturePrivate *priv;

  g_return_if_fail (CLUTTER_IS_PRESS_GESTURE (self));

  priv = clutter_press_gesture_get_instance_private (self);

  if (priv->required_button == required_button)
    return;

  priv->required_button = required_button;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_REQUIRED_BUTTON]);
}
