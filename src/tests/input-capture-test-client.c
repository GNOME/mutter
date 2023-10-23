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

#include <glib.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixfdlist.h>
#include <libei.h>
#include <linux/input.h>
#include <stdio.h>

#include "backends/meta-fd-source.h"

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

typedef struct _Event
{
  enum ei_event_type type;
  struct {
    double dx;
    double dy;
  } motion;
  struct {
    uint32_t button;
    gboolean is_press;
  } button;
  struct {
    uint32_t key;
    gboolean is_press;
  } key;
} Event;

typedef struct _InputCaptureSession
{
  MetaDBusInputCaptureSession *proxy;
  unsigned int serial;

  struct ei *ei;
  GSource *ei_source;

  Event *expected_events;
  int n_expected_events;
  int next_event;

  gboolean has_pointer;
  gboolean has_keyboard;
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

  g_clear_pointer (&session->ei, ei_unref);
  g_clear_pointer (&session->ei_source, g_source_destroy);

  if (!meta_dbus_input_capture_session_call_close_sync (session->proxy,
                                                        NULL, &error))
    g_error ("Failed to close session: %s", error->message);

  g_object_unref (session->proxy);
  g_free (session);
}

static void
record_event (InputCaptureSession *session,
              const Event         *event)
{
  const Event *expected_event;

  g_debug ("Record event #%d, with type %s",
           session->next_event + 1, ei_event_type_to_string (event->type));
  g_assert_nonnull (session->expected_events);
  g_assert_cmpint (session->next_event, <, session->n_expected_events);

  expected_event = &session->expected_events[session->next_event++];

  g_assert_cmpint (expected_event->type, ==, event->type);

  switch (event->type)
    {
    case EI_EVENT_POINTER_MOTION:
      g_assert_cmpfloat_with_epsilon (event->motion.dx,
                                      expected_event->motion.dx,
                                      DBL_EPSILON);
      g_assert_cmpfloat_with_epsilon (event->motion.dy,
                                      expected_event->motion.dy,
                                      DBL_EPSILON);
      break;
    case EI_EVENT_BUTTON_BUTTON:
      g_assert_cmpint (event->button.button, ==, expected_event->button.button);
      break;
    case EI_EVENT_KEYBOARD_KEY:
      g_assert_cmpint (event->key.key, ==, expected_event->key.key);
      break;
    case EI_EVENT_FRAME:
      break;
    default:
      break;
    }
}

static void
process_ei_event (InputCaptureSession *session,
                  struct ei_event     *ei_event)
{
  g_debug ("Processing event %s", ei_event_type_to_string (ei_event_get_type (ei_event)));

  switch (ei_event_get_type (ei_event))
    {
    case EI_EVENT_SEAT_ADDED:
      {
        struct ei_seat *ei_seat = ei_event_get_seat (ei_event);

        g_assert_true (ei_seat_has_capability (ei_seat, EI_DEVICE_CAP_POINTER));
        g_assert_true (ei_seat_has_capability (ei_seat, EI_DEVICE_CAP_KEYBOARD));
        g_assert_true (ei_seat_has_capability (ei_seat, EI_DEVICE_CAP_BUTTON));
        g_assert_true (ei_seat_has_capability (ei_seat, EI_DEVICE_CAP_SCROLL));
        ei_seat_bind_capabilities (ei_seat,
                                   EI_DEVICE_CAP_POINTER,
                                   EI_DEVICE_CAP_BUTTON,
                                   EI_DEVICE_CAP_SCROLL,
                                   EI_DEVICE_CAP_KEYBOARD,
                                   NULL);
        break;
      }
    case EI_EVENT_DEVICE_ADDED:
      {
        struct ei_device *ei_device = ei_event_get_device (ei_event);

        if (ei_device_has_capability (ei_device, EI_DEVICE_CAP_POINTER) &&
            ei_device_has_capability (ei_device, EI_DEVICE_CAP_BUTTON) &&
            ei_device_has_capability (ei_device, EI_DEVICE_CAP_SCROLL))
          session->has_pointer = TRUE;
        if (ei_device_has_capability (ei_device, EI_DEVICE_CAP_KEYBOARD))
          session->has_keyboard = TRUE;
        break;
      }
    case EI_EVENT_DEVICE_REMOVED:
      {
        struct ei_device *ei_device = ei_event_get_device (ei_event);

        if (ei_device_has_capability (ei_device, EI_DEVICE_CAP_POINTER) &&
            ei_device_has_capability (ei_device, EI_DEVICE_CAP_BUTTON) &&
            ei_device_has_capability (ei_device, EI_DEVICE_CAP_SCROLL))
          session->has_pointer = FALSE;
        if (ei_device_has_capability (ei_device, EI_DEVICE_CAP_KEYBOARD))
          session->has_keyboard = FALSE;
        break;
      }
    case EI_EVENT_POINTER_MOTION:
      record_event (session,
                    &(Event) {
                      .type = EI_EVENT_POINTER_MOTION,
                      .motion.dx = ei_event_pointer_get_dx (ei_event),
                      .motion.dy = ei_event_pointer_get_dy (ei_event),
                    });
      break;
    case EI_EVENT_BUTTON_BUTTON:
      record_event (session,
                    &(Event) {
                      .type = EI_EVENT_BUTTON_BUTTON,
                      .button.button = ei_event_button_get_button (ei_event),
                    });
      break;
    case EI_EVENT_KEYBOARD_KEY:
      record_event (session,
                    &(Event) {
                      .type = EI_EVENT_KEYBOARD_KEY,
                      .key.key = ei_event_keyboard_get_key (ei_event),
                    });
      break;
    case EI_EVENT_FRAME:
      record_event (session, &(Event) { .type = EI_EVENT_FRAME });
      break;
    default:
      break;
    }
}

static gboolean
ei_source_prepare (gpointer user_data)
{
  InputCaptureSession *session = user_data;
  struct ei_event *ei_event;
  gboolean retval;

  ei_event = ei_peek_event (session->ei);
  retval = !!ei_event;
  ei_event_unref (ei_event);

  return retval;
}

static gboolean
ei_source_dispatch (gpointer user_data)
{
  InputCaptureSession *session = user_data;

  ei_dispatch (session->ei);

  while (TRUE)
    {
      struct ei_event *ei_event;

      ei_event = ei_get_event (session->ei);
      if (!ei_event)
        break;

      process_ei_event (session, ei_event);
      ei_event_unref (ei_event);
    }

  return G_SOURCE_CONTINUE;
}

static void
set_expected_events (InputCaptureSession *session,
                     Event               *expected_events,
                     int                  n_expected_events)
{
  session->expected_events = expected_events;
  session->n_expected_events = n_expected_events;
  session->next_event = 0;
}

static void
log_handler (struct ei             *ei,
             enum ei_log_priority   priority,
             const char            *message,
             struct ei_log_context *ctx)
{
  int message_length = strlen (message);

  if (priority >= EI_LOG_PRIORITY_ERROR)
    g_critical ("libei: %.*s", message_length, message);
  else if (priority >= EI_LOG_PRIORITY_WARNING)
    g_warning ("libei: %.*s", message_length, message);
  else if (priority >= EI_LOG_PRIORITY_INFO)
    g_info ("libei: %.*s", message_length, message);
  else
    g_debug ("libei: %.*s", message_length, message);
}

static void
input_capture_session_connect_to_eis (InputCaptureSession *session)
{
  g_autoptr (GVariant) fd_variant = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  GError *error = NULL;
  int fd;
  struct ei *ei;
  int ret;

  if (!meta_dbus_input_capture_session_call_connect_to_eis_sync (session->proxy,
                                                                 NULL,
                                                                 &fd_variant,
                                                                 &fd_list,
                                                                 NULL, &error))
    g_error ("Failed to connect to EIS: %s", error->message);

  fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_variant), &error);
  if (fd == -1)
    g_error ("Failed to get EIS file descriptor: %s", error->message);

  ei = ei_new_receiver (session);
  ei_log_set_handler (ei, log_handler);
  ei_log_set_priority (ei, EI_LOG_PRIORITY_DEBUG);

  ret = ei_setup_backend_fd (ei, fd);
  if (ret < 0)
    g_error ("Failed to setup libei backend: %s", g_strerror (errno));

  session->ei = ei;
  session->ei_source = meta_create_fd_source (ei_get_fd (ei),
                                              "libei",
                                              ei_source_prepare,
                                              ei_source_dispatch,
                                              session,
                                              NULL);
  g_source_attach (session->ei_source, NULL);
  g_source_unref (session->ei_source);
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
      g_debug ("Failed to add barrier: %s", error->message);
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
  GVariantBuilder options_builder;

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&options_builder, "{sv}",
                         "cursor_position",
                         g_variant_new ("(dd)", x, y));

  if (!meta_dbus_input_capture_session_call_release_sync (session->proxy,
                                                          g_variant_builder_end (&options_builder),
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
  unsigned int barrier1, barrier2, barrier3, barrier4, barrier5;
  unsigned int invalid_barrier;
  BarriersTestData data = {};
  unsigned int prev_activated_serial;

  input_capture = input_capture_new ();
  session = input_capture_create_session (input_capture);

  zones = input_capture_session_get_zones (session);

  /*
   * +-------------+==============+
   * ||            |        ^     ||
   * ||<--B#1      |        |     ||
   * ||            |   B#2 B#3    || <- B#5
   * +-------------+    |         ||
   *        B#4 -> ||   V         ||
   *               +==============+
   */
  barrier1 = input_capture_session_add_barrier (session, 0, 0, 0, 600);
  barrier2 = input_capture_session_add_barrier (session, 800, 768, 1823, 768);
  barrier3 = input_capture_session_add_barrier (session, 800, 0, 1823, 0);
  barrier4 = input_capture_session_add_barrier (session, 800, 600, 800, 768);
  barrier5 = input_capture_session_add_barrier (session, 1824, 0, 1824, 768);

  g_assert_cmpuint (barrier1, !=, 0);
  g_assert_cmpuint (barrier2, !=, 0);
  g_assert_cmpuint (barrier3, !=, 0);
  g_assert_cmpuint (barrier4, !=, 0);
  g_assert_cmpuint (barrier5, !=, 0);
  g_assert_cmpuint (barrier1, !=, barrier2);
  g_assert_cmpuint (barrier1, !=, barrier3);
  g_assert_cmpuint (barrier1, !=, barrier4);
  g_assert_cmpuint (barrier1, !=, barrier5);
  g_assert_cmpuint (barrier2, !=, barrier3);
  g_assert_cmpuint (barrier2, !=, barrier4);
  g_assert_cmpuint (barrier2, !=, barrier5);
  g_assert_cmpuint (barrier3, !=, barrier4);
  g_assert_cmpuint (barrier3, !=, barrier5);
  g_assert_cmpuint (barrier4, !=, barrier5);

  /* 1px too wide */
  invalid_barrier = input_capture_session_add_barrier (session, 0, 0, 800, 0);
  g_assert_cmpuint (invalid_barrier, ==, 0);
  /* 1px too far south */
  invalid_barrier = input_capture_session_add_barrier (session, 0, 601, 800, 601);
  g_assert_cmpuint (invalid_barrier, ==, 0);
  /* B#3 but 1px past right edge */
  invalid_barrier = input_capture_session_add_barrier (session, 800, 0, 1824, 0);
  g_assert_cmpuint (invalid_barrier, ==, 0);
  /* 1px overlap */
  invalid_barrier = input_capture_session_add_barrier (session, 800, 599, 800, 768);
  g_assert_cmpuint (invalid_barrier, ==, 0);
  /* straight through the middle */
  invalid_barrier = input_capture_session_add_barrier (session, 800, 0, 800, 600);
  g_assert_cmpuint (invalid_barrier, ==, 0);
  /* straight through the middle part 2 */
  invalid_barrier = input_capture_session_add_barrier (session, 800, 0, 800, 768);
  g_assert_cmpuint (invalid_barrier, ==, 0);
  /* B#1 but past the screen size  */
  invalid_barrier = input_capture_session_add_barrier (session, 0, 0, 0, 768);
  g_assert_cmpuint (invalid_barrier, ==, 0);
  /* B#2 but hanging left into the left screen */
  invalid_barrier = input_capture_session_add_barrier (session, 600, 768, 1823, 768);
  g_assert_cmpuint (invalid_barrier, ==, 0);

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

static void
test_cancel_keybinding (void)
{
  InputCapture *input_capture;
  InputCaptureSession *session;
  g_autolist (Zone) zones = NULL;

  input_capture = input_capture_new ();
  session = input_capture_create_session (input_capture);

  zones = input_capture_session_get_zones (session);
  input_capture_session_add_barrier (session, 0, 0, 0, 600);
  input_capture_session_enable (session);

  write_state (session, "1");
  wait_for_state (session, "1");

  input_capture_session_close (session);
}

static void
test_events (void)
{
  InputCapture *input_capture;
  InputCaptureSession *session;
  g_autolist (Zone) zones = NULL;
  Event expected_events[] = {
    /* Move the pointer with deltas (10, 15) and (2, -5), then click */
    {
      .type = EI_EVENT_POINTER_MOTION,
      .motion = { .dx = -10.0, .dy = -10.0 },
    },
    {
      .type = EI_EVENT_FRAME,
    },
    {
      .type = EI_EVENT_POINTER_MOTION,
      .motion = { .dx = 2.0, .dy = -5.0 },
    },
    {
      .type = EI_EVENT_FRAME,
    },
    {
      .type = EI_EVENT_BUTTON_BUTTON,
      .button = { .button = BTN_LEFT, .is_press = TRUE },
    },
    {
      .type = EI_EVENT_FRAME,
    },
    {
      .type = EI_EVENT_BUTTON_BUTTON,
      .button = { .button = BTN_LEFT, .is_press = FALSE },
    },
    {
      .type = EI_EVENT_FRAME,
    },

    /* Press, then release, KEY_A */
    {
      .type = EI_EVENT_KEYBOARD_KEY,
      .key = { .key = KEY_A, .is_press = TRUE },
    },
    {
      .type = EI_EVENT_FRAME,
    },
    {
      .type = EI_EVENT_KEYBOARD_KEY,
      .key = { .key = KEY_A, .is_press = FALSE },
    },
    {
      .type = EI_EVENT_FRAME,
    },
  };

  input_capture = input_capture_new ();
  session = input_capture_create_session (input_capture);

  input_capture_session_connect_to_eis (session);
  zones = input_capture_session_get_zones (session);
  input_capture_session_add_barrier (session, 0, 0, 0, 600);

  input_capture_session_enable (session);

  while (!session->has_pointer ||
         !session->has_keyboard)
    g_main_context_iteration (NULL, TRUE);

  write_state (session, "1");

  set_expected_events (session,
                       expected_events,
                       G_N_ELEMENTS (expected_events));

  while (session->next_event < session->n_expected_events)
    g_main_context_iteration (NULL, TRUE);

  input_capture_session_close (session);
}

static void
test_a11y (void)
{
  InputCapture *input_capture;
  InputCaptureSession *session;
  g_autolist (Zone) zones = NULL;
  Event expected_events[] = {
    {
      .type = EI_EVENT_POINTER_MOTION,
      .motion = { .dx = -10.0, .dy = 0.0 },
    },
    {
      .type = EI_EVENT_FRAME,
    },
    {
      .type = EI_EVENT_BUTTON_BUTTON,
      .button = { .button = BTN_LEFT, .is_press = TRUE },
    },
    {
      .type = EI_EVENT_FRAME,
    },
    {
      .type = EI_EVENT_BUTTON_BUTTON,
      .button = { .button = BTN_LEFT, .is_press = FALSE },
    },
    {
      .type = EI_EVENT_FRAME,
    },
    {
      .type = EI_EVENT_KEYBOARD_KEY,
      .key = { .key = KEY_A, .is_press = TRUE },
    },
    {
      .type = EI_EVENT_FRAME,
    },
    {
      .type = EI_EVENT_KEYBOARD_KEY,
      .key = { .key = KEY_A, .is_press = FALSE },
    },
    {
      .type = EI_EVENT_FRAME,
    },
  };

  input_capture = input_capture_new ();
  session = input_capture_create_session (input_capture);

  input_capture_session_connect_to_eis (session);
  zones = input_capture_session_get_zones (session);
  input_capture_session_add_barrier (session, 0, 0, 0, 600);
  input_capture_session_enable (session);

  set_expected_events (session,
                       expected_events,
                       G_N_ELEMENTS (expected_events));
  write_state (session, "1");

  while (session->next_event < session->n_expected_events)
    g_main_context_iteration (NULL, TRUE);

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
  { "cancel-keybinding", test_cancel_keybinding, },
  { "events", test_events, },
  { "a11y", test_a11y, },
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
