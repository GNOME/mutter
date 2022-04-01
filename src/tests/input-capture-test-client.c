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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include <glib.h>
#include <gio/gunixinputstream.h>
#include <stdio.h>

#include "meta-dbus-input-capture.h"

typedef struct
{
  unsigned int width;
  unsigned int height;
  int x;
  int y;
} Zone;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Zone, g_free)

typedef enum _Capabilities
{
  CAPABILITY_NONE = 0,
  CAPABILITY_KEYBOARD = 1,
  CAPABILITY_POINTER = 2,
  CAPABILITY_TOUCH = 4,
} Capabilities;

typedef struct _InputCapture
{
  MetaDBusInputCapture *proxy;
} InputCapture;

typedef struct _InputCaptureSession
{
  MetaDBusInputCaptureSession *proxy;
  unsigned int serial;
} InputCaptureSession;

static GDataInputStream *stdin_reader;

static void
ping_mutter (InputCaptureSession *session)
{
  GDBusProxy *proxy = G_DBUS_PROXY (session->proxy);
  GError *error = NULL;

  if (!g_dbus_connection_call_sync (g_dbus_proxy_get_connection (proxy),
                                    "org.gnome.Mutter.InputCapture",
                                    g_dbus_proxy_get_object_path (proxy),
                                    "org.freedesktop.DBus.Peer",
                                    "Ping",
                                    NULL,
                                    NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, -1,
                                    NULL, &error))
    g_error ("Failed to ping D-Bus peer: %s", error->message);
}

static void
write_state (InputCaptureSession *session,
             const char          *state)
{
  ping_mutter (session);
  fprintf (stdout, "%s\n", state);
  fflush (stdout);
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
wait_for_state (InputCaptureSession *session,
                const char          *expected_state)
{
  WaitData data;

  data.loop = g_main_loop_new (NULL, FALSE);
  data.expected_state = expected_state;

  g_data_input_stream_read_line_async (stdin_reader,
                                       G_PRIORITY_DEFAULT,
                                       NULL,
                                       on_line_read,
                                       &data);

  g_main_loop_run (data.loop);
  g_main_loop_unref (data.loop);
  ping_mutter (session);
}

static InputCapture *
input_capture_new (void)
{
  InputCapture *input_capture;
  GError *error = NULL;

  input_capture = g_new0 (InputCapture, 1);
  input_capture->proxy = meta_dbus_input_capture_proxy_new_for_bus_sync (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
    "org.gnome.Mutter.InputCapture",
    "/org/gnome/Mutter/InputCapture",
    NULL,
    &error);
  if (!input_capture->proxy)
    g_error ("Failed to acquire proxy: %s", error->message);

  return input_capture;
}

static InputCaptureSession *
input_capture_create_session (InputCapture *input_capture)
{
  GError *error = NULL;
  InputCaptureSession *session;
  g_autofree char *session_path = NULL;

  if (!meta_dbus_input_capture_call_create_session_sync (input_capture->proxy,
                                                         CAPABILITY_POINTER,
                                                         &session_path,
                                                         NULL,
                                                         &error))
    g_error ("Failed to create input capture session: %s", error->message);

  session = g_new0 (InputCaptureSession, 1);
  session->proxy = meta_dbus_input_capture_session_proxy_new_for_bus_sync (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
    "org.gnome.Mutter.InputCapture",
    session_path,
    NULL, &error);
  if (!session->proxy)
    g_error ("Failed to acquire proxy: %s", error->message);

  return session;
}

static void
input_capture_session_close (InputCaptureSession *session)
{
  GError *error = NULL;

  if (!meta_dbus_input_capture_session_call_close_sync (session->proxy,
                                                        NULL, &error))
    g_error ("Failed to close session: %s", error->message);

  g_object_unref (session->proxy);
  g_free (session);
}

static GList *
input_capture_session_get_zones (InputCaptureSession *session)
{
  GError *error = NULL;
  g_autoptr (GVariant) zones_variant = NULL;
  GVariantIter iter;
  GList *zones = NULL;
  unsigned int width, height;
  int x, y;

  if (!meta_dbus_input_capture_session_call_get_zones_sync (session->proxy,
                                                            &session->serial,
                                                            &zones_variant,
                                                            NULL, &error))
    g_error ("Failed to get zones: %s", error->message);

  g_variant_iter_init (&iter, zones_variant);
  while (g_variant_iter_next (&iter, "(uuii)", &width, &height, &x, &y))
    {
      Zone *zone;

      zone = g_new0 (Zone, 1);
      *zone = (Zone) {
        .width = width,
        .height = height,
        .x = x,
        .y = y,
      };
      zones = g_list_append (zones, zone);
    }

  return zones;
}

static unsigned int
input_capture_session_add_barrier (InputCaptureSession *session,
                                   int                  x1,
                                   int                  y1,
                                   int                  x2,
                                   int                  y2)
{
  g_autoptr (GError) error = NULL;
  unsigned int barrier_id;

  if (!meta_dbus_input_capture_session_call_add_barrier_sync (
        session->proxy,
        session->serial,
        g_variant_new ("(iiii)", x1, y1, x2, y2),
        &barrier_id,
        NULL,
        &error))
    {
      g_warning ("Failed to add barrier: %s", error->message);
      return 0;
    }

  return barrier_id;
}

static void
input_capture_session_clear_barriers (InputCaptureSession *session)
{
  g_autoptr (GError) error = NULL;

  if (!meta_dbus_input_capture_session_call_clear_barriers_sync (
        session->proxy, NULL, &error))
    g_warning ("Failed to clear barriers: %s", error->message);
}

static void
input_capture_session_enable (InputCaptureSession *session)
{
  g_autoptr (GError) error = NULL;

  if (!meta_dbus_input_capture_session_call_enable_sync (session->proxy,
                                                         NULL, &error))
    g_warning ("Failed to enable session: %s", error->message);
}

static void
input_capture_session_disable (InputCaptureSession *session)
{
  g_autoptr (GError) error = NULL;

  if (!meta_dbus_input_capture_session_call_disable_sync (session->proxy,
                                                          NULL, &error))
    g_warning ("Failed to disable session: %s", error->message);
}

static void
input_capture_session_release (InputCaptureSession *session,
                               double               x,
                               double               y)
{
  g_autoptr (GError) error = NULL;
  GVariant *position;

  position = g_variant_new ("(dd)", x, y);
  if (!meta_dbus_input_capture_session_call_release_sync (session->proxy,
                                                          position,
                                                          NULL, &error))
    g_warning ("Failed to release pointer: %s", error->message);
}

static void
test_sanity (void)
{
  InputCapture *input_capture;
  InputCaptureSession *session;

  input_capture = input_capture_new ();
  session = input_capture_create_session (input_capture);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "*org.freedesktop.DBus.Error.Failed: Session not enabled*");
  input_capture_session_disable (session);
  g_test_assert_expected_messages ();

  input_capture_session_enable (session);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "*org.freedesktop.DBus.Error.Failed: Already enabled*");
  input_capture_session_enable (session);
  g_test_assert_expected_messages ();

  input_capture_session_disable (session);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "*org.freedesktop.DBus.Error.Failed: Session not enabled*");
  input_capture_session_disable (session);
  g_test_assert_expected_messages ();

  input_capture_session_close (session);
}

static void
on_zones_changed (MetaDBusInputCaptureSession *proxy,
                  int                         *zones_changed_count)
{
  *zones_changed_count += 1;
}

static void
assert_zones (GList       *zones,
              const Zone  *expected_zones,
              int          n_expected_zones)
{
  GList *l;
  int i;

  g_assert_cmpuint (g_list_length (zones), ==, n_expected_zones);

  for (l = zones, i = 0; l; l = l->next, i++)
    {
      Zone *zone = l->data;

      g_assert_cmpint (zone->width, ==, expected_zones[i].width);
      g_assert_cmpint (zone->height, ==, expected_zones[i].height);
      g_assert_cmpint (zone->x, ==, expected_zones[i].x);
      g_assert_cmpint (zone->y, ==, expected_zones[i].y);
    }
}

static void
test_zones (void)
{
  InputCapture *input_capture;
  InputCaptureSession *session;
  static const Zone expected_zones1[] = {
    { .width = 800, .height = 600, .x = 0, .y = 0 },
    { .width = 1024, .height = 768, .x = 800, .y = 0 },
  };
  static const Zone expected_zones2[] = {
    { .width = 1024, .height = 768, .x = 0, .y = 0 },
  };
  GList *zones;
  int zones_changed_count = 0;
  unsigned int serial;

  input_capture = input_capture_new ();
  session = input_capture_create_session (input_capture);

  g_signal_connect (session->proxy, "zones-changed",
                    G_CALLBACK (on_zones_changed),
                    &zones_changed_count);

  zones = input_capture_session_get_zones (session);
  assert_zones (zones, expected_zones1, G_N_ELEMENTS (expected_zones1));
  g_clear_list (&zones, g_free);

  write_state (session, "1");

  while (zones_changed_count == 0)
    g_main_context_iteration (NULL, TRUE);

  serial = session->serial;
  g_clear_list (&zones, g_free);

  zones = input_capture_session_get_zones (session);
  g_assert_cmpuint (session->serial, >, serial);
  assert_zones (zones, expected_zones2, G_N_ELEMENTS (expected_zones2));
  g_clear_list (&zones, g_free);

  input_capture_session_close (session);
}

typedef struct
{
  unsigned int activated_barrier_id;
  double activated_x;
  double activated_y;
  unsigned int activated_serial;
} BarriersTestData;

static void
on_activated (MetaDBusInputCaptureSession *proxy,
              unsigned int                 barrier_id,
              unsigned int                 serial,
              GVariant                    *cursor_position,
              BarriersTestData            *data)
{
  g_assert_cmpuint (data->activated_barrier_id, ==, 0);

  data->activated_barrier_id = barrier_id;
  data->activated_serial = serial;
  g_variant_get (cursor_position, "(dd)",
                 &data->activated_x, &data->activated_y);
}

static void
test_barriers (void)
{
  InputCapture *input_capture;
  InputCaptureSession *session;
  g_autolist (Zone) zones = NULL;
  unsigned int barrier1, barrier2;
  BarriersTestData data = {};
  unsigned int prev_activated_serial;

  input_capture = input_capture_new ();
  session = input_capture_create_session (input_capture);

  zones = input_capture_session_get_zones (session);

  /*
   * +-------------+--------------+
   * ||            |              |
   * ||<--B#1      |              |
   * ||            |     B#2      |
   * +-------------+      |       |
   *               |      V       |
   *               +==============+
   */
  barrier1 = input_capture_session_add_barrier (session, 0, 0, 0, 600);
  barrier2 = input_capture_session_add_barrier (session, 800, 768, 1824, 768);

  g_assert_cmpuint (barrier1, !=, 0);
  g_assert_cmpuint (barrier2, !=, 0);
  g_assert_cmpuint (barrier1, !=, barrier2);

  g_signal_connect (session->proxy, "activated",
                    G_CALLBACK (on_activated), &data);

  input_capture_session_enable (session);

  write_state (session, "1");

  while (data.activated_barrier_id == 0)
    g_main_context_iteration (NULL, TRUE);

  g_assert_cmpuint (data.activated_serial, !=, 0);
  g_assert_cmpuint (data.activated_barrier_id, ==, barrier1);
  g_assert_cmpfloat_with_epsilon (data.activated_x, 0.0, DBL_EPSILON);
  g_assert_cmpfloat_with_epsilon (data.activated_y, 15.0, DBL_EPSILON);

  wait_for_state (session, "1");

  input_capture_session_release (session, 200, 150);

  write_state (session, "2");

  prev_activated_serial = data.activated_serial;

  data = (BarriersTestData) {};
  while (data.activated_barrier_id == 0)
    g_main_context_iteration (NULL, TRUE);
  g_assert_cmpuint (data.activated_serial, !=, 0);
  g_assert_cmpuint (data.activated_serial, !=, prev_activated_serial);
  g_assert_cmpuint (data.activated_barrier_id, ==, barrier2);
  g_assert_cmpfloat_with_epsilon (data.activated_x, 1000.0, DBL_EPSILON);
  g_assert_cmpfloat_with_epsilon (data.activated_y, 768.0, DBL_EPSILON);

  input_capture_session_release (session, 1200, 700);
  write_state (session, "3");

  input_capture_session_close (session);
}

static void
test_clear_barriers (void)
{
  InputCapture *input_capture;
  InputCaptureSession *session;
  g_autolist (Zone) zones = NULL;
  BarriersTestData data = {};

  input_capture = input_capture_new ();
  session = input_capture_create_session (input_capture);

  zones = input_capture_session_get_zones (session);

  input_capture_session_add_barrier (session, 0, 0, 0, 600);

  g_signal_connect (session->proxy, "activated",
                    G_CALLBACK (on_activated), &data);

  input_capture_session_enable (session);

  write_state (session, "1");

  while (data.activated_barrier_id == 0)
    g_main_context_iteration (NULL, TRUE);

  input_capture_session_clear_barriers (session);
  write_state (session, "2");
  wait_for_state (session, "1");

  input_capture_session_close (session);
}

static const struct
{
  const char *name;
  void (* func) (void);
} test_cases[] = {
  { "sanity", test_sanity, },
  { "zones", test_zones, },
  { "barriers", test_barriers, },
  { "clear-barriers", test_clear_barriers, },
};

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
  const char *test_case;
  int i;

  g_assert_cmpint (argc, ==, 2);

  test_case = argv[1];

  g_set_print_handler (print_to_stderr);
  g_test_init (&argc, &argv, NULL);

  for (i = 0; i < G_N_ELEMENTS (test_cases); i++)
    {
      if (g_strcmp0 (test_cases[i].name, test_case) == 0)
        {
          g_autoptr (GInputStream) stdin_stream = NULL;

          stdin_stream = g_unix_input_stream_new (fileno (stdin), FALSE);
          stdin_reader = g_data_input_stream_new (stdin_stream);

          test_cases[i].func ();

          g_clear_object (&stdin_reader);
          g_clear_object (&stdin_stream);

          return EXIT_SUCCESS;
        }
    }

  g_warning ("Invalid test case '%s'", test_case);
  return EXIT_FAILURE;
}
