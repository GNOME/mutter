/*
 * Copyright (C) 2022 Jonas Dreßler <verdre@v0yd.nl>
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
 * ClutterClickGesture:
 *
 * A #ClutterPressGesture subclass for recognizing click gestures
 */

#include "clutter/clutter-click-gesture.h"

#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-marshal.h"

typedef struct _ClutterClickGesture ClutterClickGesture;

struct _ClutterClickGesture
{
  ClutterPressGesture parent;

  gboolean recognize_on_press;

  unsigned int n_clicks_required;
};

enum
{
  PROP_0,

  PROP_N_CLICKS_REQUIRED,
  PROP_RECOGNIZE_ON_PRESS,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };

G_DEFINE_FINAL_TYPE (ClutterClickGesture, clutter_click_gesture, CLUTTER_TYPE_PRESS_GESTURE)

static void
clutter_click_gesture_press (ClutterPressGesture *press_gesture)
{
  ClutterGesture *gesture = CLUTTER_GESTURE (press_gesture);
  ClutterClickGesture *self = CLUTTER_CLICK_GESTURE (press_gesture);

  if (self->recognize_on_press)
    clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_COMPLETED);

  if (self->n_clicks_required > 1 &&
      self->n_clicks_required == clutter_press_gesture_get_n_presses (press_gesture))
    clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_COMPLETED);
}

static void
clutter_click_gesture_release (ClutterPressGesture *press_gesture)
{
  ClutterGesture *gesture = CLUTTER_GESTURE (press_gesture);
  ClutterClickGesture *self = CLUTTER_CLICK_GESTURE (press_gesture);

  if (self->n_clicks_required == 1)
    {
      if (clutter_press_gesture_get_pressed (press_gesture))
        clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_COMPLETED);
      else
        clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_CANCELLED);
    }
}

static void
clutter_click_gesture_should_influence (ClutterGesture *gesture,
                                        ClutterGesture *other_gesture,
                                        gboolean       *cancel_on_recognizing)
{
  ClutterClickGesture *self = CLUTTER_CLICK_GESTURE (gesture);
  ClutterActor *actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (gesture));

  if (CLUTTER_IS_CLICK_GESTURE (other_gesture))
    {
      ClutterClickGesture *other = CLUTTER_CLICK_GESTURE (other_gesture);
      ClutterActor *other_actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (other_gesture));

      /* Make sure double-click gestures on the same actor as click gestures
       * behave as expected, that is:
       *
       * On first click the click recognizes
       * On second click the double click recognizes
       */
      if (actor == other_actor &&
          self->n_clicks_required < other->n_clicks_required)
        *cancel_on_recognizing = FALSE;
    }
}

static void
clutter_click_gesture_set_property (GObject      *gobject,
                                    unsigned int  prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  ClutterClickGesture *self = CLUTTER_CLICK_GESTURE (gobject);

  switch (prop_id)
    {
    case PROP_N_CLICKS_REQUIRED:
      clutter_click_gesture_set_n_clicks_required (self, g_value_get_uint (value));
      break;

    case PROP_RECOGNIZE_ON_PRESS:
      clutter_click_gesture_set_recognize_on_press (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_click_gesture_get_property (GObject      *gobject,
                                    unsigned int  prop_id,
                                    GValue       *value,
                                    GParamSpec   *pspec)
{
  ClutterClickGesture *self = CLUTTER_CLICK_GESTURE (gobject);

  switch (prop_id)
    {
    case PROP_N_CLICKS_REQUIRED:
      g_value_set_uint (value, clutter_click_gesture_get_n_clicks_required (self));
      break;

    case PROP_RECOGNIZE_ON_PRESS:
      g_value_set_boolean (value, clutter_click_gesture_get_recognize_on_press (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_click_gesture_class_init (ClutterClickGestureClass *klass)
{
  ClutterPressGestureClass *press_gesture_class = CLUTTER_PRESS_GESTURE_CLASS (klass);
  ClutterGestureClass *gesture_class = CLUTTER_GESTURE_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  press_gesture_class->press = clutter_click_gesture_press;
  press_gesture_class->release = clutter_click_gesture_release;

  gesture_class->should_influence = clutter_click_gesture_should_influence;

  gobject_class->set_property = clutter_click_gesture_set_property;
  gobject_class->get_property = clutter_click_gesture_get_property;

  /**
   * ClutterClickGesture:n-clicks-required:
   *
   * The number of clicks required for the gesture to recognize, this can
   * be used to implement double-click gestures.
   *
   * Note that for single clicks, the gesture will recognize on button-release,
   * while for double or more clicks, the gesture will recognize on
   * button-press.
   */
  obj_props[PROP_N_CLICKS_REQUIRED] =
    g_param_spec_uint ("n-clicks-required", NULL, NULL,
                       1, G_MAXUINT, 1,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterClickGesture:recognize-on-press:
   *
   * Set this to %TRUE to make the click gesture recognize on
   * button-press/touch-begin rather than on button-release/touch-end events.
   *
   * Defaults to %FALSE.
   */
  obj_props[PROP_RECOGNIZE_ON_PRESS] =
    g_param_spec_boolean ("recognize-on-press", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
}

static void
clutter_click_gesture_init (ClutterClickGesture *self)
{
  self->n_clicks_required = 1;
}

/**
 * clutter_click_gesture_new:
 *
 * Creates a new #ClutterClickGesture instance
 *
 * Returns: the newly created #ClutterClickGesture
 */
ClutterAction *
clutter_click_gesture_new (void)
{
  return g_object_new (CLUTTER_TYPE_CLICK_GESTURE, NULL);
}

/**
 * clutter_click_gesture_get_n_clicks_required:
 * @self: a #ClutterClickGesture
 *
 * Gets the number of clicks required for the click gesture to recognize.
 *
 * Returns: The number of clicks
 */
unsigned int
clutter_click_gesture_get_n_clicks_required (ClutterClickGesture *self)
{
  g_return_val_if_fail (CLUTTER_IS_CLICK_GESTURE (self), 1);

  return self->n_clicks_required;
}

/**
 * clutter_click_gesture_set_n_clicks_required:
 * @self: a #ClutterClickGesture
 * @n_clicks_required: the number of clicks required
 *
 * Sets the number of clicks required for the gesture to recognize, this can
 * be used to implement double-click gestures.
 *
 * See also #ClutterClickGesture:n-clicks-required.
 */
void
clutter_click_gesture_set_n_clicks_required (ClutterClickGesture *self,
                                             unsigned int         n_clicks_required)
{
  g_return_if_fail (CLUTTER_IS_CLICK_GESTURE (self));

  if (self->n_clicks_required == n_clicks_required)
    return;

  self->n_clicks_required = n_clicks_required;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_N_CLICKS_REQUIRED]);
}

/**
 * clutter_click_gesture_get_recognize_on_press:
 * @self: a #ClutterClickGesture
 *
 * Get whether the click gesture recognizes on button-press/touch-begin events.
 *
 * Returns: %TRUE when the gesture recognizes on press.
 */
gboolean
clutter_click_gesture_get_recognize_on_press (ClutterClickGesture *self)
{
  g_return_val_if_fail (CLUTTER_IS_CLICK_GESTURE (self), FALSE);

  return self->recognize_on_press;
}

/**
 * clutter_click_gesture_set_recognize_on_press:
 * @self: a #ClutterClickGesture
 * @recognize_on_press: whether to recognize on press
 *
 * Set this to %TRUE to make the click gesture recognize on
 * button-press/touch-begin rather than on button-release/touch-end events.
 *
 * See also #ClutterClickGesture:recognize-on-press.
 */
void
clutter_click_gesture_set_recognize_on_press (ClutterClickGesture *self,
                                              gboolean             recognize_on_press)
{
  g_return_if_fail (CLUTTER_IS_CLICK_GESTURE (self));

  if (self->recognize_on_press == recognize_on_press)
    return;

  self->recognize_on_press = recognize_on_press;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_RECOGNIZE_ON_PRESS]);
}
