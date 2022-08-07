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
  return TRUE;
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
move_to_waiting_on_complete (ClutterGesture *gesture,
                             GParamSpec     *spec)
{
  if (clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_CANCELLED ||
      clutter_gesture_get_state (gesture) == CLUTTER_GESTURE_STATE_COMPLETED)
    clutter_gesture_reset_state_machine (gesture);
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
gesture_relationship_freed_despite_relationship (void)
{
  ClutterAction *action_1 = CLUTTER_ACTION (g_object_new (TEST_TYPE_GESTURE, NULL));
  ClutterAction *action_2 = CLUTTER_ACTION (g_object_new (TEST_TYPE_GESTURE, NULL));

  g_object_add_weak_pointer (G_OBJECT (action_1), (gpointer *) &action_1);
  g_object_add_weak_pointer (G_OBJECT (action_2), (gpointer *) &action_2);

  clutter_gesture_can_not_cancel (CLUTTER_GESTURE (action_1),
                                  CLUTTER_GESTURE (action_2));

  g_object_unref (action_2);
  g_assert_null (action_2);

  g_object_unref (action_1);
  g_assert_null (action_1);
}

static void
gesture_relationship_cancel_on_recognize (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterGesture *gesture_1 = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, "name", "gesture-1", NULL));
  ClutterGesture *gesture_2 = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, "name", "gesture-2", NULL));
  gboolean was_updated;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture_1));
  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture_2));

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);

  clutter_actor_show (stage);
  wait_stage_updated (&was_updated);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 15, 15);
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_POSSIBLE);

  clutter_gesture_set_state (gesture_1, CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_CANCELLED);

  clutter_gesture_set_state (gesture_1, CLUTTER_GESTURE_STATE_COMPLETED);

  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);

  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture_1));
  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture_2));
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
on_gesture_signal (ClutterGesture *gesture,
                   gboolean       *was_emitted)
{
  *was_emitted = TRUE;

  g_signal_handlers_disconnect_by_func (gesture, on_gesture_signal, was_emitted);
}

static void
gesture_relationship_simple (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterGesture *gesture_1 = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, "name", "gesture-1", NULL));
  ClutterGesture *gesture_2 = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, "name", "gesture-2", NULL));
  gboolean was_updated;
  gboolean recognize_emitted, cancel_emitted;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture_1));
  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture_2));

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);

  clutter_actor_show (stage);
  wait_stage_updated (&was_updated);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 15, 15);
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_POSSIBLE);

  recognize_emitted = cancel_emitted = FALSE;

  g_signal_connect (gesture_2, "recognize",
                    G_CALLBACK (on_gesture_signal),
                    &recognize_emitted);
  g_signal_connect (gesture_1, "cancel",
                    G_CALLBACK (on_gesture_signal),
                    &cancel_emitted);

  clutter_gesture_set_state (gesture_2, CLUTTER_GESTURE_STATE_COMPLETED);
  g_assert_true (recognize_emitted);
  g_assert_true (!cancel_emitted);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_CANCELLED);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_COMPLETED);

  g_signal_handlers_disconnect_by_func (gesture_1, on_gesture_signal, &cancel_emitted);

  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);

  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture_1));
  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture_2));
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
gesture_relationship_two_points (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterGesture *gesture_1 = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, "name", "gesture-1", NULL));
  ClutterGesture *gesture_2 = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, "name", "gesture-2", NULL));
  gboolean was_updated;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture_1));
  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture_2));

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);

  clutter_actor_show (stage);
  wait_stage_updated (&was_updated);

  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 0, 15, 15);
  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 1, 15, 20);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_POSSIBLE);

  clutter_gesture_set_state (gesture_1, CLUTTER_GESTURE_STATE_COMPLETED);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_COMPLETED);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_CANCELLED);

  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 1);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_COMPLETED);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_CANCELLED);

  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 0);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);

  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture_1));
  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture_2));
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
gesture_relationship_two_points_two_actors (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterActor *second_actor = clutter_actor_new ();
  ClutterGesture *gesture_1 = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, "name", "gesture-1", NULL));
  ClutterGesture *gesture_2 = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, "name", "gesture-2", NULL));
  gboolean was_updated;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  clutter_actor_set_size (second_actor, 20, 20);
  clutter_actor_set_reactive (second_actor, true);
  clutter_actor_add_child (stage, second_actor);

  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture_1));
  clutter_actor_add_action (second_actor, CLUTTER_ACTION (gesture_2));

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);

  clutter_actor_show (stage);
  wait_stage_updated (&was_updated);

  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 0, 15, 15);
  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 1, 15, 50);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_POSSIBLE);

  clutter_gesture_set_state (gesture_1, CLUTTER_GESTURE_STATE_COMPLETED);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_COMPLETED);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_CANCELLED);

  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 0);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_COMPLETED);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);

  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 0, 15, 15);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_COMPLETED);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_POSSIBLE);

  clutter_gesture_set_state (gesture_2, CLUTTER_GESTURE_STATE_COMPLETED);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_COMPLETED);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_COMPLETED);

  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 0);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_COMPLETED);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);

  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 1);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);

  clutter_actor_destroy (second_actor);

  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture_1));
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
gesture_relationship_claim_new_sequence_while_already_recognizing (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterGesture *gesture_1 = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, "name", "gesture-1", NULL));
  ClutterGesture *gesture_2 =
    CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE,
                                   "name", "gesture-2",
                                   NULL));
  gboolean was_updated;
  ClutterGestureState gesture_2_state_change;

  g_signal_connect (gesture_2, "notify::state",
                    G_CALLBACK (move_to_waiting_on_complete),
                    NULL);

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture_1));
  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture_2));

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);

  clutter_actor_show (stage);
  wait_stage_updated (&was_updated);

  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 0, 15, 15);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_1), ==, 1);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_2), ==, 1);

  clutter_gesture_set_state (gesture_1, CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_2), ==, 0);

  g_signal_connect (gesture_2, "notify::state",
                    G_CALLBACK (gesture_changed_state_once),
                    &gesture_2_state_change);

  /* With move_to_waiting_on_complete, gesture_2 should move into POSSIBLE, then
   * gesture_1 claims the new point and that should cancel gesture_2,
   * moving it to CANCELLED then WAITING immediately.
   */
  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 1, 45, 0);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (gesture_2_state_change == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_1), ==, 2);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_2), ==, 0);

  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 1);
  wait_stage_updated (&was_updated);

  g_signal_handlers_disconnect_by_func (gesture_2, move_to_waiting_on_complete, NULL);
  g_signal_connect (gesture_2, "notify::state",
                    G_CALLBACK (gesture_changed_state_once),
                    &gesture_2_state_change);

  /* Repeat without move_to_waiting_on_complete, same things happen at first but
   * gesture_2 stays in CANCELLED and waits until the point is removed.
   */
  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 1, 45, 0);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (gesture_2_state_change == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_CANCELLED);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_1), ==, 2);
  /* gesture_2 reports n_points = 0, not 1, because it got cancelled so quickly
   * that points_added() never got emitted.
   */
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_2), ==, 0);

  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 1);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);

  clutter_gesture_set_state (gesture_1, CLUTTER_GESTURE_STATE_COMPLETED);
  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 0);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);

  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture_1));
  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture_2));
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
gesture_relationship_claim_new_sequence_while_already_recognizing_2 (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterActor *second_actor = clutter_actor_new ();
  ClutterGesture *gesture_1 = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, "name", "gesture-1", NULL));
  ClutterGesture *gesture_2 = CLUTTER_GESTURE (g_object_new (TEST_TYPE_GESTURE, "name", "gesture-2", NULL));
  gboolean was_updated;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  clutter_actor_add_action (stage, CLUTTER_ACTION (gesture_1));

  clutter_actor_set_size (second_actor, 20, 20);
  clutter_actor_set_reactive (second_actor, true);
  clutter_actor_add_child (stage, second_actor);
  clutter_actor_add_action (second_actor, CLUTTER_ACTION (gesture_2));

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);

  clutter_actor_show (stage);
  wait_stage_updated (&was_updated);

  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 0, 25, 25);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_1), ==, 1);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_2), ==, 0);

  clutter_gesture_set_state (gesture_1, CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_1), ==, 1);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_2), ==, 0);

  /* Allow both gesture to share a sequence, now gesture_1 shouldn't try to claim
   * the new sequence and gesture_2 should recognize just fine.
   */
  clutter_gesture_can_not_cancel (gesture_1, gesture_2);
  clutter_gesture_can_not_cancel (gesture_2, gesture_1);

  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 1, 15, 15);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_POSSIBLE);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_1), ==, 2);
  g_assert_cmpint (clutter_gesture_get_n_points (gesture_2), ==, 1);

  clutter_gesture_set_state (gesture_2, CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_RECOGNIZING);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_RECOGNIZING);

  clutter_gesture_set_state (gesture_1, CLUTTER_GESTURE_STATE_COMPLETED);
  clutter_gesture_set_state (gesture_2, CLUTTER_GESTURE_STATE_COMPLETED);
  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 1);
  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 0);
  wait_stage_updated (&was_updated);
  g_assert_true (clutter_gesture_get_state (gesture_1) == CLUTTER_GESTURE_STATE_WAITING);
  g_assert_true (clutter_gesture_get_state (gesture_2) == CLUTTER_GESTURE_STATE_WAITING);

  clutter_actor_remove_action (stage, CLUTTER_ACTION (gesture_1));
  clutter_actor_destroy (second_actor);
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/gesture/relationship/freed-despite-relationship", gesture_relationship_freed_despite_relationship);
  CLUTTER_TEST_UNIT ("/gesture/relationship/cancel-on-recognize", gesture_relationship_cancel_on_recognize);
  CLUTTER_TEST_UNIT ("/gesture/relationship/simple", gesture_relationship_simple);
  CLUTTER_TEST_UNIT ("/gesture/relationship/two-points", gesture_relationship_two_points);
  CLUTTER_TEST_UNIT ("/gesture/relationship/two-points-two-actors", gesture_relationship_two_points_two_actors);
  CLUTTER_TEST_UNIT ("/gesture/relationship/claim-new-sequence-while-already-recognizing", gesture_relationship_claim_new_sequence_while_already_recognizing);
  CLUTTER_TEST_UNIT ("/gesture/relationship/claim-new-sequence-while-already-recognizing-2", gesture_relationship_claim_new_sequence_while_already_recognizing_2);
)
