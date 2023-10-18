#include <clutter/clutter.h>

#include "clutter/clutter-mutter.h"

#include "tests/clutter-test-utils.h"

static void
on_after_update (ClutterStage     *stage,
                 ClutterStageView *view,
                 ClutterFrame     *frame,
                 gboolean         *was_updated)
{
  *was_updated = TRUE;
}

static gboolean
on_event_return_stop (ClutterActor *actor,
                      ClutterEvent *event,
                      unsigned int *n_events)
{
  (*n_events)++;

  return CLUTTER_EVENT_STOP;
}

static gboolean
on_event_return_propagate (ClutterActor *actor,
                           ClutterEvent *event,
                           unsigned int *n_events)
{
  (*n_events)++;

  return CLUTTER_EVENT_PROPAGATE;
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
event_delivery_consecutive_touch_begin_end (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  gboolean was_updated;
  unsigned int n_captured_touch_events = 0;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);
  g_signal_connect (stage, "captured-event::touch", G_CALLBACK (on_event_return_stop),
                    &n_captured_touch_events);

  clutter_actor_show (stage);

  was_updated = FALSE;
  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 0, 5, 5);
  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 0);
  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 0, 5, 5);
  g_assert_true (!was_updated);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_captured_touch_events, ==, 3);

  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 0);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_captured_touch_events, ==, 4);

  g_signal_handlers_disconnect_by_func (stage, on_event_return_stop, &n_captured_touch_events);
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static unsigned int n_action_motion_events = 0;
static unsigned int n_action_touch_events = 0;
static unsigned int n_action_sequences_cancelled = 0;
static gboolean action_claim_sequence = FALSE;
static gboolean action_handle_event_retval = CLUTTER_EVENT_PROPAGATE;

G_BEGIN_DECLS

#define TEST_TYPE_ACTION (test_action_get_type ())

G_DECLARE_DERIVABLE_TYPE (TestAction, test_action,
                          TEST, ACTION, ClutterAction)

struct _TestActionClass
{
  ClutterActionClass parent_class;
};

G_DEFINE_TYPE (TestAction, test_action, CLUTTER_TYPE_ACTION);

G_END_DECLS

static gboolean
test_action_handle_event (ClutterAction      *action,
                          const ClutterEvent *event)
{
  ClutterEventType type = clutter_event_type (event);

  if (type == CLUTTER_MOTION)
    n_action_motion_events++;

  if (type == CLUTTER_TOUCH_BEGIN ||
      type == CLUTTER_TOUCH_UPDATE ||
      type == CLUTTER_TOUCH_END ||
      type == CLUTTER_TOUCH_CANCEL)
    {
      n_action_touch_events++;

      if (action_claim_sequence)
        {
          ClutterActor *actor;

          actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));
          clutter_stage_notify_action_implicit_grab (CLUTTER_STAGE (clutter_actor_get_stage (actor)),
                                                     clutter_event_get_device (event),
                                                     clutter_event_get_event_sequence (event));
        }
    }

  return action_handle_event_retval;
}

static void
test_action_sequence_cancelled (ClutterAction        *action,
                                ClutterInputDevice   *device,
                                ClutterEventSequence *sequence)
{
  n_action_sequences_cancelled++;
}

static void
test_action_class_init (TestActionClass *klass)
{
  ClutterActionClass *action_class = CLUTTER_ACTION_CLASS (klass);

  action_class->handle_event = test_action_handle_event;
  action_class->sequence_cancelled = test_action_sequence_cancelled;
}

static void
test_action_init (TestAction *self)
{
}

static void
event_delivery_implicit_grabbing (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterActor *child;
  gboolean was_updated;
  unsigned int n_child_motion_events, n_stage_motion_events;
  unsigned int n_child_button_events, n_stage_button_events;
  unsigned int n_child_enter_events, n_stage_enter_events;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  child = clutter_actor_new ();
  clutter_actor_set_reactive (child, TRUE);
  clutter_actor_set_position (child, 20, 0);
  clutter_actor_set_size (child, 20, 20);
  clutter_actor_add_child (stage, child);

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);
  g_signal_connect (stage, "event::motion", G_CALLBACK (on_event_return_propagate),
                    &n_stage_motion_events);
  g_signal_connect (stage, "event::button", G_CALLBACK (on_event_return_propagate),
                    &n_stage_button_events);
  g_signal_connect (child, "event::motion", G_CALLBACK (on_event_return_propagate),
                    &n_child_motion_events);
  g_signal_connect (child, "event::button", G_CALLBACK (on_event_return_propagate),
                    &n_child_button_events);

  clutter_actor_show (stage);
  wait_stage_updated (&was_updated);

  n_stage_motion_events = n_child_motion_events = 0;
  n_stage_button_events = n_child_button_events = 0;
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 1, 1);
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_motion_events, ==, 0);
  g_assert_cmpint (n_child_button_events, ==, 0);
  g_assert_cmpint (n_stage_motion_events, ==, 1);
  g_assert_cmpint (n_stage_button_events, ==, 1);

  n_stage_motion_events = n_child_motion_events = 0;
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 30, 1);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_motion_events, ==, 0);
  g_assert_cmpint (n_stage_motion_events, ==, 1);

  /* After the implicit grab ends, the new actor under cursor should receive a
   * GRAB_NOTIFY ENTER event.
   */
  g_signal_connect (stage, "enter-event", G_CALLBACK (on_event_return_propagate),
                    &n_stage_enter_events);
  g_signal_connect (child, "enter-event", G_CALLBACK (on_event_return_propagate),
                    &n_child_enter_events);

  n_stage_button_events = n_child_button_events = 0;
  n_stage_enter_events = n_child_enter_events = 0;
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_button_events, ==, 0);
  g_assert_cmpint (n_child_enter_events, ==, 1);
  g_assert_cmpint (n_stage_button_events, ==, 1);
  g_assert_cmpint (n_stage_enter_events, ==, 0);

  g_signal_handlers_disconnect_by_func (child, on_event_return_propagate, &n_child_enter_events);
  g_signal_handlers_disconnect_by_func (stage, on_event_return_propagate, &n_stage_enter_events);

  n_stage_motion_events = n_child_motion_events = 0;
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 30, 1);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_motion_events, ==, 1);
  g_assert_cmpint (n_stage_motion_events, ==, 1);

  g_signal_handlers_disconnect_by_func (child, on_event_return_propagate, &n_child_button_events);
  g_signal_handlers_disconnect_by_func (child, on_event_return_propagate, &n_child_motion_events);
  clutter_actor_destroy (child);
  g_signal_handlers_disconnect_by_func (stage, on_event_return_propagate, &n_stage_button_events);
  g_signal_handlers_disconnect_by_func (stage, on_event_return_propagate, &n_stage_motion_events);
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
event_delivery_implicit_grab_cancelled (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterActor *child_1, *child_2;
  ClutterAction *action_1;
  gboolean was_updated;
  unsigned int n_child_1_button_events, n_child_2_button_events, n_stage_button_events;
  unsigned int n_child_1_enter_events, n_child_2_enter_events, n_stage_enter_events;
  unsigned int n_child_1_leave_events, n_child_2_leave_events, n_stage_leave_events;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  child_1 = clutter_actor_new ();
  action_1 = g_object_new (TEST_TYPE_ACTION, NULL);
  action_handle_event_retval = CLUTTER_EVENT_PROPAGATE;
  clutter_actor_add_action (child_1, CLUTTER_ACTION (action_1));
  clutter_actor_set_reactive (child_1, TRUE);
  clutter_actor_set_size (child_1, 20, 20);
  clutter_actor_add_child (stage, child_1);

  child_2 = clutter_actor_new ();
  clutter_actor_set_reactive (child_2, TRUE);
  clutter_actor_set_position (child_2, 30, 0);
  clutter_actor_set_size (child_2, 20, 20);
  clutter_actor_add_child (stage, child_2);

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);
  g_signal_connect (stage, "event::button", G_CALLBACK (on_event_return_propagate),
                    &n_stage_button_events);
  g_signal_connect (child_1, "event::button", G_CALLBACK (on_event_return_propagate),
                    &n_child_1_button_events);
  g_signal_connect (child_2, "event::button", G_CALLBACK (on_event_return_propagate),
                    &n_child_2_button_events);
  g_signal_connect (stage, "enter-event", G_CALLBACK (on_event_return_propagate),
                    &n_stage_enter_events);
  g_signal_connect (child_1, "enter-event", G_CALLBACK (on_event_return_propagate),
                    &n_child_1_enter_events);
  g_signal_connect (child_2, "enter-event", G_CALLBACK (on_event_return_propagate),
                    &n_child_2_enter_events);
  g_signal_connect (stage, "leave-event", G_CALLBACK (on_event_return_propagate),
                    &n_stage_leave_events);
  g_signal_connect (child_1, "leave-event", G_CALLBACK (on_event_return_propagate),
                    &n_child_1_leave_events);
  g_signal_connect (child_2, "leave-event", G_CALLBACK (on_event_return_propagate),
                    &n_child_2_leave_events);

  clutter_actor_show (stage);
  wait_stage_updated (&was_updated);

  n_child_1_button_events = n_child_2_button_events = n_stage_button_events = 0;
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 1, 1);
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_1_button_events, ==, 1);
  g_assert_cmpint (n_child_2_button_events, ==, 0);
  g_assert_cmpint (n_stage_button_events, ==, 1);

  n_child_1_enter_events = n_child_2_enter_events = n_stage_enter_events = 0;
  n_child_1_leave_events = n_child_2_leave_events = n_stage_leave_events = 0;
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 32, 1);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_1_enter_events, ==, 1);
  g_assert_cmpint (n_child_1_leave_events, ==, 1);
  g_assert_cmpint (n_child_2_enter_events, ==, 0);
  g_assert_cmpint (n_child_2_leave_events, ==, 0);
  g_assert_cmpint (n_stage_enter_events, ==, 1);
  g_assert_cmpint (n_stage_leave_events, ==, 1);

  /* Destroying child_1 should not cancel the grab, instead the grab should still
   * be in effect on the parent (so the stage) now.
   */
  n_child_1_enter_events = n_child_2_enter_events = n_stage_enter_events = 0;
  n_child_1_leave_events = n_child_2_leave_events = n_stage_leave_events = 0;
  n_action_sequences_cancelled = 0;
  clutter_actor_destroy (child_1);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_1_enter_events, ==, 0);
  g_assert_cmpint (n_child_1_leave_events, ==, 0);
  g_assert_cmpint (n_action_sequences_cancelled, ==, 1);
  g_assert_cmpint (n_child_2_enter_events, ==, 0);
  g_assert_cmpint (n_child_2_leave_events, ==, 0);
  g_assert_cmpint (n_stage_enter_events, ==, 0);
  g_assert_cmpint (n_stage_leave_events, ==, 0);

  n_child_2_enter_events = n_stage_enter_events = 0;
  n_child_2_leave_events = n_stage_leave_events = 0;
  n_child_2_button_events = n_stage_button_events = 0;
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_2_button_events, ==, 0);
  g_assert_cmpint (n_child_2_enter_events, ==, 1);
  g_assert_cmpint (n_child_2_leave_events, ==, 0);
  g_assert_cmpint (n_stage_button_events, ==, 1);

  g_signal_handlers_disconnect_by_func (child_2, on_event_return_propagate, &n_child_2_leave_events);
  g_signal_handlers_disconnect_by_func (stage, on_event_return_propagate, &n_stage_leave_events);
  g_signal_handlers_disconnect_by_func (child_2, on_event_return_propagate, &n_child_2_enter_events);
  g_signal_handlers_disconnect_by_func (stage, on_event_return_propagate, &n_stage_enter_events);

  g_signal_handlers_disconnect_by_func (child_2, on_event_return_propagate, &n_child_2_button_events);
  g_signal_handlers_disconnect_by_func (stage, on_event_return_propagate, &n_stage_button_events);
  clutter_actor_destroy (child_2);

  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
event_delivery_implicit_grab_existing_clutter_grab (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  ClutterActor *child_1, *child_2;
  ClutterAction *action_1;
  gboolean was_updated;
  unsigned int n_child_1_button_events, n_child_2_button_events, n_stage_button_events;
  unsigned int n_child_1_motion_events, n_child_2_motion_events, n_stage_motion_events;
  unsigned int n_child_1_enter_events, n_child_2_enter_events, n_stage_enter_events;
  unsigned int n_child_1_leave_events, n_child_2_leave_events, n_stage_leave_events;
  ClutterGrab *grab_1, *grab_2;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  child_1 = clutter_actor_new ();
  action_1 = g_object_new (TEST_TYPE_ACTION, NULL);
  action_handle_event_retval = CLUTTER_EVENT_PROPAGATE;
  clutter_actor_add_action (child_1, CLUTTER_ACTION (action_1));
  clutter_actor_set_reactive (child_1, TRUE);
  clutter_actor_set_size (child_1, 20, 20);
  clutter_actor_add_child (stage, child_1);

  child_2 = clutter_actor_new ();
  clutter_actor_set_reactive (child_2, TRUE);
  clutter_actor_set_position (child_2, 30, 0);
  clutter_actor_set_size (child_2, 20, 20);
  clutter_actor_add_child (stage, child_2);

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);
  g_signal_connect (stage, "event::button", G_CALLBACK (on_event_return_propagate),
                    &n_stage_button_events);
  g_signal_connect (child_1, "event::button", G_CALLBACK (on_event_return_propagate),
                    &n_child_1_button_events);
  g_signal_connect (child_2, "event::button", G_CALLBACK (on_event_return_propagate),
                    &n_child_2_button_events);
  g_signal_connect (stage, "event::motion", G_CALLBACK (on_event_return_propagate),
                    &n_stage_motion_events);
  g_signal_connect (child_1, "event::motion", G_CALLBACK (on_event_return_propagate),
                    &n_child_1_motion_events);
  g_signal_connect (child_2, "event::motion", G_CALLBACK (on_event_return_propagate),
                    &n_child_2_motion_events);
  g_signal_connect (stage, "enter-event", G_CALLBACK (on_event_return_propagate),
                    &n_stage_enter_events);
  g_signal_connect (child_1, "enter-event", G_CALLBACK (on_event_return_propagate),
                    &n_child_1_enter_events);
  g_signal_connect (child_2, "enter-event", G_CALLBACK (on_event_return_propagate),
                    &n_child_2_enter_events);
  g_signal_connect (stage, "leave-event", G_CALLBACK (on_event_return_propagate),
                    &n_stage_leave_events);
  g_signal_connect (child_1, "leave-event", G_CALLBACK (on_event_return_propagate),
                    &n_child_1_leave_events);
  g_signal_connect (child_2, "leave-event", G_CALLBACK (on_event_return_propagate),
                    &n_child_2_leave_events);

  clutter_actor_show (stage);
  wait_stage_updated (&was_updated);

  n_child_1_button_events = n_child_2_button_events = n_stage_button_events = 0;
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 1, 1);
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_1_button_events, ==, 1);
  g_assert_cmpint (n_child_2_button_events, ==, 0);
  g_assert_cmpint (n_stage_button_events, ==, 1);

  /* The ClutterGrab on child_1 (while that same actor is implicitly grabbed)
   * should cause us to keep the implicit grab intact but send actors outside (so
   * the stage) a LEAVE event.
   */
  n_child_1_enter_events = n_child_2_enter_events = n_stage_enter_events = 0;
  n_child_1_leave_events = n_child_2_leave_events = n_stage_leave_events = 0;
  n_action_sequences_cancelled = 0;
  grab_1 = clutter_stage_grab (CLUTTER_STAGE (stage), child_1);
  g_assert_cmpint (n_child_1_enter_events, ==, 0);
  g_assert_cmpint (n_child_1_leave_events, ==, 0);
  g_assert_cmpint (n_action_sequences_cancelled, ==, 0);
  g_assert_cmpint (n_child_2_enter_events, ==, 0);
  g_assert_cmpint (n_child_2_leave_events, ==, 0);
  g_assert_cmpint (n_stage_enter_events, ==, 0);
  g_assert_cmpint (n_stage_leave_events, ==, 1);

  /* Implicit grab is still there, but only on child_1 now */
  n_child_1_motion_events = n_child_2_motion_events = n_stage_motion_events = 0;
  n_action_motion_events = 0;
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 31, 1);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_1_motion_events, ==, 1);
  g_assert_cmpint (n_action_motion_events, ==, 1);
  g_assert_cmpint (n_child_2_motion_events, ==, 0);
  g_assert_cmpint (n_stage_motion_events, ==, 0);

  /* Push another ClutterGrab, this time on child_2. This will now cancel the
   * implicit one.
   */
  n_child_1_enter_events = n_child_2_enter_events = n_stage_enter_events = 0;
  n_child_1_leave_events = n_child_2_leave_events = n_stage_leave_events = 0;
  n_action_sequences_cancelled = 0;
  grab_2 = clutter_stage_grab (CLUTTER_STAGE (stage), child_2);
  g_assert_cmpint (n_child_1_enter_events, ==, 0);
  g_assert_cmpint (n_child_1_leave_events, ==, 0);
  g_assert_cmpint (n_action_sequences_cancelled, ==, 1);
  g_assert_cmpint (n_child_2_enter_events, ==, 1);
  g_assert_cmpint (n_child_2_leave_events, ==, 0);
  g_assert_cmpint (n_stage_enter_events, ==, 0);
  g_assert_cmpint (n_stage_leave_events, ==, 0);

  n_child_1_motion_events = n_child_2_motion_events = n_stage_motion_events = 0;
  n_action_motion_events = 0;
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 1, 1);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_1_motion_events, ==, 0);
  g_assert_cmpint (n_action_motion_events, ==, 0);
  g_assert_cmpint (n_child_2_motion_events, ==, 1);
  g_assert_cmpint (n_stage_motion_events, ==, 0);

  n_child_1_enter_events = n_child_2_enter_events = n_stage_enter_events = 0;
  n_child_1_leave_events = n_child_2_leave_events = n_stage_leave_events = 0;
  clutter_grab_dismiss (grab_2);
  g_assert_cmpint (n_child_1_enter_events, ==, 1);
  g_assert_cmpint (n_child_1_leave_events, ==, 0);
  g_assert_cmpint (n_child_2_enter_events, ==, 0);
  g_assert_cmpint (n_child_2_leave_events, ==, 0);
  g_assert_cmpint (n_stage_enter_events, ==, 0);
  g_assert_cmpint (n_stage_leave_events, ==, 0);

  n_child_1_enter_events = n_child_2_enter_events = n_stage_enter_events = 0;
  n_child_1_leave_events = n_child_2_leave_events = n_stage_leave_events = 0;
  clutter_grab_dismiss (grab_1);
  g_assert_cmpint (n_child_1_enter_events, ==, 0);
  g_assert_cmpint (n_child_1_leave_events, ==, 0);
  g_assert_cmpint (n_child_2_enter_events, ==, 0);
  g_assert_cmpint (n_child_2_leave_events, ==, 0);
  g_assert_cmpint (n_stage_enter_events, ==, 1);
  g_assert_cmpint (n_stage_leave_events, ==, 0);

  n_child_1_button_events = n_child_2_button_events = n_stage_button_events = 0;
  clutter_virtual_input_device_notify_button (virtual_pointer, now_us,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_1_button_events, ==, 1);
  g_assert_cmpint (n_child_2_button_events, ==, 0);
  g_assert_cmpint (n_stage_button_events, ==, 1);

  g_signal_handlers_disconnect_by_func (child_2, on_event_return_propagate, &n_child_2_leave_events);
  g_signal_handlers_disconnect_by_func (child_1, on_event_return_propagate, &n_child_1_leave_events);
  g_signal_handlers_disconnect_by_func (stage, on_event_return_propagate, &n_stage_leave_events);
  g_signal_handlers_disconnect_by_func (child_2, on_event_return_propagate, &n_child_2_enter_events);
  g_signal_handlers_disconnect_by_func (child_1, on_event_return_propagate, &n_child_1_enter_events);
  g_signal_handlers_disconnect_by_func (stage, on_event_return_propagate, &n_stage_enter_events);

  g_signal_handlers_disconnect_by_func (child_2, on_event_return_propagate, &n_child_2_motion_events);
  g_signal_handlers_disconnect_by_func (child_1, on_event_return_propagate, &n_child_1_motion_events);
  g_signal_handlers_disconnect_by_func (stage, on_event_return_propagate, &n_stage_motion_events);
  g_signal_handlers_disconnect_by_func (child_2, on_event_return_propagate, &n_child_2_button_events);
  g_signal_handlers_disconnect_by_func (child_1, on_event_return_propagate, &n_child_1_button_events);
  g_signal_handlers_disconnect_by_func (stage, on_event_return_propagate, &n_stage_button_events);
  clutter_actor_destroy (child_2);
  clutter_actor_destroy (child_1);
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
event_delivery_stop_discrete_event (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  TestAction *test_action;
  ClutterActor *child;
  gboolean was_updated;
  unsigned int n_child_motion_events, n_stage_motion_events;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  now_us = g_get_monotonic_time ();

  test_action = g_object_new (TEST_TYPE_ACTION, NULL);
  action_handle_event_retval = CLUTTER_EVENT_STOP;

  child = clutter_actor_new ();
  clutter_actor_set_reactive (child, TRUE);
  clutter_actor_set_size (child, 20, 20);
  clutter_actor_add_action (child, CLUTTER_ACTION (test_action));
  clutter_actor_add_child (stage, child);

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);
  g_signal_connect (stage, "event::motion", G_CALLBACK (on_event_return_stop),
                    &n_stage_motion_events);
  g_signal_connect (child, "event::motion", G_CALLBACK (on_event_return_stop),
                    &n_child_motion_events);

  clutter_actor_show (stage);
  wait_stage_updated (&was_updated);

  n_stage_motion_events = n_action_motion_events = n_child_motion_events = 0;
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 1, 1);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_action_motion_events, ==, 1);
  g_assert_cmpint (n_child_motion_events, ==, 0);
  g_assert_cmpint (n_stage_motion_events, ==, 0);

  action_handle_event_retval = CLUTTER_EVENT_PROPAGATE;

  n_stage_motion_events = n_action_motion_events = n_child_motion_events = 0;
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer, now_us, 1, 1);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_action_motion_events, ==, 1);
  g_assert_cmpint (n_child_motion_events, ==, 1);
  g_assert_cmpint (n_stage_motion_events, ==, 0);

  g_signal_handlers_disconnect_by_func (child, on_event_return_propagate, &n_child_motion_events);
  clutter_actor_destroy (child);
  g_signal_handlers_disconnect_by_func (stage, on_event_return_stop, &n_stage_motion_events);
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

static void
event_delivery_actor_stop_sequence_event (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterSeat *seat =
    clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  int64_t now_us;
  TestAction *test_action;
  ClutterActor *child;
  gboolean was_updated;
  unsigned int n_child_touch_events, n_stage_touch_events;

  virtual_pointer = clutter_seat_create_virtual_device (seat, CLUTTER_TOUCHSCREEN_DEVICE);
  now_us = g_get_monotonic_time ();

  test_action = g_object_new (TEST_TYPE_ACTION, NULL);
  action_handle_event_retval = CLUTTER_EVENT_PROPAGATE;

  child = clutter_actor_new ();
  clutter_actor_set_reactive (child, TRUE);
  clutter_actor_set_size (child, 20, 20);
  clutter_actor_add_action (child, CLUTTER_ACTION (test_action));
  clutter_actor_add_child (stage, child);

  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update),
                    &was_updated);
  g_signal_connect (stage, "event::touch", G_CALLBACK (on_event_return_stop),
                    &n_stage_touch_events);
  g_signal_connect (child, "captured-event::touch", G_CALLBACK (on_event_return_stop),
                    &n_child_touch_events);

  clutter_actor_show (stage);
  wait_stage_updated (&was_updated);

  n_stage_touch_events = n_action_touch_events = n_child_touch_events = 0;
  n_action_sequences_cancelled = 0;
  clutter_virtual_input_device_notify_touch_down (virtual_pointer, now_us, 0, 1, 1);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_touch_events, ==, 1);
  g_assert_cmpint (n_action_sequences_cancelled, ==, 1);
  g_assert_cmpint (n_action_touch_events, ==, 0);
  g_assert_cmpint (n_stage_touch_events, ==, 0);

  /* Even if the child now lets events propagate, the action should no longer see them */
  g_signal_handlers_disconnect_by_func (child, on_event_return_stop, &n_child_touch_events);
  g_signal_connect (child, "captured-event::touch", G_CALLBACK (on_event_return_propagate),
                    &n_child_touch_events);

  n_stage_touch_events = n_action_touch_events = n_child_touch_events = 0;
  n_action_sequences_cancelled = 0;
  clutter_virtual_input_device_notify_touch_motion (virtual_pointer, now_us, 0, 1, 1);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_touch_events, ==, 1);
  g_assert_cmpint (n_action_sequences_cancelled, ==, 0);
  g_assert_cmpint (n_action_touch_events, ==, 0);
  g_assert_cmpint (n_stage_touch_events, ==, 1);

  n_stage_touch_events = n_action_touch_events = n_child_touch_events = 0;
  n_action_sequences_cancelled = 0;
  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 0);
  wait_stage_updated (&was_updated);
  g_assert_cmpint (n_child_touch_events, ==, 1);
  g_assert_cmpint (n_action_sequences_cancelled, ==, 0);
  g_assert_cmpint (n_action_touch_events, ==, 0);
  g_assert_cmpint (n_stage_touch_events, ==, 1);

  g_signal_handlers_disconnect_by_func (child, on_event_return_propagate, &n_child_touch_events);
  clutter_actor_destroy (child);
  g_signal_handlers_disconnect_by_func (stage, on_event_return_stop, &n_stage_touch_events);
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/event/delivery/consecutive-touch-begin-end", event_delivery_consecutive_touch_begin_end);
  CLUTTER_TEST_UNIT ("/event/delivery/implicit-grabbing", event_delivery_implicit_grabbing);
  CLUTTER_TEST_UNIT ("/event/delivery/implicit-grab-cancelled", event_delivery_implicit_grab_cancelled);
  CLUTTER_TEST_UNIT ("/event/delivery/implicit-grab-existing-clutter-grab", event_delivery_implicit_grab_existing_clutter_grab);
  CLUTTER_TEST_UNIT ("/event/delivery/stop-discrete-event", event_delivery_stop_discrete_event);
  CLUTTER_TEST_UNIT ("/event/delivery/actor-stop-sequence-event", event_delivery_actor_stop_sequence_event);
)
