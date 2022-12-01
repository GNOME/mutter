#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

static void
on_after_update (ClutterStage     *stage,
                 ClutterStageView *view,
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

static void
wait_stage_updated (ClutterStage *stage,
                    gboolean     *was_updated)
{
  *was_updated = FALSE;
  clutter_stage_schedule_update (stage);
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
  clutter_test_flush_input ();
  wait_stage_updated (CLUTTER_STAGE (stage), &was_updated);
  g_assert_cmpint (n_captured_touch_events, ==, 3);

  clutter_virtual_input_device_notify_touch_up (virtual_pointer, now_us, 0);
  clutter_test_flush_input ();
  wait_stage_updated (CLUTTER_STAGE (stage), &was_updated);
  g_assert_cmpint (n_captured_touch_events, ==, 4);

  g_signal_handlers_disconnect_by_func (stage, on_event_return_stop, &n_captured_touch_events);
  g_signal_handlers_disconnect_by_func (stage, on_after_update, &was_updated);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/event/delivery/consecutive-touch-begin-end", event_delivery_consecutive_touch_begin_end);
)
