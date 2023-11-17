#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"
#include "clutter/clutter-event-private.h"
#include "clutter/clutter-stage-private.h"

typedef struct
{
  const char *name;
  ClutterEventType type;
} EventLog;

typedef struct
{
  ClutterActor *stage, *a, *b, *c;
  GArray *events;
} TestData;

static void
event_log_compare (EventLog *expected,
                   GArray   *obtained)
{
  EventLog *elem;
  guint i;

  for (i = 0; expected[i].name != NULL; i++)
    {
      g_assert_cmpuint (i, <, obtained->len);
      elem = &g_array_index (obtained, EventLog, i);
      g_assert_cmpuint (expected[i].type, ==, elem->type);
      g_assert_cmpstr (expected[i].name, ==, elem->name);
    }

  if (i != obtained->len)
    {
      elem = &g_array_index (obtained, EventLog, i);
      g_critical ("Unexpected event %d on actor '%s'",
                  elem->type, elem->name);
    }

  g_assert_cmpuint (i, ==, obtained->len);

  /* Clear the array for future comparisons */
  g_array_set_size (obtained, 0);
}

static gboolean
event_cb (ClutterActor *actor,
          ClutterEvent *event,
          gpointer      user_data)
{
  GArray *events = user_data;
  EventLog entry;

  switch (clutter_event_type (event))
    {
    case CLUTTER_ENTER:
    case CLUTTER_LEAVE:
      if ((clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_GRAB_NOTIFY) != 0)
        {
          entry = (EventLog) {
            clutter_actor_get_name (actor),
            clutter_event_type (event)
          };

          g_debug ("Event '%s' on actor '%s'",
                   clutter_event_get_name (event),
                   entry.name);
          g_array_append_val (events, entry);
        }
      break;

    default:
      entry = (EventLog) {
        clutter_actor_get_name (actor),
        clutter_event_type (event)
      };
      g_debug ("Event '%s' on actor '%s'",
               clutter_event_get_name (event),
               entry.name);
      g_array_append_val (events, entry);
      break;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static void
create_actors (ClutterActor  *stage,
               ClutterActor **a,
               ClutterActor **b,
               ClutterActor **c)
{
  /* This builds the following tree:
   *
   *    stage
   *     ╱ ╲
   *    a   c
   *   ╱
   *  b
   */

  *a = clutter_actor_new ();
  clutter_actor_set_name (*a, "a");
  clutter_actor_set_reactive (*a, TRUE);
  clutter_actor_set_width (*a, clutter_actor_get_width (stage) / 2);
  clutter_actor_set_height (*a, clutter_actor_get_height (stage));
  clutter_actor_add_child (stage, *a);

  *b = clutter_actor_new ();
  clutter_actor_set_name (*b, "b");
  clutter_actor_set_reactive (*b, TRUE);
  clutter_actor_set_width (*b, clutter_actor_get_width (stage) / 2);
  clutter_actor_set_height (*b, clutter_actor_get_height (stage));
  clutter_actor_add_child (*a, *b);

  *c = clutter_actor_new ();
  clutter_actor_set_name (*c, "c");
  clutter_actor_set_reactive (*c, TRUE);
  clutter_actor_set_x (*c, clutter_actor_get_width (stage) / 2);
  clutter_actor_set_width (*c, clutter_actor_get_width (stage) / 2);
  clutter_actor_set_height (*c, clutter_actor_get_height (stage));
  clutter_actor_add_child (stage, *c);
}

static void
has_pointer_cb (ClutterActor *actor)
{
  if (clutter_actor_has_pointer (actor))
    clutter_test_quit ();
}

static void
create_pointer (ClutterActor *actor)
{
  ClutterVirtualInputDevice *pointer;
  ClutterSeat *seat;
  guint notify_id;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);

  clutter_virtual_input_device_notify_absolute_motion (pointer,
                                                       0,
                                                       clutter_actor_get_x (actor) +
                                                       clutter_actor_get_width (actor) / 2,
                                                       clutter_actor_get_y (actor) +
                                                       clutter_actor_get_height (actor) / 2);

  notify_id = g_signal_connect (actor, "notify::has-pointer",
                                G_CALLBACK (has_pointer_cb), NULL);
  clutter_test_main ();

  g_signal_handler_disconnect (actor, notify_id);

  g_object_unref (pointer);
}

static void
connect_signals (ClutterActor *stage,
                 ClutterActor *a,
                 ClutterActor *b,
                 ClutterActor *c,
                 gpointer      user_data)
{
  g_signal_connect (stage, "event", G_CALLBACK (event_cb), user_data);
  g_signal_connect (a, "event", G_CALLBACK (event_cb), user_data);
  g_signal_connect (b, "event", G_CALLBACK (event_cb), user_data);
  g_signal_connect (c, "event", G_CALLBACK (event_cb), user_data);
}

static void
disconnect_signals (ClutterActor *stage,
                    ClutterActor *a,
                    ClutterActor *b,
                    ClutterActor *c,
                    gpointer      user_data)
{
  g_signal_handlers_disconnect_by_func (stage, event_cb, user_data);
  g_signal_handlers_disconnect_by_func (a, event_cb, user_data);
  g_signal_handlers_disconnect_by_func (b, event_cb, user_data);
  g_signal_handlers_disconnect_by_func (c, event_cb, user_data);
}

static void
test_data_init (TestData *data)
{
  ClutterActor *stage;
  ClutterActor *a, *b, *c;
  GArray *events;

  stage = clutter_test_get_stage ();
  clutter_actor_set_name (stage, "stage");
  create_actors (stage, &a, &b, &c);
  clutter_actor_show (stage);
  create_pointer (b);

  events = g_array_new (TRUE, TRUE, sizeof (EventLog));

  connect_signals (stage, a, b, c, events);

  *data = (TestData) {
    stage, a, b, c, events,
  };
}

static void
test_data_shutdown (TestData *data)
{
  disconnect_signals (data->stage, data->a, data->b, data->c, data->events);
  clutter_actor_destroy (data->c);
  clutter_actor_destroy (data->b);
  clutter_actor_destroy (data->a);
  g_array_unref (data->events);
}

static void
grab_under_pointer (void)
{
  TestData data;
  ClutterGrab *grab;
  EventLog grab_log[] = {
    { "a", CLUTTER_LEAVE },
    { "stage", CLUTTER_LEAVE },
    { NULL, 0 },
  };
  EventLog ungrab_log[] = {
    { "a", CLUTTER_ENTER },
    { "stage", CLUTTER_ENTER },
    { NULL, 0 },
  };

  test_data_init (&data);

  /* Grab 'b', pointer is on 'b' */
  grab = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.b);
  event_log_compare ((EventLog *) &grab_log, data.events);

  clutter_grab_dismiss (grab);
  g_clear_object (&grab);
  event_log_compare ((EventLog *) &ungrab_log, data.events);

  test_data_shutdown (&data);
}

static void
grab_under_pointers_parent (void)
{
  TestData data;
  ClutterGrab *grab;
  EventLog grab_log[] = {
    { "stage", CLUTTER_LEAVE },
    { NULL, 0 },
  };
  EventLog ungrab_log[] = {
    { "stage", CLUTTER_ENTER },
    { NULL, 0 },
  };

  test_data_init (&data);

  /* Grab 'a', pointer is on its child 'b' */
  grab = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.a);
  event_log_compare ((EventLog *) &grab_log, data.events);

  clutter_grab_dismiss (grab);
  g_clear_object (&grab);
  event_log_compare ((EventLog *) &ungrab_log, data.events);

  test_data_shutdown (&data);
}

static void
grab_outside_pointer (void)
{
  TestData data;
  ClutterGrab *grab;
  EventLog grab_log[] = {
    { "b", CLUTTER_LEAVE },
    { "a", CLUTTER_LEAVE },
    { "stage", CLUTTER_LEAVE },
    { NULL, 0 },
  };
  EventLog ungrab_log[] = {
    { "b", CLUTTER_ENTER },
    { "a", CLUTTER_ENTER },
    { "stage", CLUTTER_ENTER },
    { NULL, 0 },
  };

  test_data_init (&data);

  /* Grab 'c', pointer is on 'b' */
  grab = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.c);
  event_log_compare ((EventLog *) &grab_log, data.events);

  clutter_grab_dismiss (grab);
  g_clear_object (&grab);
  event_log_compare ((EventLog *) &ungrab_log, data.events);

  test_data_shutdown (&data);
}

static void
grab_stage (void)
{
  TestData data;
  ClutterGrab *grab;
  EventLog grab_log[] = {
    { NULL, 0 },
  };
  EventLog ungrab_log[] = {
    { NULL, 0 },
  };

  test_data_init (&data);

  /* Grab 'stage', pointer is on 'b' */
  grab = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.stage);
  event_log_compare ((EventLog *) &grab_log, data.events);

  clutter_grab_dismiss (grab);
  g_clear_object (&grab);
  event_log_compare ((EventLog *) &ungrab_log, data.events);

  test_data_shutdown (&data);
}

static void
grab_stack_1 (void)
{
  TestData data;
  ClutterGrab *grab1, *grab2;
  EventLog grab1_log[] = {
    { "a", CLUTTER_LEAVE },
    { "stage", CLUTTER_LEAVE },
    { NULL, 0 },
  };
  EventLog grab2_log[] = {
    { "b", CLUTTER_LEAVE },
    { "a", CLUTTER_LEAVE },
    { "stage", CLUTTER_LEAVE },
    { NULL, 0 },
  };
  EventLog ungrab2_log[] = {
    { "b", CLUTTER_ENTER },
    { NULL, 0 },
  };
  EventLog ungrab1_log[] = {
    { "a", CLUTTER_ENTER },
    { "stage", CLUTTER_ENTER },
    { NULL, 0 },
  };

  test_data_init (&data);

  /* Grab 'b', pointer is on 'b' */
  grab1 = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.b);
  event_log_compare ((EventLog *) &grab1_log, data.events);

  /* Grab 'c', pointer and grab is on 'b' */
  grab2 = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.c);
  event_log_compare ((EventLog *) &grab2_log, data.events);

  /* Dismiss orderly */
  clutter_grab_dismiss (grab2);
  g_clear_object (&grab2);
  event_log_compare ((EventLog *) &ungrab2_log, data.events);

  clutter_grab_dismiss (grab1);
  g_clear_object (&grab1);
  event_log_compare ((EventLog *) &ungrab1_log, data.events);

  test_data_shutdown (&data);
}

static void
grab_stack_2 (void)
{
  TestData data;
  ClutterGrab *grab1, *grab2;
  EventLog grab1_log[] = {
    { "b", CLUTTER_LEAVE },
    { "a", CLUTTER_LEAVE },
    { "stage", CLUTTER_LEAVE },
    { NULL, 0 },
  };
  EventLog grab2_log[] = {
    { "b", CLUTTER_ENTER },
    { NULL, 0 },
  };
  EventLog ungrab2_log[] = {
    { "b", CLUTTER_LEAVE },
    { "a", CLUTTER_LEAVE },
    { "stage", CLUTTER_LEAVE },
    { NULL, 0 },
  };
  EventLog ungrab1_log[] = {
    { "b", CLUTTER_ENTER },
    { "a", CLUTTER_ENTER },
    { "stage", CLUTTER_ENTER },
    { NULL, 0 },
  };

  test_data_init (&data);

  /* Grab 'c', pointer is on 'b' */
  grab1 = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.c);
  event_log_compare ((EventLog *) &grab1_log, data.events);

  /* Grab 'b', pointer is on b, prior grab is on 'c' */
  grab2 = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.b);
  event_log_compare ((EventLog *) &grab2_log, data.events);

  /* Dismiss orderly */
  clutter_grab_dismiss (grab2);
  g_clear_object (&grab2);
  event_log_compare ((EventLog *) &ungrab2_log, data.events);

  clutter_grab_dismiss (grab1);
  g_clear_object (&grab1);
  event_log_compare ((EventLog *) &ungrab1_log, data.events);

  test_data_shutdown (&data);
}

static void
grab_unordered_ungrab_1 (void)
{
  TestData data;
  ClutterGrab *grab1, *grab2;
  EventLog grab1_log[] = {
    { "a", CLUTTER_LEAVE },
    { "stage", CLUTTER_LEAVE },
    { NULL, 0 },
  };
  EventLog grab2_log[] = {
    { "b", CLUTTER_LEAVE },
    { "a", CLUTTER_LEAVE },
    { "stage", CLUTTER_LEAVE },
    { NULL, 0 },
  };
  EventLog ungrab1_log[] = {
    { NULL, 0 },
  };
  EventLog ungrab2_log[] = {
    { "b", CLUTTER_ENTER },
    { "a", CLUTTER_ENTER },
    { "stage", CLUTTER_ENTER },
    { NULL, 0 },
  };

  test_data_init (&data);

  /* Grab 'b', pointer is on 'b' */
  grab1 = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.b);
  event_log_compare ((EventLog *) &grab1_log, data.events);

  /* Grab 'c', pointer and grab is on 'b' */
  grab2 = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.c);
  event_log_compare ((EventLog *) &grab2_log, data.events);

  /* Dismiss disorderly */
  clutter_grab_dismiss (grab1);
  g_clear_object (&grab1);
  event_log_compare ((EventLog *) &ungrab1_log, data.events);

  clutter_grab_dismiss (grab2);
  g_clear_object (&grab2);
  event_log_compare ((EventLog *) &ungrab2_log, data.events);

  test_data_shutdown (&data);
}

static void
grab_unordered_ungrab_2 (void)
{
  TestData data;
  ClutterGrab *grab1, *grab2;
  EventLog grab1_log[] = {
    { "b", CLUTTER_LEAVE },
    { "a", CLUTTER_LEAVE },
    { "stage", CLUTTER_LEAVE },
    { NULL, 0 },
  };
  EventLog grab2_log[] = {
    { "b", CLUTTER_ENTER },
    { NULL, 0 },
  };
  EventLog ungrab1_log[] = {
    { NULL, 0 },
  };
  EventLog ungrab2_log[] = {
    { "a", CLUTTER_ENTER },
    { "stage", CLUTTER_ENTER },
    { NULL, 0 },
  };

  test_data_init (&data);

  /* Grab 'c', pointer is on 'b' */
  grab1 = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.c);
  event_log_compare ((EventLog *) &grab1_log, data.events);

  /* Grab 'b', pointer is on b, prior grab is on 'c' */
  grab2 = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.b);
  event_log_compare ((EventLog *) &grab2_log, data.events);

  /* Dismiss disorderly */
  clutter_grab_dismiss (grab1);
  g_clear_object (&grab1);
  event_log_compare ((EventLog *) &ungrab1_log, data.events);

  clutter_grab_dismiss (grab2);
  g_clear_object (&grab2);
  event_log_compare ((EventLog *) &ungrab2_log, data.events);

  test_data_shutdown (&data);
}

static void
grab_key_focus_in_grab (void)
{
  TestData data;
  ClutterGrab *grab;

  test_data_init (&data);

  clutter_actor_grab_key_focus (data.b);
  g_assert_true (clutter_actor_has_key_focus (data.b));

  grab = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.b);
  g_assert_true (clutter_actor_has_key_focus (data.b));

  clutter_grab_dismiss (grab);
  g_clear_object (&grab);
  g_assert_true (clutter_actor_has_key_focus (data.b));

  test_data_shutdown (&data);
}

static void
grab_key_focus_outside_grab (void)
{
  TestData data;
  ClutterGrab *grab;

  test_data_init (&data);

  clutter_actor_grab_key_focus (data.b);
  g_assert_true (clutter_actor_has_key_focus (data.b));

  grab = clutter_stage_grab (CLUTTER_STAGE (data.stage), data.c);
  g_assert_false (clutter_actor_has_key_focus (data.b));

  clutter_grab_dismiss (grab);
  g_clear_object (&grab);
  g_assert_true (clutter_actor_has_key_focus (data.b));

  test_data_shutdown (&data);
}

static gboolean
handle_input_only_event (const ClutterEvent *event,
                         gpointer            user_data)
{
  GArray *events = user_data;
  EventLog entry = { "input-only grab", clutter_event_type (event) };

  g_debug ("Input only grab event '%s'", clutter_event_get_name (event));
  g_array_append_val (events, entry);

  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
last_event_is (GArray           *events,
               ClutterEventType  event_type)
{
  EventLog *entry;

  if (events->len == 0)
    return FALSE;

  entry = &g_array_index (events, EventLog, events->len - 1);
  return entry->type == event_type;
}

static void
grab_input_only (void)
{
  TestData data;
  ClutterGrab *grab;
  EventLog grab1_log[] = {
    { "b", CLUTTER_LEAVE },
    { "a", CLUTTER_LEAVE },
    { "stage", CLUTTER_LEAVE },
    { NULL, 0 },
  };
  EventLog grab2_log[] = {
    { "input-only grab", CLUTTER_BUTTON_PRESS },
    { "input-only grab", CLUTTER_BUTTON_RELEASE },
    { NULL, 0 },
  };
  EventLog grab3_log[] = {
    { "b", CLUTTER_ENTER },
    { "a", CLUTTER_ENTER },
    { "stage", CLUTTER_ENTER },
    { NULL, 0 },
  };
  EventLog grab4_log[] = {
    { "b", CLUTTER_BUTTON_PRESS },
    { "a", CLUTTER_BUTTON_PRESS },
    { "stage", CLUTTER_BUTTON_PRESS },
    { "b", CLUTTER_BUTTON_RELEASE },
    { "a", CLUTTER_BUTTON_RELEASE },
    { "stage", CLUTTER_BUTTON_RELEASE },
    { NULL, 0 },
  };
  ClutterSeat *seat;
  g_autoptr (ClutterVirtualInputDevice) pointer = NULL;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  pointer = clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);

  test_data_init (&data);

  grab = clutter_stage_grab_input_only (CLUTTER_STAGE (data.stage),
                                        handle_input_only_event,
                                        data.events, NULL);
  event_log_compare ((EventLog *) &grab1_log, data.events);

  clutter_virtual_input_device_notify_button (pointer,
                                              0,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  clutter_virtual_input_device_notify_button (pointer,
                                              0,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);

  while (!last_event_is (data.events, CLUTTER_BUTTON_RELEASE))
    g_main_context_iteration (NULL, TRUE);
  event_log_compare ((EventLog *) &grab2_log, data.events);

  g_clear_object (&grab);
  event_log_compare ((EventLog *) &grab3_log, data.events);

  clutter_virtual_input_device_notify_button (pointer,
                                              0,
                                              CLUTTER_BUTTON_SECONDARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  clutter_virtual_input_device_notify_button (pointer,
                                              0,
                                              CLUTTER_BUTTON_SECONDARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);
  while (!last_event_is (data.events, CLUTTER_BUTTON_RELEASE))
    g_main_context_iteration (NULL, TRUE);
  event_log_compare ((EventLog *) &grab4_log, data.events);

  test_data_shutdown (&data);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/grab/input-only", grab_input_only);
  CLUTTER_TEST_UNIT ("/grab/grab-under-pointer", grab_under_pointer)
  CLUTTER_TEST_UNIT ("/grab/grab-under-pointers-parent", grab_under_pointers_parent)
  CLUTTER_TEST_UNIT ("/grab/grab-outside-pointer", grab_outside_pointer)
  CLUTTER_TEST_UNIT ("/grab/grab-stage", grab_stage)
  CLUTTER_TEST_UNIT ("/grab/grab-stack-1", grab_stack_1)
  CLUTTER_TEST_UNIT ("/grab/grab-stack-2", grab_stack_2)
  CLUTTER_TEST_UNIT ("/grab/grab-unordered-ungrab-1", grab_unordered_ungrab_1)
  CLUTTER_TEST_UNIT ("/grab/grab-unordered-ungrab-2", grab_unordered_ungrab_2)
  CLUTTER_TEST_UNIT ("/grab/key-focus-in-grab", grab_key_focus_in_grab);
  CLUTTER_TEST_UNIT ("/grab/key-focus-outside-grab", grab_key_focus_outside_grab);
)
