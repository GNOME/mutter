#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include <clutter/clutter.h>

#include "clutter/clutter-mutter.h"

#include "tests/clutter-test-utils.h"

G_BEGIN_DECLS

#define TEST_TYPE_GESTURE test_gesture_get_type()

static
G_DECLARE_FINAL_TYPE (TestGesture, test_gesture, TEST, GESTURE, ClutterGesture)

struct _TestGesture
{
  ClutterGesture parent;
};

G_DEFINE_TYPE (TestGesture, test_gesture, CLUTTER_TYPE_GESTURE);

G_END_DECLS

static gboolean
test_gesture_should_handle_sequence (ClutterGesture     *self,
                                     const ClutterEvent *sequence_begin_event)
{
  if (clutter_event_type (sequence_begin_event) == CLUTTER_BUTTON_PRESS)
    return TRUE;

  return FALSE;
}

static void
test_gesture_init (TestGesture *self)
{
}

static void
test_gesture_class_init (TestGestureClass *klass)
{
  ClutterGestureClass *gesture_class = CLUTTER_GESTURE_CLASS (klass);

  gesture_class->should_handle_sequence = test_gesture_should_handle_sequence;
}

static void
gesture_changed_state_once (ClutterGesture      *gesture,
                            GParamSpec          *spec,
                            ClutterGestureState *state_ptr)
{
  *state_ptr = clutter_gesture_get_state (gesture);

  g_signal_handlers_disconnect_by_func (gesture, gesture_changed_state_once, state_ptr);
}

static void
on_after_update (ClutterStage     *stage,
                 ClutterStageView *view,
                 ClutterFrame     *frame,
                 gboolean         *was_updated)
{
  *was_updated = TRUE;
}

static void
wait_stage_updated (gboolean *was_updated)
{
  *was_updated = FALSE;

  clutter_test_flush_input ();

  while (!*was_updated)
    g_main_context_iteration (NULL, TRUE);
}

static void
gesture_disposed_while_active (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterActor *second_actor = clutter_actor_new ();
  ClutterGesture *gesture = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, NULL));
  gboolean was_updated;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  clutter_actor_set_size (second_actor, 20, 20);
  clutter_actor_set_x (second_actor, 15);
  clutter_actor_set_reactive (second_actor, true);
  clutter_actor_add_child (stage, second_actor);
  clutter_actor_add_action (second_actor, CLUTTER_ACTION (gesture));

  g_object_add_weak_pointer (G_OBJECT (gesture), (gpointer *) &gesture);

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);

  clutter_actor_show (stage);
  wait_stage_updated (&was_updated);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 15, 15);
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  wait_stage_updated (&was_updated);

  clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_RECOGNIZING);

  clutter_actor_destroy (second_actor);
  g_assert_null (gesture);
  wait_stage_updated (&was_updated);

  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  wait_stage_updated (&was_updated);

  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
gesture_state_machine_move_to_waiting (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterGesture *gesture = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, NULL));
  gboolean was_updated;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_WAITING);
  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture));
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_WAITING);

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);

  clutter_actor_show (stage);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 15, 15);
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 1);

  clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_CANCELLED);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_CANCELLED);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 1);

  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_SECONDARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_CANCELLED);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 1);

  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_CANCELLED);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 1);

  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_SECONDARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 0);

  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture));
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
gesture_state_machine_move_to_cancelled_while_possible (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterGesture *gesture = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, NULL));
  gboolean was_updated;
  ClutterGestureState gesture_state_change;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture));

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);

  clutter_actor_show (stage);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 15, 15);
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 1);

  g_signal_connect (gesture, "notify::state",
                    G_CALLBACK (gesture_changed_state_once),
                    &gesture_state_change);

  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  wait_stage_updated (&was_updated);

  g_assert_true (gesture_state_change == CLUTTER_GESTURE_STATE_CANCELLED);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 0);

  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture));
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
gesture_state_machine_move_to_cancelled_on_sequence_cancel (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterActor *second_actor = clutter_actor_new ();
  ClutterGesture *gesture = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, NULL));
  gboolean was_updated;
  ClutterGestureState gesture_state_change;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture));

  clutter_actor_set_size (second_actor, 20, 20);
  clutter_actor_set_reactive (second_actor, true);
  clutter_actor_add_child (stage, second_actor);

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);

  clutter_actor_show (stage);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 15, 15);
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 1);

  clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_RECOGNIZING);

  g_signal_connect (gesture, "notify::state",
                    G_CALLBACK (gesture_changed_state_once),
                    &gesture_state_change);

  /* Take a grab on the second_actor so that the sequence for the button press
   * gets cancelled.
   */
  clutter_stage_grab (CLUTTER_STAGE (stage), second_actor);
  g_assert_true (gesture_state_change == CLUTTER_GESTURE_STATE_CANCELLED);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 0);

  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  wait_stage_updated (&was_updated);

  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture));
  clutter_actor_destroy (second_actor);
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
gesture_multiple_mouse_buttons (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterGesture *gesture = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, NULL));
  gboolean was_updated;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture));

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);

  clutter_actor_show (stage);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 15, 15);
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 1);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 5, 5);
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_SECONDARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 1);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 15, 15);
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 1);

  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_SECONDARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture), ==, 0);

  clutter_gesture_set_state (gesture, CLUTTER_GESTURE_STATE_CANCELLED);
  g_assert_true (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_WAITING);

  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture));
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/gesture/disposed-while-active", gesture_disposed_while_active);
  CLUTTER_TEST_UNIT ("/gesture/state-machine-move-to-waiting", gesture_state_machine_move_to_waiting);
  CLUTTER_TEST_UNIT ("/gesture/state-machine-move-to-cancelled-while-possible", gesture_state_machine_move_to_cancelled_while_possible);
  CLUTTER_TEST_UNIT ("/gesture/state-machine-move-to-cancelled-on-sequence-cancel", gesture_state_machine_move_to_cancelled_on_sequence_cancel);
  CLUTTER_TEST_UNIT ("/gesture/multiple-mouse-buttons", gesture_multiple_mouse_buttons);
)
