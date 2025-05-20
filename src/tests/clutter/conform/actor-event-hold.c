/*
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
 * Author: José Expósito <jose.exposito89@gmail.com>
 */
#include <stdlib.h>
#include <string.h>

#include <clutter/clutter-mutter.h>
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

#define EVENT_TIME 1000

typedef struct {
  ClutterTouchpadGesturePhase phase;
  guint n_fingers;
  gfloat x;
  gfloat y;
} HoldTestCase;

static const HoldTestCase test_cases[] = {
  {
    .phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN,
    .n_fingers = 1,
    .x = 100,
    .y = 150,
  },
  {
    .phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_END,
    .n_fingers = 2,
    .x = 200,
    .y = 250,
  },
  {
    .phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL,
    .n_fingers = 3,
    .x = 300,
    .y = 350,
  },
};

static gboolean
on_stage_captured_event (ClutterActor  *stage,
                         ClutterEvent  *event,
                         ClutterEvent **captured_event)
{
  *captured_event = clutter_event_copy (event);
  return TRUE;
}

static void
actor_event_hold (void)
{
  ClutterActor *stage;
  ClutterContext *context;
  ClutterBackend *backend;
  ClutterSeat *seat;
  ClutterSprite *sprite;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterEvent *event;
  ClutterEvent *captured_event;
  size_t n_test_case;

  /* Get the stage and listen for touchpad events */
  stage = clutter_test_get_stage ();
  g_signal_connect (stage, "captured-event::touchpad",
                    G_CALLBACK (on_stage_captured_event),
                    &captured_event);
  clutter_actor_show (stage);

  /* Get the input device*/
  seat = clutter_test_get_default_seat ();

  virtual_pointer =
    clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       now_us,
                                                       1.0, 1.0);

  context = clutter_actor_get_context (stage);
  backend = clutter_context_get_backend (context);

  sprite = clutter_backend_get_pointer_sprite (backend, CLUTTER_STAGE (stage));

  while (clutter_focus_get_current_actor (CLUTTER_FOCUS (sprite)) == NULL)
    g_main_context_iteration (NULL, FALSE);

  for (n_test_case = 0; n_test_case < G_N_ELEMENTS (test_cases); n_test_case++)
    {
      graphene_point_t actual_position;
      gdouble *actual_axes;
      ClutterTouchpadGesturePhase actual_phase;
      guint actual_n_fingers;
      gdouble dx, dy, udx, udy;

      const HoldTestCase *test_case = test_cases + n_test_case;

      /* Create a synthetic hold event */
      event = clutter_event_touchpad_hold_new (CLUTTER_EVENT_NONE,
                                               EVENT_TIME,
                                               clutter_seat_get_pointer (seat),
                                               test_case->phase,
                                               test_case->n_fingers,
                                               GRAPHENE_POINT_INIT (test_case->x,
                                                                    test_case->y));
      clutter_event_put (event);
      clutter_event_free (event);

      /* Capture the event received by the stage */
      captured_event = NULL;
      while (captured_event == NULL)
        g_main_context_iteration (NULL, FALSE);

      /* Check that expected the event params match the actual values */
      clutter_event_get_position (captured_event, &actual_position);
      actual_axes = clutter_event_get_axes (captured_event, 0);
      actual_phase = clutter_event_get_gesture_phase (captured_event);
      actual_n_fingers = clutter_event_get_touchpad_gesture_finger_count (captured_event);
      clutter_event_get_gesture_motion_delta (captured_event, &dx, &dy);
      clutter_event_get_gesture_motion_delta_unaccelerated (captured_event, &udx, &udy);

      g_assert_cmpfloat (actual_position.x, ==, test_case->x);
      g_assert_cmpfloat (actual_position.y, ==, test_case->y);
      g_assert_null (actual_axes);
      g_assert_cmpint (actual_phase, ==, test_case->phase);
      g_assert_cmpint (actual_n_fingers, ==, test_case->n_fingers);
      g_assert_cmpfloat (dx, ==, 0);
      g_assert_cmpfloat (dy, ==, 0);
      g_assert_cmpfloat (udx, ==, 0);
      g_assert_cmpfloat (udy, ==, 0);

      clutter_event_free (captured_event);
    }
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/event/hold", actor_event_hold)
)
