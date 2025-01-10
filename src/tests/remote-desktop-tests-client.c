/*
 * Copyright (C) 2025 Red Hat Inc.
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

#include <gio/gunixinputstream.h>
#include <glib.h>
#include <libei.h>
#include <linux/input.h>

#include "tests/remote-desktop-utils.h"

static RemoteDesktop *remote_desktop;
static ScreenCast *screen_cast;
static Session *session;
static Stream *stream;
static GDataInputStream *stdin_stream;

static void
emit_after_unbind_test (void)
{
  g_debug ("Binding keyboard capability");
  session_add_seat_capability (session, EI_DEVICE_CAP_KEYBOARD);
  while (!session->keyboard)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Pressing space");
  ei_device_keyboard_key (session->keyboard, KEY_SPACE, true);
  ei_device_frame (session->keyboard, g_get_monotonic_time ());
  ei_device_keyboard_key (session->keyboard, KEY_SPACE, false);
  ei_device_frame (session->keyboard, g_get_monotonic_time ());

  g_debug ("Unbinding keyboard capability");
  session_remove_seat_capability (session, EI_DEVICE_CAP_KEYBOARD);

  g_debug ("Pressing Esc");
  ei_device_keyboard_key (session->keyboard, KEY_ESC, true);
  ei_device_frame (session->keyboard, g_get_monotonic_time ());
  ei_device_keyboard_key (session->keyboard, KEY_ESC, false);
  ei_device_frame (session->keyboard, g_get_monotonic_time ());

  g_debug ("Binding pointer capability");
  session_add_seat_capability (session,
                               EI_DEVICE_CAP_POINTER);
  while (!session->pointer)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Move pointer");
  ei_device_pointer_motion (session->pointer, 1.0, 1.0);
  ei_device_frame (session->pointer, g_get_monotonic_time ());

  g_debug ("Unbinding pointer capability");
  session_remove_seat_capability (session, EI_DEVICE_CAP_POINTER);

  g_debug ("Move pointer again");
  ei_device_pointer_motion (session->pointer, 1.0, 1.0);
  ei_device_frame (session->pointer, g_get_monotonic_time ());

  while (session->pointer)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Binding absolute pointer capability");
  session_add_seat_capability (session,
                               EI_DEVICE_CAP_POINTER_ABSOLUTE);
  while (!session->pointer)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Move absolute pointer");
  ei_device_pointer_motion_absolute (session->pointer, 1.0, 1.0);
  ei_device_frame (session->pointer, g_get_monotonic_time ());

  g_debug ("Unbinding absolute pointer capability");
  session_remove_seat_capability (session, EI_DEVICE_CAP_POINTER_ABSOLUTE);

  g_debug ("Move absolute pointer again");
  ei_device_pointer_motion_absolute (session->pointer, 1.0, 1.0);
  ei_device_frame (session->pointer, g_get_monotonic_time ());

  while (session->pointer)
    g_main_context_iteration (NULL, TRUE);
}

G_GNUC_NULL_TERMINATED
static void
send_command (const char *command,
              ...)
{
  g_autoptr (GStrvBuilder) args_builder = NULL;
  g_auto (GStrv) args = NULL;
  va_list vap;
  g_autofree char *response = NULL;
  g_autoptr (GError) error = NULL;

  args_builder = g_strv_builder_new ();
  g_strv_builder_add (args_builder, command);

  va_start (vap, command);
  while (TRUE)
    {
      char *arg;

      arg = va_arg (vap, char *);
      if (!arg)
        break;

      g_strv_builder_add (args_builder, arg);
    }
  va_end (vap);

  args = g_strv_builder_end (args_builder);
  command = g_strjoinv (" ", args);

  fprintf (stdout, "%s\n", command);
  fflush (stdout);

  response = g_data_input_stream_read_line (stdin_stream, NULL, NULL, &error);
  g_assert_no_error (error);

  g_assert_cmpstr (response, ==, "OK");
}

static void
keyboard_layout_test (void)
{
  struct ei_device *old_keyboard;

  g_debug ("Binding keyboard capability");
  session_add_seat_capability (session, EI_DEVICE_CAP_KEYBOARD);
  while (!session->keyboard)
    g_main_context_iteration (NULL, TRUE);

  ei_device_keyboard_key (session->keyboard, KEY_B, true);
  ei_device_frame (session->keyboard, g_get_monotonic_time ());
  ei_device_keyboard_key (session->keyboard, KEY_B, false);
  ei_device_frame (session->keyboard, g_get_monotonic_time ());

  session_ei_roundtrip (session);
  send_command ("flush_input", NULL);

  old_keyboard = ei_device_ref (session->keyboard);

  send_command ("switch_keyboard_layout", "us", "dvorak-alt-intl", NULL);

  ei_device_keyboard_key (old_keyboard, KEY_B, true);
  ei_device_frame (old_keyboard, g_get_monotonic_time ());
  ei_device_keyboard_key (old_keyboard, KEY_B, false);
  ei_device_frame (old_keyboard, g_get_monotonic_time ());

  ei_device_unref (old_keyboard);
}

static void
change_viewport_test (void)
{
  struct ei_device *old_pointer;

  g_debug ("Binding absolute pointer capability");
  session_add_seat_capability (session,
                               EI_DEVICE_CAP_POINTER_ABSOLUTE);
  while (!session->pointer)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Move absolute pointer");
  ei_device_pointer_motion_absolute (session->pointer, 1.0, 1.0);
  ei_device_frame (session->pointer, g_get_monotonic_time ());

  session_ei_roundtrip (session);
  send_command ("flush_input", NULL);

  old_pointer = ei_device_ref (session->pointer);

  send_command ("update_viewports", session_get_id (session), NULL);

  ei_device_pointer_motion_absolute (old_pointer, 10.0, 10.0);
  ei_device_frame (old_pointer, g_get_monotonic_time ());
}

static void
run_test (int    argc,
          char **argv)
{
  g_debug ("Running test %s", argv[1]);

  if (strcmp (argv[1], "emit-after-unbind") == 0)
    emit_after_unbind_test ();
  else if (strcmp (argv[1], "keyboard-layout") == 0)
    keyboard_layout_test ();
  else if (strcmp (argv[1], "change-viewport") == 0)
    change_viewport_test ();
  else
    g_error ("Unknown test '%s'", argv[1]);
}

static void
print_to_stderr (const char *text)
{
  fputs (text, stderr);
  fflush (stderr);
}

int
main (int    argc,
      char **argv)
{
  GInputStream *stdin_unix_stream;

  g_set_print_handler (print_to_stderr);
  g_test_init (&argc, &argv, NULL);
  g_assert_cmpint (argc, ==, 2);

  stdin_unix_stream = g_unix_input_stream_new (fileno (stdin), FALSE);
  stdin_stream = g_data_input_stream_new (stdin_unix_stream);
  g_object_unref (stdin_unix_stream);

  g_debug ("Initializing PipeWire");
  init_pipewire ();

  g_debug ("Creating remote desktop session");
  remote_desktop = remote_desktop_new ();
  screen_cast = screen_cast_new ();
  session = screen_cast_create_session (remote_desktop, screen_cast);
  session_connect_to_eis (session);

  stream = session_record_virtual (session, 800, 600, CURSOR_MODE_METADATA);

  g_debug ("Starting remote desktop session");
  session_start (session);

  run_test (argc, argv);

  g_debug ("Stopping session");
  session_stop (session);

  stream_free (stream);
  session_free (session);
  screen_cast_free (screen_cast);
  remote_desktop_free (remote_desktop);

  release_pipewire ();

  g_object_unref (stdin_stream);

  g_debug ("Done");

  return EXIT_SUCCESS;
}
