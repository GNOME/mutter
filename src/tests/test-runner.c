/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat, Inc.
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
 */

#include "config.h"

#include <gio/gio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "backends/meta-virtual-monitor.h"
#include "compositor/meta-window-actor-private.h"
#include "core/meta-workspace-manager-private.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "meta/util.h"
#include "meta/window.h"
#include "tests/meta-test-utils.h"
#include "wayland/meta-wayland.h"
#include "x11/meta-x11-display-private.h"

typedef struct {
  MetaContext *context;
  GHashTable *clients;
  MetaX11AlarmFilter *alarm_filter;
  MetaAsyncWaiter *waiter;
  GString *warning_messages;
  GMainLoop *loop;
  gulong x11_display_opened_handler_id;
  GHashTable *virtual_monitors;
  ClutterVirtualInputDevice *pointer;
  GHashTable *cloned_windows;
} TestCase;

static gboolean
test_case_alarm_filter (MetaX11Display        *x11_display,
                        XSyncAlarmNotifyEvent *event,
                        gpointer               data)
{
  TestCase *test = data;

  if (meta_async_waiter_process_x11_event (test->waiter, x11_display, event))
    return TRUE;

  return FALSE;
}

static void
on_x11_display_opened (MetaDisplay *display,
                       TestCase    *test)
{
  MetaX11Display *x11_display = meta_display_get_x11_display (display);

  test->alarm_filter =
    meta_x11_display_add_alarm_filter (x11_display,
                                       test_case_alarm_filter,
                                       test);
  test->waiter = meta_async_waiter_new (display->x11_display);
}

static TestCase *
test_case_new (MetaContext *context)
{
  TestCase *test = g_new0 (TestCase, 1);
  MetaDisplay *display = meta_context_get_display (context);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  MetaVirtualMonitor *monitor;

  if (display->x11_display)
    {
      on_x11_display_opened (display, test);
    }
  else
    {
      test->x11_display_opened_handler_id =
        g_signal_connect (display, "x11-display-opened",
                          G_CALLBACK (on_x11_display_opened),
                          test);
    }

  test->context = context;
  test->clients = g_hash_table_new (g_str_hash, g_str_equal);
  test->loop = g_main_loop_new (NULL, FALSE);
  test->pointer = clutter_seat_create_virtual_device (seat,
                                                      CLUTTER_POINTER_DEVICE);

  test->virtual_monitors = g_hash_table_new_full (g_str_hash,
                                                  g_str_equal,
                                                  g_free,
                                                  g_object_unref);
  monitor = meta_create_test_monitor (context, 800, 600, 60.0);
  g_hash_table_insert (test->virtual_monitors, g_strdup ("default"), monitor);

  return test;
}

static gboolean
test_case_loop_quit (gpointer data)
{
  TestCase *test = data;

  g_main_loop_quit (test->loop);

  return FALSE;
}

static gboolean
test_case_dispatch (TestCase *test,
                    GError  **error)
{
  MetaBackend *backend = meta_context_get_backend (test->context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaDisplay *display = meta_context_get_display (test->context);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  MetaLaters *laters = meta_compositor_get_laters (compositor);

  /* Wait until we've done any outstanding queued up work.
   * Though we add this as BEFORE_REDRAW, the iteration that runs the
   * BEFORE_REDRAW idles will proceed on and do the redraw, so we're
   * waiting until after *all* frame processing.
   */
  meta_laters_add (laters, META_LATER_BEFORE_REDRAW,
                   test_case_loop_quit,
                   test,
                   NULL);

  clutter_stage_schedule_update (CLUTTER_STAGE (stage));
  g_main_loop_run (test->loop);

  return TRUE;
}

static gboolean
test_case_wait (TestCase *test,
                GError  **error)
{
  GHashTableIter iter;
  gpointer key, value;

  /* First have each client set a XSync counter, and wait until
   * we receive the resulting event - so we know we've received
   * everything that the client have sent us.
   */
  g_hash_table_iter_init (&iter, test->clients);
  while (g_hash_table_iter_next (&iter, &key, &value))
    if (!meta_test_client_wait (value, error))
      return FALSE;

  /* Then wait until we've done any outstanding queued up work. */
  test_case_dispatch (test, error);

  /* Then set an XSync counter ourselves and and wait until
   * we receive the resulting event - this makes sure that we've
   * received back any X events we generated.
   */
  if (test->waiter)
    meta_async_waiter_set_and_wait (test->waiter);
  return TRUE;
}

static gboolean
test_case_sleep (TestCase  *test,
                 uint32_t   interval_ms,
                 GError   **error)
{
  g_timeout_add_full (G_PRIORITY_LOW, interval_ms,
                      test_case_loop_quit, test, NULL);
  g_main_loop_run (test->loop);

  return TRUE;
}

#define BAD_COMMAND(...)                                                \
  G_STMT_START {                                                        \
      g_set_error (error,                                               \
                   META_TEST_CLIENT_ERROR,                              \
                   META_TEST_CLIENT_ERROR_BAD_COMMAND,                  \
                   __VA_ARGS__);                                        \
      return FALSE;                                                     \
  } G_STMT_END

static MetaTestClient *
test_case_lookup_client (TestCase *test,
                         char     *client_id,
                         GError  **error)
{
  MetaTestClient *client = g_hash_table_lookup (test->clients, client_id);
  if (!client)
    {
      g_set_error (error,
                   META_TEST_CLIENT_ERROR,
                   META_TEST_CLIENT_ERROR_BAD_COMMAND,
                   "No such client %s", client_id);
    }

  return client;
}

static gboolean
test_case_parse_window_id (TestCase        *test,
                           const char      *client_and_window_id,
                           MetaTestClient **client,
                           const char     **window_id,
                           GError         **error)
{
  const char *slash = strchr (client_and_window_id, '/');
  char *tmp;
  if (slash == NULL)
    BAD_COMMAND ("client/window ID %s doesn't contain a /", client_and_window_id);

  *window_id = slash + 1;

  tmp = g_strndup (client_and_window_id, slash - client_and_window_id);
  *client = test_case_lookup_client (test, tmp, error);
  g_free (tmp);

  return client != NULL;
}

static gboolean
test_case_assert_stacking (TestCase       *test,
                           char          **expected_windows,
                           int             n_expected_windows,
                           MetaWorkspace  *workspace,
                           GError        **error)
{
  MetaDisplay *display = meta_context_get_display (test->context);
  guint64 *windows;
  int n_windows;
  GString *stack_string = g_string_new (NULL);
  GString *expected_string = g_string_new (NULL);
  int i;

  meta_stack_tracker_get_stack (display->stack_tracker, &windows, &n_windows);
  for (i = 0; i < n_windows; i++)
    {
      MetaWindow *window = meta_display_lookup_stack_id (display, windows[i]);

      if (workspace && !meta_window_located_on_workspace (window, workspace))
        continue;

      if (window != NULL && window->title)
        {
          if (stack_string->len > 0)
            g_string_append_c (stack_string, ' ');

          if (g_str_has_prefix (window->title, "test/"))
            g_string_append (stack_string, window->title + 5);
          else
            g_string_append_printf (stack_string, "(%s)", window->title);
        }
      else if (windows[i] == display->x11_display->guard_window)
        {
          if (stack_string->len > 0)
            g_string_append_c (stack_string, ' ');

          g_string_append_c (stack_string, '|');
        }
    }

  for (i = 0; i < n_expected_windows; i++)
    {
      if (expected_string->len > 0)
        g_string_append_c (expected_string, ' ');

      g_string_append (expected_string, expected_windows[i]);
    }

  /* Don't require '| ' as a prefix if there are no hidden windows - we
   * remove the prefix from the actual string instead of adding it to the
   * expected string for clarity of the error message
   */
  if (index (expected_string->str, '|') == NULL && stack_string->str[0] == '|')
    {
      g_string_erase (stack_string,
                      0, stack_string->str[1] == ' ' ? 2 : 1);
    }

  if (strcmp (expected_string->str, stack_string->str) != 0)
    {
      g_set_error (error,
                   META_TEST_CLIENT_ERROR,
                   META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                   "stacking: expected='%s', actual='%s'",
                   expected_string->str, stack_string->str);
    }

  g_string_free (stack_string, TRUE);
  g_string_free (expected_string, TRUE);

  return *error == NULL;
}

static gboolean
test_case_assert_focused (TestCase    *test,
                          const char  *expected_window,
                          GError     **error)
{
  MetaDisplay *display = meta_context_get_display (test->context);

  if (!display->focus_window)
    {
      if (g_strcmp0 (expected_window, "none") != 0)
        {
          g_set_error (error,
                       META_TEST_CLIENT_ERROR,
                       META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                       "focus: expected='%s', actual='none'", expected_window);
        }
    }
  else
    {
      const char *focused = display->focus_window->title;

      if (g_str_has_prefix (focused, "test/"))
        focused += 5;

      if (g_strcmp0 (focused, expected_window) != 0)
        g_set_error (error,
                     META_TEST_CLIENT_ERROR,
                     META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                     "focus: expected='%s', actual='%s'",
                     expected_window, focused);
    }

  return *error == NULL;
}

static gboolean
test_case_assert_size (TestCase    *test,
                       MetaWindow  *window,
                       int          expected_width,
                       int          expected_height,
                       GError     **error)
{
  MtkRectangle frame_rect;

  meta_window_get_frame_rect (window, &frame_rect);

  if (frame_rect.width != expected_width ||
      frame_rect.height != expected_height)
    {
      g_set_error (error,
                   META_TEST_CLIENT_ERROR,
                   META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                   "Expected size %dx%d didn't match actual size %dx%d",
                   expected_width, expected_height,
                   frame_rect.width, frame_rect.height);
      return FALSE;
    }

  return TRUE;
}

static gboolean
test_case_check_xserver_stacking (TestCase *test,
                                  GError  **error)
{
  MetaDisplay *display = meta_context_get_display (test->context);
  GString *local_string = g_string_new (NULL);
  GString *x11_string = g_string_new (NULL);
  int i;

  if (!display->x11_display)
    return TRUE;

  guint64 *windows;
  int n_windows;
  meta_stack_tracker_get_stack (display->stack_tracker, &windows, &n_windows);

  for (i = 0; i < n_windows; i++)
    {
      if (META_STACK_ID_IS_X11 (windows[i]))
        {
          if (local_string->len > 0)
            g_string_append_c (local_string, ' ');

          g_string_append_printf (local_string, "%#lx", (Window)windows[i]);
        }
    }

  Window root;
  Window parent;
  Window *children;
  unsigned int n_children;
  XQueryTree (display->x11_display->xdisplay,
              display->x11_display->xroot,
              &root, &parent, &children, &n_children);

  for (i = 0; i < (int)n_children; i++)
    {
      if (x11_string->len > 0)
        g_string_append_c (x11_string, ' ');

      g_string_append_printf (x11_string, "%#lx", (Window)children[i]);
    }

  if (strcmp (x11_string->str, local_string->str) != 0)
    g_set_error (error,
                 META_TEST_CLIENT_ERROR,
                 META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                 "xserver stacking: x11='%s', local='%s'",
                 x11_string->str, local_string->str);

  XFree (children);

  g_string_free (local_string, TRUE);
  g_string_free (x11_string, TRUE);

  return *error == NULL;
}

static int
maybe_divide (const char *str,
              int         value)
{
  if (strstr (str, "/") == str)
    {
      int divisor;

      str += 1;
      divisor = atoi (str);

      value /= divisor;
    }

  return value;
}

static int
parse_window_size (MetaWindow *window,
                   const char *size_str)
{
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle logical_monitor_layout;
  int value;

  logical_monitor = meta_window_find_monitor_from_frame_rect (window);
  g_assert_nonnull (logical_monitor);

  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);

  if (strstr (size_str, "MONITOR_WIDTH") == size_str)
    {
      value = logical_monitor_layout.width;
      size_str += strlen ("MONITOR_WIDTH");
      value = maybe_divide (size_str, value);
    }
  else if (strstr (size_str, "MONITOR_HEIGHT") == size_str)
    {
      value = logical_monitor_layout.height;
      size_str += strlen ("MONITOR_HEIGHT");
      value = maybe_divide (size_str, value);
    }
  else
    {
      value = atoi (size_str);
    }

  return value;
}

static gboolean
str_to_bool (const char *str,
             gboolean   *val)
{
  if (g_ascii_strcasecmp (str, "true") == 0) {
    if (val != NULL)
      *val = true;
    return TRUE;
  }

  if (g_ascii_strcasecmp (str, "false") == 0) {
    if (val != NULL)
      *val = false;
    return TRUE;
  }

  return FALSE;
}

static gboolean
test_case_do (TestCase    *test,
              const char  *filename,
              int          line_no,
              int          argc,
              char       **argv,
              GError     **error)
{
  g_autofree char *command = NULL;

  command = g_strjoinv (" ", argv);
  g_debug ("%s:%d: '%s'", filename, line_no, command);

  if (strcmp (argv[0], "new_client") == 0)
    {
      MetaWindowClientType type;
      MetaTestClient *client;

      if (argc != 3)
        BAD_COMMAND("usage: new_client <client-id> [wayland|x11]");

      if (strcmp (argv[2], "x11") == 0)
        type = META_WINDOW_CLIENT_TYPE_X11;
      else if (strcmp (argv[2], "wayland") == 0)
        type = META_WINDOW_CLIENT_TYPE_WAYLAND;
      else
        BAD_COMMAND("usage: new_client <client-id> [wayland|x11]");

      if (g_hash_table_lookup (test->clients, argv[1]))
        BAD_COMMAND("client %s already exists", argv[1]);

      client = meta_test_client_new (test->context, argv[1], type, error);
      if (!client)
        return FALSE;

      g_hash_table_insert (test->clients, meta_test_client_get_id (client), client);
    }
  else if (strcmp (argv[0], "quit_client") == 0)
    {
      if (argc != 2)
        BAD_COMMAND("usage: quit_client <client-id>");

      MetaTestClient *client = test_case_lookup_client (test, argv[1], error);
      if (!client)
        return FALSE;

      if (!meta_test_client_quit (client, error))
        return FALSE;

      g_hash_table_remove (test->clients, meta_test_client_get_id (client));
      meta_test_client_destroy (client);
    }
  else if (strcmp (argv[0], "create") == 0)
    {
      if (!(argc == 2 ||
            (argc == 3 && strcmp (argv[2], "override") == 0) ||
            (argc == 3 && strcmp (argv[2], "csd") == 0)))
        BAD_COMMAND("usage: %s <client-id>/<window-id > [override|csd]", argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!meta_test_client_do (client, error,
                                "create", window_id,
                                argc == 3 ? argv[2] : NULL,
                                NULL))
        return FALSE;

      if (!meta_test_client_wait (client, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "set_parent") == 0 ||
           strcmp (argv[0], "set_parent_exported") == 0)
    {
      if (argc != 3)
        BAD_COMMAND("usage: %s <client-id>/<window-id> <parent-window-id>",
                    argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!meta_test_client_do (client, error,
                                argv[0], window_id,
                                argv[2],
                                NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "accept_focus") == 0)
    {
      if (argc != 3 || !str_to_bool (argv[2], NULL))
        BAD_COMMAND("usage: %s <client-id>/<window-id> [true|false]",
                    argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!meta_test_client_do (client, error,
                                argv[0], window_id,
                                argv[2],
                                NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "can_take_focus") == 0)
    {
      if (argc != 3 || !str_to_bool (argv[2], NULL))
        BAD_COMMAND("usage: %s <client-id>/<window-id> [true|false]",
                    argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!meta_test_client_do (client, error,
                                argv[0], window_id,
                                argv[2],
                                NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "accept_take_focus") == 0)
    {
      if (argc != 3 || !str_to_bool (argv[2], NULL))
        BAD_COMMAND("usage: %s <client-id>/<window-id> [true|false]",
                    argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!meta_test_client_do (client, error,
                                argv[0], window_id,
                                argv[2],
                                NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "show") == 0)
    {
      MetaWindow *window;
      gboolean show_async = FALSE;

      if (argc != 2 && argc != 3)
        BAD_COMMAND("usage: %s <client-id>/<window-id> [async]", argv[0]);

      if (argc == 3 && strcmp (argv[2], "async") == 0)
        show_async = TRUE;

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!meta_test_client_do (client, error, argv[0], window_id, NULL))
        return FALSE;

      if (!test_case_wait (test, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      if (!show_async)
        meta_test_client_wait_for_window_shown (client, window);
    }
  else if (strcmp (argv[0], "sync_shown") == 0)
    {
      MetaWindow *window;
      MetaTestClient *client;
      const char *window_id;

      if (argc != 2)
        BAD_COMMAND("usage: %s <client-id>/<window-id>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      meta_test_client_wait_for_window_shown (client, window);
    }
  else if (strcmp (argv[0], "resize") == 0)
    {
      if (argc != 4)
        BAD_COMMAND("usage: %s <client-id>/<window-id> width height", argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!meta_test_client_do (client, error, argv[0], window_id,
                                argv[2], argv[3], NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "move") == 0)
    {
      MetaWindow *window;

      if (argc != 4)
        BAD_COMMAND("usage: %s <client-id>/<window-id> x y", argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      meta_window_move_frame (window, TRUE, atoi (argv[2]), atoi (argv[3]));
    }
  else if (strcmp (argv[0], "tile") == 0)
    {
      MetaWindow *window;

      if (argc != 3)
        BAD_COMMAND("usage: %s <client-id>/<window-id> [right|left]", argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      MetaTileMode tile_mode;
      if (strcmp (argv[2], "right") == 0)
        {
          tile_mode = META_TILE_RIGHT;
        }
      else if (strcmp (argv[2], "left") == 0)
        {
          tile_mode = META_TILE_LEFT;
        }
      else
        {
          g_set_error (error,
                       META_TEST_CLIENT_ERROR,
                       META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                       "Invalid tile mode '%s'", argv[2]);
          return FALSE;
        }

      meta_window_tile (window, tile_mode);
    }
  else if (strcmp (argv[0], "untile") == 0)
    {
      MetaWindow *window;

      if (argc != 2)
        BAD_COMMAND("usage: %s <client-id>/<window-id>", argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      meta_window_untile (window);
    }
  else if (strcmp (argv[0], "hide") == 0 ||
           strcmp (argv[0], "activate") == 0 ||
           strcmp (argv[0], "raise") == 0 ||
           strcmp (argv[0], "lower") == 0 ||
           strcmp (argv[0], "minimize") == 0 ||
           strcmp (argv[0], "unminimize") == 0 ||
           strcmp (argv[0], "maximize") == 0 ||
           strcmp (argv[0], "unmaximize") == 0 ||
           strcmp (argv[0], "fullscreen") == 0 ||
           strcmp (argv[0], "unfullscreen") == 0 ||
           strcmp (argv[0], "freeze") == 0 ||
           strcmp (argv[0], "thaw") == 0 ||
           strcmp (argv[0], "destroy") == 0)
    {
      if (argc != 2)
        BAD_COMMAND("usage: %s <client-id>/<window-id>", argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!meta_test_client_do (client, error, argv[0], window_id, NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "local_activate") == 0)
    {
      MetaWindow *window;

      if (argc != 2)
        BAD_COMMAND("usage: %s <client-id>/<window-id>", argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      meta_window_activate (window, 0);
    }
  else if (strcmp (argv[0], "wait") == 0)
    {
      if (argc != 1)
        BAD_COMMAND("usage: %s", argv[0]);

      if (!test_case_wait (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "wait_reconfigure") == 0)
    {
      if (argc != 1)
        BAD_COMMAND("usage: %s", argv[0]);

      /*
       * Wait twice, so that we
       *  1) First wait for any requests to configure being made
       *  2) Then wait until the new configuration has been applied
       */

      if (!test_case_wait (test, error))
        return FALSE;
      if (!test_case_dispatch (test, error))
        return FALSE;
      if (!test_case_wait (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "dispatch") == 0)
    {
      if (argc != 1)
        BAD_COMMAND("usage: %s", argv[0]);

      if (!test_case_dispatch (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "sleep") == 0)
    {
      uint64_t interval_ms;

      if (argc != 2)
        BAD_COMMAND("usage: %s <milliseconds>|<known-time>", argv[0]);

      if (strcmp (argv[1], "suspend_timeout") == 0)
        interval_ms = s2ms (meta_get_window_suspend_timeout_s ());
      else if (!g_ascii_string_to_unsigned (argv[1], 10, 0, G_MAXUINT32,
                                            &interval_ms, error))
        return FALSE;

      if (!test_case_sleep (test, (uint32_t) interval_ms, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "set_strut") == 0)
    {
      if (argc != 6)
        BAD_COMMAND("usage: %s <x> <y> <width> <height> <side>", argv[0]);

      int x = atoi (argv[1]);
      int y = atoi (argv[2]);
      int width = atoi (argv[3]);
      int height = atoi (argv[4]);

      MetaSide side;
      if (strcmp (argv[5], "left") == 0)
        side = META_SIDE_LEFT;
      else if (strcmp (argv[5], "right") == 0)
        side = META_SIDE_RIGHT;
      else if (strcmp (argv[5], "top") == 0)
        side = META_SIDE_TOP;
      else if (strcmp (argv[5], "bottom") == 0)
        side = META_SIDE_BOTTOM;
      else
        return FALSE;

      MetaDisplay *display = meta_context_get_display (test->context);
      MetaWorkspaceManager *workspace_manager =
        meta_display_get_workspace_manager (display);
      MtkRectangle rect = { x, y, width, height };
      MetaStrut strut = { rect, side };
      GSList *struts = g_slist_append (NULL, &strut);
      GList *workspaces =
        meta_workspace_manager_get_workspaces (workspace_manager);
      GList *l;

      for (l = workspaces; l; l = l->next)
        {
          MetaWorkspace *workspace = l->data;
          meta_workspace_set_builtin_struts (workspace, struts);
        }

      g_slist_free (struts);
    }
  else if (strcmp (argv[0], "clear_struts") == 0)
    {
      if (argc != 1)
        BAD_COMMAND("usage: %s", argv[0]);

      MetaDisplay *display = meta_context_get_display (test->context);
      MetaWorkspaceManager *workspace_manager =
        meta_display_get_workspace_manager (display);
      GList *workspaces =
        meta_workspace_manager_get_workspaces (workspace_manager);
      GList *l;

      for (l = workspaces; l; l = l->next)
        {
          MetaWorkspace *workspace = l->data;
          meta_workspace_set_builtin_struts (workspace, NULL);
        }
    }
  else if (strcmp (argv[0], "assert_stacking") == 0)
    {
      if (!test_case_assert_stacking (test, argv + 1, argc - 1, NULL, error))
        return FALSE;

      if (!test_case_check_xserver_stacking (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "assert_focused") == 0)
    {
      if (!test_case_assert_focused (test, argv[1], error))
        return FALSE;
    }
  else if (strcmp (argv[0], "assert_size") == 0)
    {
      MetaWindow *window;

      if (argc != 4)
        {
          BAD_COMMAND("usage: %s <client-id>/<window-id> <width> <height>",
                      argv[0]);
        }

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      if (meta_window_get_frame (window))
        {
          g_set_error (error,
                       META_TEST_CLIENT_ERROR,
                       META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                       "Can only assert size of CSD window");
          return FALSE;
        }

      int width = parse_window_size (window, argv[2]);
      int height = parse_window_size (window, argv[3]);
      g_autofree char *width_str = g_strdup_printf ("%d", width);
      g_autofree char *height_str = g_strdup_printf ("%d", height);

      if (!meta_test_client_do (client, error, argv[0],
                                window_id,
                                width_str,
                                height_str,
                                NULL))
        return FALSE;

      if (!test_case_assert_size (test, window,
                                  width, height,
                                  error))
        return FALSE;
    }
  else if (strcmp (argv[0], "assert_position") == 0)
    {
      MetaWindow *window;

      if (argc != 4)
        {
          BAD_COMMAND("usage: %s <client-id>/<window-id> <x> <y>",
                      argv[0]);
        }

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      MtkRectangle frame_rect;
      meta_window_get_frame_rect (window, &frame_rect);
      int x = atoi (argv[2]);
      int y = atoi (argv[3]);
      if (frame_rect.x != x || frame_rect.y != y)
        {
          g_set_error (error,
                       META_TEST_CLIENT_ERROR,
                       META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                       "Expected window position (%d, %d) doesn't match (%d, %d)",
                       x, y, frame_rect.x, frame_rect.y);
          return FALSE;
        }
    }
  else if (strcmp (argv[0], "stop_after_next") == 0 ||
           strcmp (argv[0], "continue") == 0)
    {
      if (argc != 2)
        BAD_COMMAND("usage: %s <client-id>", argv[0]);

      MetaTestClient *client = test_case_lookup_client (test, argv[1], error);
      if (!client)
        return FALSE;

      if (!meta_test_client_do (client, error, argv[0], NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "clipboard-set") == 0)
    {
      if (argc != 4)
        BAD_COMMAND("usage: %s <client-id> <mimetype> <text>", argv[0]);

      MetaTestClient *client = test_case_lookup_client (test, argv[1], error);
      if (!client)
        return FALSE;

      if (!meta_test_client_do (client, error, argv[0], argv[2], argv[3], NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "resize_monitor") == 0)
    {
      MetaBackend *backend = meta_context_get_backend (test->context);
      MetaMonitorManager *monitor_manager =
        meta_backend_get_monitor_manager (backend);
      MetaCrtcMode *crtc_mode;
      const MetaCrtcModeInfo *crtc_mode_info;
      MetaVirtualMonitor *monitor;

      if (argc != 4)
        BAD_COMMAND ("usage: %s <monitor-id> <width> <height>", argv[0]);

      monitor = g_hash_table_lookup (test->virtual_monitors, argv[1]);
      if (!monitor)
        BAD_COMMAND ("Unknown monitor %s", argv[1]);

      crtc_mode = meta_virtual_monitor_get_crtc_mode (monitor);
      crtc_mode_info = meta_crtc_mode_get_info (crtc_mode);
      meta_virtual_monitor_set_mode (monitor,
                                     atoi (argv[2]),
                                     atoi (argv[3]),
                                     crtc_mode_info->refresh_rate);
      meta_monitor_manager_reload (monitor_manager);
    }
  else if (strcmp (argv[0], "add_monitor") == 0)
    {
      MetaBackend *backend = meta_context_get_backend (test->context);
      MetaMonitorManager *monitor_manager =
        meta_backend_get_monitor_manager (backend);
      MetaVirtualMonitor *monitor;
      int width, height;

      if (argc != 4)
        BAD_COMMAND ("usage: %s <monitor-id> <width> <height>", argv[0]);

      width = atoi (argv[2]);
      height = atoi (argv[3]);

      monitor = meta_create_test_monitor (test->context, width, height, 60.0);
      meta_monitor_manager_reload (monitor_manager);

      g_hash_table_insert (test->virtual_monitors, g_strdup (argv[1]), monitor);
    }
  else if (strcmp (argv[0], "assert_primary_monitor") == 0)
    {
      MetaVirtualMonitor *virtual_monitor;
      MetaOutput *output;
      MetaMonitor *monitor;

      if (argc != 2)
        BAD_COMMAND ("usage: %s <monitor-id>", argv[0]);

      virtual_monitor = g_hash_table_lookup (test->virtual_monitors, argv[1]);
      if (!virtual_monitor)
        BAD_COMMAND ("Unknown monitor %s", argv[1]);

      output = meta_virtual_monitor_get_output (virtual_monitor);
      monitor = meta_output_get_monitor (output);

      if (!meta_monitor_is_primary (monitor))
        {
          g_set_error (error,
                       META_TEST_CLIENT_ERROR,
                       META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                       "Monitor %s is not the primary monitor", argv[1]);
          return FALSE;
        }
    }
  else if (strcmp (argv[0], "num_workspaces") == 0)
    {
      if (argc != 2)
        BAD_COMMAND("usage: %s <num>", argv[0]);

      MetaDisplay *display = meta_context_get_display (test->context);
      MetaWorkspaceManager *workspace_manager =
        meta_display_get_workspace_manager (display);
      uint32_t timestamp = meta_display_get_current_time_roundtrip (display);
      int num = atoi (argv[1]);
      meta_workspace_manager_update_num_workspaces (workspace_manager,
                                                    timestamp, num);
    }
  else if (strcmp (argv[0], "activate_workspace") == 0)
    {
      if (argc != 2)
        BAD_COMMAND("usage: %s <workspace-index>", argv[0]);

      MetaDisplay *display = meta_context_get_display (test->context);
      MetaWorkspaceManager *workspace_manager =
        meta_display_get_workspace_manager (display);

      int index = atoi (argv[1]);
      if (index >= meta_workspace_manager_get_n_workspaces (workspace_manager))
        return FALSE;

      MetaWorkspace *workspace =
        meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                       index);
      uint32_t timestamp = meta_display_get_current_time_roundtrip (display);
      meta_workspace_activate (workspace, timestamp);
    }
  else if (strcmp (argv[0], "activate_workspace_with_focus") == 0)
    {
      if (argc != 3)
        BAD_COMMAND("usage: %s <workspace-index> <window-id>", argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[2], &client, &window_id, error))
        return FALSE;

      MetaWindow *window;
      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      MetaDisplay *display = meta_context_get_display (test->context);
      MetaWorkspaceManager *workspace_manager =
        meta_display_get_workspace_manager (display);

      int index = atoi (argv[1]);
      if (index >= meta_workspace_manager_get_n_workspaces (workspace_manager))
        return FALSE;

      MetaWorkspace *workspace =
        meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                       index);
      uint32_t timestamp = meta_display_get_current_time_roundtrip (display);
      meta_workspace_activate_with_focus (workspace, window, timestamp);
    }
  else if (strcmp (argv[0], "assert_stacking_workspace") == 0)
    {
      if (argc < 2)
        BAD_COMMAND("usage: %s <workspace-index> [<window-id1> ...]", argv[0]);

      MetaDisplay *display = meta_context_get_display (test->context);
      MetaWorkspaceManager *workspace_manager =
        meta_display_get_workspace_manager (display);

      int index = atoi (argv[1]);
      if (index >= meta_workspace_manager_get_n_workspaces (workspace_manager))
        return FALSE;

      MetaWorkspace *workspace =
        meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                       index);

      if (!test_case_assert_stacking (test, argv + 2, argc - 2, workspace, error))
        return FALSE;

      if (!test_case_check_xserver_stacking (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "window_to_workspace") == 0)
    {
      if (argc != 3)
        BAD_COMMAND("usage: %s <window-id> <workspace-index>", argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      MetaWindow *window;
      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      MetaDisplay *display = meta_context_get_display (test->context);
      MetaWorkspaceManager *workspace_manager =
        meta_display_get_workspace_manager (display);

      int index = atoi (argv[2]);
      if (index >= meta_workspace_manager_get_n_workspaces (workspace_manager))
        return FALSE;

      MetaWorkspace *workspace =
        meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                       index);

      meta_window_change_workspace (window, workspace);
    }
  else if (strcmp (argv[0], "make_above") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;
      gboolean make_above;

      if (argc != 3 || !str_to_bool (argv[2], &make_above))
        BAD_COMMAND("usage: %s <client-id>/<window-id> [true|false]",
                    argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      if (make_above)
        meta_window_make_above (window);
      else
        meta_window_unmake_above (window);
    }
  else if (strcmp (argv[0], "focus_default_window") == 0)
    {
      if (argc != 1)
        BAD_COMMAND("usage: %s", argv[0]);

      MetaDisplay *display = meta_context_get_display (test->context);
      uint32_t timestamp = meta_display_get_current_time_roundtrip (display);

      meta_display_focus_default_window (display, timestamp);
    }
  else if (strcmp (argv[0], "move_cursor_to") == 0)
    {
      if (argc != 3)
        BAD_COMMAND("usage: %s <x> <y>", argv[0]);

      float x = atof (argv[1]);
      float y = atof (argv[2]);

      clutter_virtual_input_device_notify_absolute_motion (test->pointer,
                                                           CLUTTER_CURRENT_TIME,
                                                           x, y);
      meta_flush_input (test->context);
    }
  else if (strcmp (argv[0], "click") == 0)
    {
      if (argc != 1)
        BAD_COMMAND("usage: %s", argv[0]);

      clutter_virtual_input_device_notify_button (test->pointer,
                                                  CLUTTER_CURRENT_TIME,
                                                  CLUTTER_BUTTON_PRIMARY,
                                                  CLUTTER_BUTTON_STATE_PRESSED);
      clutter_virtual_input_device_notify_button (test->pointer,
                                                  CLUTTER_CURRENT_TIME,
                                                  CLUTTER_BUTTON_PRIMARY,
                                                  CLUTTER_BUTTON_STATE_RELEASED);
      meta_flush_input (test->context);
    }
  else if (strcmp (argv[0], "set_pref") == 0)
    {
      GSettings *wm;
      GSettings *mutter;

      if (argc != 3)
        BAD_COMMAND("usage: %s <KEY> <VALUE>", argv[0]);

      wm = g_settings_new ("org.gnome.desktop.wm.preferences");
      g_assert_nonnull (wm);
      mutter = g_settings_new ("org.gnome.mutter");
      g_assert_nonnull (mutter);

      if (strcmp (argv[1], "raise-on-click") == 0)
        {
          gboolean value;
          if (!str_to_bool (argv[2], &value))
            BAD_COMMAND("usage: %s %s [true|false]", argv[0], argv[1]);

          g_assert_true (g_settings_set_boolean (wm, "raise-on-click", value));
        }
      else if (strcmp (argv[1], "focus-mode") == 0)
        {
          GDesktopFocusMode mode;

          if (g_ascii_strcasecmp (argv[2], "click") == 0)
            mode = G_DESKTOP_FOCUS_MODE_CLICK;
          else if (g_ascii_strcasecmp (argv[2], "sloppy") == 0)
            mode = G_DESKTOP_FOCUS_MODE_SLOPPY;
          else if (g_ascii_strcasecmp (argv[2], "mouse") == 0)
            mode = G_DESKTOP_FOCUS_MODE_MOUSE;
          else
            BAD_COMMAND("usage: %s %s [click|sloppy|mouse]", argv[0], argv[1]);

          g_assert_true (g_settings_set_enum (wm, "focus-mode", mode));
        }
      else if (strcmp (argv[1], "workspaces-only-on-primary") == 0)
        {
          gboolean value;
          if (!str_to_bool (argv[2], &value))
            BAD_COMMAND("usage: %s %s [true|false]", argv[0], argv[1]);

          g_assert_true (g_settings_set_boolean (mutter, "workspaces-only-on-primary", value));
        }
      else if (strcmp (argv[1], "focus-change-on-pointer-rest") == 0)
        {
          gboolean value;
          if (!str_to_bool (argv[2], &value))
            BAD_COMMAND("usage: %s %s [true|false]", argv[0], argv[1]);

          g_assert_true (g_settings_set_boolean (mutter, "focus-change-on-pointer-rest", value));
        }
      else if (strcmp (argv[1], "auto-raise") == 0)
        {
          gboolean value;
          if (!str_to_bool (argv[2], &value))
            BAD_COMMAND("usage: %s %s [true|false]", argv[0], argv[1]);

          g_assert_true (g_settings_set_boolean (wm, "auto-raise", value));
        }
      else if (strcmp (argv[1], "auto-raise-delay") == 0)
        {
          int value = atoi (argv[2]);

          g_assert_true (g_settings_set_int (wm, "auto-raise-delay", value));
        }
      else {
        BAD_COMMAND("Unknown preference %s", argv[1]);
      }
    }
  else if (strcmp (argv[0], "toggle_overview") == 0)
    {
      MetaDisplay *display = meta_context_get_display (test->context);

      if (argc != 1)
        BAD_COMMAND ("usage: %s", argv[0]);

      g_signal_emit_by_name (display, "overlay-key", 0);
    }
  else if (strcmp (argv[0], "clone") == 0)
    {
      MetaBackend *backend = meta_context_get_backend (test->context);
      ClutterActor *stage = meta_backend_get_stage (backend);
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;
      MetaWindowActor *window_actor;
      ClutterActor *clone;

      if (argc != 2)
        BAD_COMMAND("usage: %s <client-id>/<window-id>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      if (g_object_get_data (G_OBJECT (window), "test-clone"))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Already cloned");
          return FALSE;
        }

      window_actor = meta_window_actor_from_window (window);
      clone = clutter_clone_new (CLUTTER_ACTOR (window_actor));
      clutter_actor_show (clone);

      clutter_actor_add_child (stage, clone);
      g_object_set_data (G_OBJECT (window), "test-clone", clone);

      if (!test->cloned_windows)
        {
          test->cloned_windows = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                        g_free, g_object_unref);
        }

      g_hash_table_insert (test->cloned_windows,
                           g_strdup (argv[1]), g_object_ref (window));
    }
  else if (strcmp (argv[0], "declone") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *live_window;
      MetaWindow *window;
      ClutterActor *clone;

      if (argc != 2)
        BAD_COMMAND("usage: %s <client-id>/<window-id>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = g_hash_table_lookup (test->cloned_windows, argv[1]);
      g_assert_nonnull (window);

      live_window = meta_test_client_find_window (client, window_id, NULL);
      if (live_window)
        g_assert_true (live_window == window);

      if (!g_object_get_data (G_OBJECT (window), "test-clone"))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Wasn't cloned");
          return FALSE;
        }

      clone = g_object_get_data (G_OBJECT (window), "test-clone");
      clutter_actor_destroy (clone);

      g_hash_table_remove (test->cloned_windows, argv[1]);
    }
  else if (strcmp (argv[0], "wait_for_effects") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;
      MetaWindowActor *window_actor;

      if (argc != 2)
        BAD_COMMAND("usage: %s <client-id>/<window-id>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      window_actor = meta_window_actor_from_window (window);
      g_object_add_weak_pointer (G_OBJECT (window_actor),
                                 (gpointer *) &window_actor);
      while (window_actor && meta_window_actor_effect_in_progress (window_actor))
        g_main_context_iteration (NULL, TRUE);
      if (window_actor)
        {
          g_object_remove_weak_pointer (G_OBJECT (window_actor),
                                        (gpointer *) &window_actor);
        }
    }
  else
    {
      BAD_COMMAND("Unknown command %s", argv[0]);
    }

  return TRUE;
}

static gboolean
test_case_destroy (TestCase *test,
                   GError  **error)
{
  /* Failures when cleaning up the test case aren't recoverable, since we'll
   * pollute the subsequent test cases, so we just return the error, and
   * skip the rest of the cleanup.
   */
  GHashTableIter iter;
  gpointer key, value;
  MetaDisplay *display;

  if (test->cloned_windows)
    {
      g_assert_cmpuint (g_hash_table_size (test->cloned_windows), ==, 0);
      g_hash_table_unref (test->cloned_windows);
    }

  g_hash_table_iter_init (&iter, test->clients);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (!meta_test_client_do (value, error, "destroy_all", NULL))
        return FALSE;

    }

  if (!test_case_wait (test, error))
    return FALSE;

  if (!test_case_assert_stacking (test, NULL, 0, NULL, error))
    return FALSE;

  g_hash_table_iter_init (&iter, test->clients);
  while (g_hash_table_iter_next (&iter, &key, &value))
    meta_test_client_destroy (value);

  g_clear_pointer (&test->waiter, meta_async_waiter_destroy);

  display = meta_context_get_display (test->context);
  g_clear_signal_handler (&test->x11_display_opened_handler_id, display);
  if (display->x11_display && test->alarm_filter)
    {
      meta_x11_display_remove_alarm_filter (display->x11_display,
                                            test->alarm_filter);
    }

  g_hash_table_destroy (test->clients);
  g_hash_table_unref (test->virtual_monitors);
  g_object_unref (test->pointer);
  g_free (test);

  return TRUE;
}

/**********************************************************************/

static gboolean
run_test (MetaContext *context,
          const char  *filename,
          int          index)
{
  TestCase *test = test_case_new (context);
  g_autofree char *file_basename = NULL;
  GError *error = NULL;

  GFile *file = g_file_new_for_path (filename);

  GDataInputStream *in = NULL;

  GFileInputStream *in_raw = g_file_read (file, NULL, &error);
  g_object_unref (file);
  if (in_raw == NULL)
    goto out;

  in = g_data_input_stream_new (G_INPUT_STREAM (in_raw));
  g_object_unref (in_raw);

  file_basename = g_path_get_basename (filename);

  int line_no = 0;
  while (error == NULL)
    {
      char *line = g_data_input_stream_read_line_utf8 (in, NULL, NULL, &error);
      if (line == NULL)
        break;

      line_no++;

      int argc;
      char **argv = NULL;
      if (!g_shell_parse_argv (line, &argc, &argv, &error))
        {
          if (g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_EMPTY_STRING))
            {
              g_clear_error (&error);
              goto next;
            }

          goto next;
        }

      test_case_do (test, file_basename, line_no, argc, argv, &error);

    next:
      if (error)
        g_prefix_error (&error, "%d: ", line_no);

      g_free (line);
      g_strfreev (argv);
    }

  {
    GError *tmp_error = NULL;
    if (!g_input_stream_close (G_INPUT_STREAM (in), NULL, &tmp_error))
      {
        if (error != NULL)
          g_clear_error (&tmp_error);
        else
          g_propagate_error (&error, tmp_error);
      }
  }

 out:
  if (in != NULL)
    g_object_unref (in);

  GError *cleanup_error = NULL;
  test_case_destroy (test, &cleanup_error);

  const char *testspos = strstr (filename, "tests/");
  char *pretty_name;
  if (testspos)
    pretty_name = g_strdup (testspos + strlen("tests/"));
  else
    pretty_name = g_strdup (filename);

  if (error || cleanup_error)
    {
      g_print ("not ok %d %s\n", index, pretty_name);

      if (error)
        g_print ("   %s\n", error->message);

      if (cleanup_error)
        {
          g_print ("   Fatal Error During Cleanup\n");
          g_print ("   %s\n", cleanup_error->message);
          exit (1);
        }
    }
  else
    {
      g_print ("ok %d %s\n", index, pretty_name);
    }

  g_free (pretty_name);

  gboolean success = error == NULL;

  g_clear_error (&error);
  g_clear_error (&cleanup_error);

  return success;
}

typedef struct
{
  int n_tests;
  char **tests;
} RunTestsInfo;

static int
run_tests (MetaContext  *context,
           RunTestsInfo *info)
{
  int i;
  gboolean success = TRUE;

  g_print ("1..%d\n", info->n_tests);

  for (i = 0; i < info->n_tests; i++)
    {
      if (!run_test (context, info->tests[i], i + 1))
        success = FALSE;
    }


  return success ? 0 : 1;
}

/**********************************************************************/

static gboolean
find_metatests_in_directory (GFile     *directory,
                             GPtrArray *results,
                             GError   **error)
{
  GFileEnumerator *enumerator = g_file_enumerate_children (directory,
                                                           "standard::name,standard::type",
                                                           G_FILE_QUERY_INFO_NONE,
                                                           NULL, error);
  if (!enumerator)
    return FALSE;

  while (*error == NULL)
    {
      GFileInfo *info = g_file_enumerator_next_file (enumerator, NULL, error);
      if (info == NULL)
        break;

      GFile *child = g_file_enumerator_get_child (enumerator, info);
      switch (g_file_info_get_file_type (info))
        {
        case G_FILE_TYPE_REGULAR:
          {
            const char *name = g_file_info_get_name (info);
            if (g_str_has_suffix (name, ".metatest"))
              g_ptr_array_add (results, g_file_get_path (child));
            break;
          }
        case G_FILE_TYPE_DIRECTORY:
          find_metatests_in_directory (child, results, error);
          break;
        default:
          break;
        }

      g_object_unref (child);
      g_object_unref (info);
    }

  {
    GError *tmp_error = NULL;
    if (!g_file_enumerator_close (enumerator, NULL, &tmp_error))
      {
        if (*error != NULL)
          g_clear_error (&tmp_error);
        else
          g_propagate_error (error, tmp_error);
      }
  }

  g_object_unref (enumerator);
  return *error == NULL;
}

static gboolean all_tests = FALSE;

const GOptionEntry options[] = {
  {
    "all", 0, 0, G_OPTION_ARG_NONE,
    &all_tests,
    "Run all installed tests",
    NULL
  },
  { NULL }
};

int
main (int argc, char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GPtrArray) tests = NULL;
  RunTestsInfo info;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);

  meta_context_add_option_entries (context, options, NULL);

  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  tests = g_ptr_array_new_with_free_func (g_free);
  if (all_tests)
    {
      GFile *test_dir = g_file_new_for_path (MUTTER_PKGDATADIR "/tests");
      g_autoptr (GError) error = NULL;

      if (!find_metatests_in_directory (test_dir, tests, &error))
        {
          g_printerr ("Error enumerating tests: %s\n", error->message);
          return EXIT_FAILURE;
        }
    }
  else
    {
      int i;
      char *curdir = g_get_current_dir ();

      for (i = 1; i < argc; i++)
        {
          if (g_path_is_absolute (argv[i]))
            g_ptr_array_add (tests, g_strdup (argv[i]));
          else
            g_ptr_array_add (tests, g_build_filename (curdir, argv[i], NULL));
        }

      g_free (curdir);
    }

  info.tests = (char **) tests->pdata;
  info.n_tests = tests->len;
  g_signal_connect (context, "run-tests", G_CALLBACK (run_tests), &info);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
