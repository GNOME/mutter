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

static const struct
{
  const char *name;
  void (* func) (void);
} test_cases[] = {
  { "sanity", test_sanity, },
  { "zones", test_zones, },
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
          test_cases[i].func ();
          return EXIT_SUCCESS;
        }
    }

  g_warning ("Invalid test case '%s'", test_case);
  return EXIT_FAILURE;
}
