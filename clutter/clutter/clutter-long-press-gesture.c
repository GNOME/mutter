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
 * ClutterLongPressGesture:
 *
 * A #ClutterPressGesture subclass for recognizing long-press gestures
 */

#include "clutter/clutter-long-press-gesture.h"

#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-private.h"

struct _ClutterLongPressGesture
{
  ClutterPressGesture parent_instance;
};

G_DEFINE_FINAL_TYPE (ClutterLongPressGesture, clutter_long_press_gesture, CLUTTER_TYPE_PRESS_GESTURE)

static void
clutter_long_press_gesture_long_press (ClutterPressGesture *press_gesture)
{
  clutter_gesture_set_state (CLUTTER_GESTURE (press_gesture),
                             CLUTTER_GESTURE_STATE_RECOGNIZING);
}

static void
clutter_long_press_gesture_release (ClutterPressGesture *press_gesture)
{
  ClutterGesture *gesture = CLUTTER_GESTURE (press_gesture);

  if (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_RECOGNIZING)
    clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_COMPLETED);
  else
    clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_CANCELLED);
}

static void
clutter_long_press_gesture_class_init (ClutterLongPressGestureClass *klass)
{
  ClutterPressGestureClass *press_gesture_class = CLUTTER_PRESS_GESTURE_CLASS (klass);

  press_gesture_class->long_press = clutter_long_press_gesture_long_press;
  press_gesture_class->release = clutter_long_press_gesture_release;
}

static void
clutter_long_press_gesture_init (ClutterLongPressGesture *self)
{
}

/**
 * clutter_long_press_gesture_new:
 *
 * Creates a new #ClutterLongPressGesture instance
 *
 * Returns: the newly created #ClutterLongPressGesture
 */
ClutterAction *
clutter_long_press_gesture_new (void)
{
  return g_object_new (CLUTTER_TYPE_LONG_PRESS_GESTURE, NULL);
}
