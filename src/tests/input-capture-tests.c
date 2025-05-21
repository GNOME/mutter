/*
 * Copyright (C) 2022 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <gio/gio.h>
#include <linux/input-event-codes.h>

#include "backends/meta-backend-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-test-utils.h"

typedef struct _InputCaptureTestClient
{
  GSubprocess *subprocess;
  char *path;
  GMainLoop *main_loop;
  GDataInputStream *line_reader;
  GDataOutputStream *line_writer;
} InputCaptureTestClient;

static MetaContext *test_context;

static InputCaptureTestClient *
input_capture_test_client_new (const char *test_case)
{
  g_autofree char *test_client_path = NULL;
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  GSubprocess *subprocess;
  GError *error = NULL;
  InputCaptureTestClient *test_client;
  GInputStream *stdout_stream;
  GDataInputStream *line_reader;
  GOutputStream *stdin_stream;
  GDataOutputStream *line_writer;

  test_client_path = g_test_build_filename (G_TEST_BUILT,
                                            "mutter-input-capture-test-client",
                                            NULL);
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                        G_SUBPROCESS_FLAGS_STDIN_PIPE);
  subprocess = g_subprocess_launcher_spawn (launcher,
                                            &error,
                                            test_client_path,
                                            test_case,
                                            NULL);
  if (!subprocess)
    g_error ("Failed to launch input capture test client: %s", error->message);

  stdout_stream = g_subprocess_get_stdout_pipe (subprocess);
  line_reader = g_data_input_stream_new (stdout_stream);

  stdin_stream = g_subprocess_get_stdin_pipe (subprocess);
  line_writer = g_data_output_stream_new (stdin_stream);

  test_client = g_new0 (InputCaptureTestClient, 1);
  test_client->subprocess = subprocess;
  test_client->main_loop = g_main_loop_new (NULL, FALSE);
  test_client->line_reader = line_reader;
  test_client->line_writer = line_writer;

  return test_client;
}

typedef struct
{
  GMainLoop *loop;
  const char *expected_state;
} WaitData;

static void
on_line_read (GObject      *source_object,
              GAsyncResult *res,
              gpointer      user_data)
{
  WaitData *data = user_data;
  g_autofree char *line = NULL;
  g_autoptr (GError) error = NULL;

  line =
    g_data_input_stream_read_line_finish (G_DATA_INPUT_STREAM (source_object),
                                          res, NULL, &error);
  if (error)
    g_error ("Failed to read line from test client: %s", error->message);
  if (!line)
    g_error ("Unexpected EOF");

  g_assert_cmpstr (data->expected_state, ==, line);

  g_main_loop_quit (data->loop);
}

static void
input_capture_test_client_wait_for_state (InputCaptureTestClient *test_client,
                                          const char             *expected_state)
{
  WaitData data;

  data.loop = g_main_loop_new (NULL, FALSE);
  data.expected_state = expected_state;

  g_data_input_stream_read_line_async (test_client->line_reader,
                                       G_PRIORITY_DEFAULT,
                                       NULL,
                                       on_line_read,
                                       &data);

  g_main_loop_run (data.loop);
  g_main_loop_unref (data.loop);
}

static void
input_capture_test_client_write_state (InputCaptureTestClient *test_client,
                                       const char             *state)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *line = NULL;

  line = g_strdup_printf ("%s\n", state);

  if (!g_data_output_stream_put_string (test_client->line_writer,
                                        line, NULL, &error))
    g_error ("Failed to write state: %s", error->message);

  if (!g_output_stream_flush (G_OUTPUT_STREAM (test_client->line_writer),
                              NULL, &error))
    g_error ("Failed to flush state: %s", error->message);
}

static void
input_capture_test_client_finished (GObject      *source_object,
                                    GAsyncResult *res,
                                    gpointer      user_data)
{
  InputCaptureTestClient *test_client = user_data;
  GError *error = NULL;

  if (!g_subprocess_wait_finish (test_client->subprocess,
                                 res,
                                 &error))
    {
      g_error ("Failed to wait for input capture test client: %s",
               error->message);
    }

  g_main_loop_quit (test_client->main_loop);
}

static void
input_capture_test_client_finish (InputCaptureTestClient *test_client)
{
  g_subprocess_wait_async (test_client->subprocess, NULL,
                           input_capture_test_client_finished, test_client);

  g_main_loop_run (test_client->main_loop);

  g_assert_true (g_subprocess_get_successful (test_client->subprocess));

  g_main_loop_unref (test_client->main_loop);
  g_object_unref (test_client->line_reader);
  g_object_unref (test_client->subprocess);
  g_free (test_client);
}

static void
click_button (ClutterVirtualInputDevice *virtual_pointer,
              uint32_t                   button)
{
  clutter_virtual_input_device_notify_button (virtual_pointer,
                                              g_get_monotonic_time (),
                                              button,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  clutter_virtual_input_device_notify_button (virtual_pointer,
                                              g_get_monotonic_time (),
                                              button,
                                              CLUTTER_BUTTON_STATE_RELEASED);
}

static void
press_key (ClutterVirtualInputDevice *virtual_keyboard,
           uint32_t                   key)
{
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           key,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           key,
                                           CLUTTER_KEY_STATE_RELEASED);
}

static void
meta_test_input_capture_sanity (void)
{
  InputCaptureTestClient *test_client;

  test_client = input_capture_test_client_new ("sanity");
  input_capture_test_client_finish (test_client);
}

static void
meta_test_input_capture_zones (void)
{
  g_autoptr (MetaVirtualMonitor) virtual_monitor1 = NULL;
  g_autoptr (MetaVirtualMonitor) virtual_monitor2 = NULL;
  InputCaptureTestClient *test_client;

  virtual_monitor1 = meta_create_test_monitor (test_context, 800, 600, 20.0);
  virtual_monitor2 = meta_create_test_monitor (test_context, 1024, 768, 20.0);

  test_client = input_capture_test_client_new ("zones");

  input_capture_test_client_wait_for_state (test_client, "1");

  g_clear_object (&virtual_monitor1);

  input_capture_test_client_finish (test_client);
}

static void
assert_pointer_position (MetaBackend *backend,
                         double          x,
                         double          y)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  graphene_point_t pos;

  clutter_seat_query_state (seat, NULL, &pos, NULL);

  g_assert_cmpfloat_with_epsilon (pos.x, x, DBL_EPSILON);
  g_assert_cmpfloat_with_epsilon (pos.y, y, DBL_EPSILON);
}

static void
meta_test_input_capture_barriers (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (MetaVirtualMonitor) virtual_monitor1 = NULL;
  g_autoptr (MetaVirtualMonitor) virtual_monitor2 = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  InputCaptureTestClient *test_client;

  virtual_monitor1 = meta_create_test_monitor (test_context, 800, 600, 20.0);
  virtual_monitor2 = meta_create_test_monitor (test_context, 1024, 768, 20.0);

  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       10.0, 10.0);

  test_client = input_capture_test_client_new ("barriers");
  input_capture_test_client_wait_for_state (test_client, "1");

  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       -20.0, 10.0);
  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       -20.0, 10.0);
  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       -20.0, 10.0);

  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);

  assert_pointer_position (backend, 0.0, 15.0);

  input_capture_test_client_write_state (test_client, "1");
  input_capture_test_client_wait_for_state (test_client, "2");

  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);

  assert_pointer_position (backend, 200.0, 150.0);

  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       800.0, 300.0);
  meta_flush_input (test_context);

  assert_pointer_position (backend, 1000.0, 450.0);

  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       0.0, 400.0);

  input_capture_test_client_wait_for_state (test_client, "3");
  meta_flush_input (test_context);
  assert_pointer_position (backend, 1200.0, 700.0);

  input_capture_test_client_finish (test_client);
}

static void
meta_test_input_capture_clear_barriers (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (MetaVirtualMonitor) virtual_monitor1 = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  InputCaptureTestClient *test_client;

  virtual_monitor1 = meta_create_test_monitor (test_context, 800, 600, 20.0);

  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       10.0, 10.0);

  test_client = input_capture_test_client_new ("clear-barriers");
  input_capture_test_client_wait_for_state (test_client, "1");

  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       -20.0, 0.0);
  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);
  assert_pointer_position (backend, 0.0, 10.0);

  input_capture_test_client_wait_for_state (test_client, "2");

  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       10.0, 10.0);
  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);
  assert_pointer_position (backend, 10.0, 20.0);

  input_capture_test_client_write_state (test_client, "1");
  input_capture_test_client_finish (test_client);
}

static void
meta_test_input_capture_cancel_keybinding (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (MetaVirtualMonitor) virtual_monitor = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  InputCaptureTestClient *test_client;

  virtual_monitor = meta_create_test_monitor (test_context, 800, 600, 20.0);
  virtual_keyboard = clutter_seat_create_virtual_device (seat,
                                                         CLUTTER_KEYBOARD_DEVICE);
  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       10.0, 10.0);

  test_client = input_capture_test_client_new ("cancel-keybinding");
  input_capture_test_client_wait_for_state (test_client, "1");

  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       -20.0, 0.0);
  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);
  assert_pointer_position (backend, 0.0, 10.0);

  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       10.0, 10.0);
  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);
  assert_pointer_position (backend, 0.0, 10.0);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTMETA,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTSHIFT,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_ESC,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_ESC,
                                           CLUTTER_KEY_STATE_RELEASED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTSHIFT,
                                           CLUTTER_KEY_STATE_RELEASED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTMETA,
                                           CLUTTER_KEY_STATE_RELEASED);

  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);

  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       10.0, 10.0);

  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);
  assert_pointer_position (backend, 10.0, 20.0);

  input_capture_test_client_write_state (test_client, "1");

  input_capture_test_client_finish (test_client);
}

static void
meta_test_input_capture_events (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (MetaVirtualMonitor) virtual_monitor1 = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  InputCaptureTestClient *test_client;

  virtual_monitor1 = meta_create_test_monitor (test_context, 800, 600, 20.0);

  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       10.0, 10.0);
  virtual_keyboard = clutter_seat_create_virtual_device (seat,
                                                         CLUTTER_KEYBOARD_DEVICE);

  test_client = input_capture_test_client_new ("events");
  input_capture_test_client_wait_for_state (test_client, "1");

  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       -20.0, -20.0);
  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       2.0, -5.0);
  click_button (virtual_pointer, CLUTTER_BUTTON_PRIMARY);
  press_key (virtual_keyboard, KEY_A);

  input_capture_test_client_finish (test_client);
}

static void
on_a11y_timeout_started (ClutterSeat                   *seat,
                         ClutterInputDevice            *device,
                         ClutterPointerA11yTimeoutType  timeout_type,
                         unsigned int                   delay_ms,
                         int                           *a11y_started_counter)
{
  (*a11y_started_counter)++;
}

static gboolean
atk_key_listener (AtkKeyEventStruct *event,
                  gpointer           user_data)
{
  int *a11y_key_counter = user_data;

  (*a11y_key_counter)++;

  return TRUE;
}

static void
meta_test_input_capture_a11y (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (MetaVirtualMonitor) virtual_monitor = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  InputCaptureTestClient *test_client;
  ClutterPointerA11yDwellClickType dwell_click_type;
  int a11y_started_counter = 0;
  int a11y_key_counter = 0;
  g_autoptr (GSettings) a11y_mouse_settings = NULL;
  guint atk_key_listener_id;

  atk_key_listener_id = atk_add_key_event_listener (atk_key_listener,
                                                    &a11y_key_counter);

  a11y_mouse_settings = g_settings_new ("org.gnome.desktop.a11y.mouse");

  virtual_monitor = meta_create_test_monitor (test_context, 800, 600, 20.0);
  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);
  virtual_keyboard = clutter_seat_create_virtual_device (seat,
                                                         CLUTTER_KEYBOARD_DEVICE);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       10.0, 10.0);

  g_settings_set_boolean (a11y_mouse_settings, "dwell-click-enabled", TRUE);
  g_settings_set_boolean (a11y_mouse_settings, "secondary-click-enabled", TRUE);

  dwell_click_type = CLUTTER_A11Y_DWELL_CLICK_TYPE_SECONDARY;
  clutter_seat_set_pointer_a11y_dwell_click_type (seat, dwell_click_type);
  g_signal_connect (seat, "ptr-a11y-timeout-started",
                    G_CALLBACK (on_a11y_timeout_started),
                    &a11y_started_counter);

  click_button (virtual_pointer, CLUTTER_BUTTON_PRIMARY);
  press_key (virtual_keyboard, KEY_A);
  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);
  g_assert_cmpint (a11y_started_counter, ==, 1);
  g_assert_cmpint (a11y_key_counter, ==, 2);

  test_client = input_capture_test_client_new ("a11y");
  input_capture_test_client_wait_for_state (test_client, "1");

  click_button (virtual_pointer, CLUTTER_BUTTON_PRIMARY);
  press_key (virtual_keyboard, KEY_A);
  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);
  g_assert_cmpint (a11y_started_counter, ==, 2);
  g_assert_cmpint (a11y_key_counter, ==, 4);

  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       -20.0, 0.0);

  click_button (virtual_pointer, CLUTTER_BUTTON_PRIMARY);
  press_key (virtual_keyboard, KEY_A);
  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);
  g_assert_cmpint (a11y_started_counter, ==, 2);
  g_assert_cmpint (a11y_key_counter, ==, 4);

  input_capture_test_client_write_state (test_client, "1");
  input_capture_test_client_finish (test_client);

  click_button (virtual_pointer, CLUTTER_BUTTON_PRIMARY);
  press_key (virtual_keyboard, KEY_A);
  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);
  g_assert_cmpint (a11y_started_counter, ==, 3);
  g_assert_cmpint (a11y_key_counter, ==, 6);

  dwell_click_type = CLUTTER_A11Y_DWELL_CLICK_TYPE_NONE;
  clutter_seat_set_pointer_a11y_dwell_click_type (seat, dwell_click_type);
  g_settings_set_boolean (a11y_mouse_settings, "dwell-click-enabled", FALSE);
  g_settings_set_boolean (a11y_mouse_settings, "secondary-click-enabled", FALSE);
  atk_remove_key_event_listener (atk_key_listener_id);
}

static void
meta_test_input_capture_disconnect (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (MetaVirtualMonitor) virtual_monitor = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  InputCaptureTestClient *test_client;

  virtual_monitor = meta_create_test_monitor (test_context, 800, 600, 20.0);
  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       10.0, 10.0);
  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);

  test_client = input_capture_test_client_new ("disconnect");

  input_capture_test_client_wait_for_state (test_client, "1");

  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       -20.0, -20.0);

  input_capture_test_client_write_state (test_client, "1");
  input_capture_test_client_wait_for_state (test_client, "2");

  input_capture_test_client_finish (test_client);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/input-capture/sanity",
                   meta_test_input_capture_sanity);
  g_test_add_func ("/backends/native/input-capture/zones",
                   meta_test_input_capture_zones);
  g_test_add_func ("/backends/native/input-capture/barriers",
                   meta_test_input_capture_barriers);
  g_test_add_func ("/backends/native/input-capture/clear-barriers",
                   meta_test_input_capture_clear_barriers);
  g_test_add_func ("/backends/native/input-capture/cancel-keybinding",
                   meta_test_input_capture_cancel_keybinding);
  g_test_add_func ("/backends/native/input-capture/events",
                   meta_test_input_capture_events);
  g_test_add_func ("/backends/native/input-capture/a11y",
                   meta_test_input_capture_a11y);
  g_test_add_func ("/backends/native/input-capture/disconnect",
                   meta_test_input_capture_disconnect);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = test_context =
    meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                              META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
