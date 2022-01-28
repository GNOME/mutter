/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014-2017 Red Hat, Inc.
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

#include "tests/meta-test-utils-private.h"

#include <gio/gio.h>
#include <string.h>
#include <X11/Xlib-xcb.h>

#include "backends/meta-monitor-config-store.h"
#include "backends/meta-virtual-monitor.h"
#include "core/display-private.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "wayland/meta-wayland.h"
#include "wayland/meta-xwayland.h"
#include "x11/meta-x11-display-private.h"

struct _MetaTestClient
{
  char *id;
  MetaWindowClientType type;
  GSubprocess *subprocess;
  GCancellable *cancellable;
  GMainLoop *loop;
  GDataOutputStream *in;
  GDataInputStream *out;

  char *line;
  GError **error;

  MetaAsyncWaiter *waiter;
};

struct _MetaAsyncWaiter {
  XSyncCounter counter;
  int counter_value;
  XSyncAlarm alarm;

  GMainLoop *loop;
  int counter_wait_value;
};

G_DEFINE_QUARK (meta-test-client-error-quark, meta_test_client_error)

static char *test_client_path;

void
meta_ensure_test_client_path (int    argc,
                              char **argv)
{
  test_client_path = g_test_build_filename (G_TEST_BUILT,
                                            "src",
                                            "tests",
                                            "mutter-test-client",
                                            NULL);
  if (!g_file_test (test_client_path,
                    G_FILE_TEST_EXISTS | G_FILE_TEST_IS_EXECUTABLE))
    {
      g_autofree char *basename = NULL;
      g_autofree char *dirname = NULL;

      basename = g_path_get_basename (argv[0]);

      dirname = g_path_get_dirname (argv[0]);
      test_client_path = g_build_filename (dirname,
                                           "mutter-test-client", NULL);
    }

  if (!g_file_test (test_client_path,
                    G_FILE_TEST_EXISTS | G_FILE_TEST_IS_EXECUTABLE))
    g_error ("mutter-test-client executable not found");
}

MetaAsyncWaiter *
meta_async_waiter_new (void)
{
  MetaAsyncWaiter *waiter = g_new0 (MetaAsyncWaiter, 1);

  MetaDisplay *display = meta_get_display ();
  Display *xdisplay = display->x11_display->xdisplay;
  XSyncValue value;
  XSyncAlarmAttributes attr;

  waiter->counter_value = 0;
  XSyncIntToValue (&value, waiter->counter_value);

  waiter->counter = XSyncCreateCounter (xdisplay, value);

  attr.trigger.counter = waiter->counter;
  attr.trigger.test_type = XSyncPositiveComparison;

  /* Initialize to one greater than the current value */
  attr.trigger.value_type = XSyncRelative;
  XSyncIntToValue (&attr.trigger.wait_value, 1);

  /* After triggering, increment test_value by this until
   * until the test condition is false */
  XSyncIntToValue (&attr.delta, 1);

  /* we want events (on by default anyway) */
  attr.events = True;

  waiter->alarm = XSyncCreateAlarm (xdisplay,
                                    XSyncCACounter |
                                    XSyncCAValueType |
                                    XSyncCAValue |
                                    XSyncCATestType |
                                    XSyncCADelta |
                                    XSyncCAEvents,
                                    &attr);

  waiter->loop = g_main_loop_new (NULL, FALSE);

  return waiter;
}

void
meta_async_waiter_destroy (MetaAsyncWaiter *waiter)
{
  MetaDisplay *display = meta_get_display ();
  Display *xdisplay = display->x11_display->xdisplay;

  XSyncDestroyAlarm (xdisplay, waiter->alarm);
  XSyncDestroyCounter (xdisplay, waiter->counter);
  g_main_loop_unref (waiter->loop);
}

static int
meta_async_waiter_next_value (MetaAsyncWaiter *waiter)
{
  return waiter->counter_value + 1;
}

static void
meta_async_waiter_wait (MetaAsyncWaiter *waiter,
                        int              wait_value)
{
  if (waiter->counter_value < wait_value)
    {
      waiter->counter_wait_value = wait_value;
      g_main_loop_run (waiter->loop);
      waiter->counter_wait_value = 0;
    }
}

void
meta_async_waiter_set_and_wait (MetaAsyncWaiter *waiter)
{
  MetaDisplay *display = meta_get_display ();
  Display *xdisplay = display->x11_display->xdisplay;
  int wait_value = meta_async_waiter_next_value (waiter);

  XSyncValue sync_value;
  XSyncIntToValue (&sync_value, wait_value);

  XSyncSetCounter (xdisplay, waiter->counter, sync_value);
  meta_async_waiter_wait (waiter, wait_value);
}

gboolean
meta_async_waiter_process_x11_event (MetaAsyncWaiter       *waiter,
                                     MetaX11Display        *x11_display,
                                     XSyncAlarmNotifyEvent *event)
{

  if (event->alarm != waiter->alarm)
    return FALSE;

  waiter->counter_value = XSyncValueLow32 (event->counter_value);

  if (waiter->counter_wait_value != 0 &&
      waiter->counter_value >= waiter->counter_wait_value)
    g_main_loop_quit (waiter->loop);

  return TRUE;
}

char *
meta_test_client_get_id (MetaTestClient *client)
{
  return client->id;
}

static void
test_client_line_read (GObject      *source,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  MetaTestClient *client = user_data;

  client->line = g_data_input_stream_read_line_finish_utf8 (client->out,
                                                            result,
                                                            NULL,
                                                            client->error);
  g_main_loop_quit (client->loop);
}

gboolean
meta_test_client_dov (MetaTestClient  *client,
                      GError         **error,
                      va_list          vap)
{
  GString *command = g_string_new (NULL);
  char *line = NULL;
  GError *local_error = NULL;

  while (TRUE)
    {
      char *word = va_arg (vap, char *);
      char *quoted;

      if (word == NULL)
        break;

      if (command->len > 0)
        g_string_append_c (command, ' ');

      quoted = g_shell_quote (word);
      g_string_append (command, quoted);
      g_free (quoted);
    }

  g_string_append_c (command, '\n');

  if (!g_data_output_stream_put_string (client->in, command->str,
                                        client->cancellable, &local_error))
    goto out;

  g_data_input_stream_read_line_async (client->out,
                                       G_PRIORITY_DEFAULT,
                                       client->cancellable,
                                       test_client_line_read,
                                       client);

  client->error = &local_error;
  g_main_loop_run (client->loop);
  line = client->line;
  client->line = NULL;
  client->error = NULL;

  if (!line)
    {
      if (!local_error)
        {
          g_set_error (&local_error,
                       META_TEST_CLIENT_ERROR,
                       META_TEST_CLIENT_ERROR_RUNTIME_ERROR,
                       "test client exited");
        }
      goto out;
    }

  if (strcmp (line, "OK") != 0)
    {
      g_set_error (&local_error,
                   META_TEST_CLIENT_ERROR,
                   META_TEST_CLIENT_ERROR_RUNTIME_ERROR,
                   "%s", line);
      goto out;
    }

 out:
  g_string_free (command, TRUE);
  g_free (line);

  if (local_error)
    {
      g_propagate_error (error, local_error);
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

gboolean
meta_test_client_do (MetaTestClient  *client,
                     GError         **error,
                     ...)
{
  va_list vap;
  gboolean retval;

  va_start (vap, error);
  retval = meta_test_client_dov (client, error, vap);
  va_end (vap);

  return retval;
}

gboolean
meta_test_client_wait (MetaTestClient  *client,
                       GError         **error)
{
  if (client->type == META_WINDOW_CLIENT_TYPE_WAYLAND)
    {
      return meta_test_client_do (client, error, "sync", NULL);
    }
  else
    {
      int wait_value = meta_async_waiter_next_value (client->waiter);
      char *counter_str = g_strdup_printf ("%lu", client->waiter->counter);
      char *wait_value_str = g_strdup_printf ("%d", wait_value);
      gboolean success;

      success = meta_test_client_do (client, error,
                                     "set_counter", counter_str, wait_value_str,
                                     NULL);
      g_free (counter_str);
      g_free (wait_value_str);
      if (!success)
        return FALSE;

      meta_async_waiter_wait (client->waiter, wait_value);
      return TRUE;
    }
}

MetaWindow *
meta_find_window_from_title (MetaContext *context,
                             const char  *title)
{
  g_autoptr (GList) windows = NULL;
  GList *l;

  windows = meta_display_list_all_windows (meta_context_get_display (context));
  for (l = windows; l; l = l->next)
    {
      MetaWindow *window = l->data;

      if (g_strcmp0 (window->title, title) == 0)
        return window;
    }

  return NULL;
}

MetaWindow *
meta_test_client_find_window (MetaTestClient  *client,
                              const char      *window_id,
                              GError         **error)
{
  MetaDisplay *display = meta_get_display ();
  g_autofree char *expected_title = NULL;
  MetaWindow *window;

  expected_title = g_strdup_printf ("test/%s/%s", client->id, window_id);
  window = meta_find_window_from_title (meta_display_get_context (display),
                                        expected_title);

  if (!window)
    {
      g_set_error (error,
                   META_TEST_CLIENT_ERROR,
                   META_TEST_CLIENT_ERROR_RUNTIME_ERROR,
                   "window %s/%s isn't known to Mutter", client->id, window_id);
      return NULL;
    }

  return window;
}

typedef struct _WaitForShownData
{
  GMainLoop *loop;
  MetaWindow *window;
  gulong shown_handler_id;
} WaitForShownData;

static void
on_window_shown (MetaWindow       *window,
                 WaitForShownData *data)
{
  g_main_loop_quit (data->loop);
}

static gboolean
wait_for_showing_before_redraw (gpointer user_data)
{
  WaitForShownData *data = user_data;

  if (meta_window_is_hidden (data->window))
    {
      data->shown_handler_id = g_signal_connect (data->window, "shown",
                                                 G_CALLBACK (on_window_shown),
                                                 data);
    }
  else
    {
      g_main_loop_quit (data->loop);
    }

  return FALSE;
}

void
meta_test_client_wait_for_window_shown (MetaTestClient *client,
                                        MetaWindow     *window)
{
  WaitForShownData data = {
    .loop = g_main_loop_new (NULL, FALSE),
    .window = window,
  };
  meta_later_add (META_LATER_BEFORE_REDRAW,
                  wait_for_showing_before_redraw,
                  &data,
                  NULL);
  g_main_loop_run (data.loop);
  g_clear_signal_handler (&data.shown_handler_id, window);
  g_main_loop_unref (data.loop);
}

gboolean
meta_test_client_process_x11_event (MetaTestClient        *client,
                                    MetaX11Display        *x11_display,
                                    XSyncAlarmNotifyEvent *event)
{
  if (client->waiter)
    {
      return meta_async_waiter_process_x11_event (client->waiter,
                                                  x11_display,
                                                  event);
    }
  else
    {
      return FALSE;
    }
}

static gpointer
spawn_xwayland (gpointer user_data)
{
  xcb_connection_t *connection;

  connection = xcb_connect (NULL, NULL);
  g_assert_nonnull (connection);
  xcb_disconnect (connection);

  return NULL;
}

MetaTestClient *
meta_test_client_new (MetaContext           *context,
                      const char            *id,
                      MetaWindowClientType   type,
                      GError               **error)
{
  MetaTestClient *client;
  GSubprocessLauncher *launcher;
  GSubprocess *subprocess;
  MetaWaylandCompositor *compositor;
  const char *wayland_display_name;
  const char *x11_display_name;

  launcher =  g_subprocess_launcher_new ((G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                          G_SUBPROCESS_FLAGS_STDOUT_PIPE));

  g_assert (meta_is_wayland_compositor ());
  compositor = meta_context_get_wayland_compositor (context);
  wayland_display_name = meta_wayland_get_wayland_display_name (compositor);
  x11_display_name = meta_wayland_get_public_xwayland_display_name (compositor);

  if (wayland_display_name)
    {
      g_subprocess_launcher_setenv (launcher,
                                    "WAYLAND_DISPLAY", wayland_display_name,
                                    TRUE);
    }

  if (x11_display_name)
    {
      g_subprocess_launcher_setenv (launcher,
                                    "DISPLAY", x11_display_name,
                                    TRUE);
    }

  subprocess = g_subprocess_launcher_spawn (launcher,
                                            error,
                                            test_client_path,
                                            "--client-id",
                                            id,
                                            (type == META_WINDOW_CLIENT_TYPE_WAYLAND ?
                                             "--wayland" : NULL),
                                            NULL);
  g_object_unref (launcher);

  if (!subprocess)
    return NULL;

  client = g_new0 (MetaTestClient, 1);
  client->type = type;
  client->id = g_strdup (id);
  client->cancellable = g_cancellable_new ();
  client->subprocess = subprocess;
  client->in =
    g_data_output_stream_new (g_subprocess_get_stdin_pipe (subprocess));
  client->out =
    g_data_input_stream_new (g_subprocess_get_stdout_pipe (subprocess));
  client->loop = g_main_loop_new (NULL, FALSE);

  if (client->type == META_WINDOW_CLIENT_TYPE_X11)
    {
      MetaDisplay *display = meta_get_display ();

      if (!display->x11_display)
        {
          GThread *thread;

          thread = g_thread_new ("Mutter Spawn Xwayland Thread",
                                 spawn_xwayland,
                                 NULL);
          meta_context_test_wait_for_x11_display (META_CONTEXT_TEST (context));
          g_thread_join (thread);
        }

      client->waiter = meta_async_waiter_new ();
    }

  return client;
}

gboolean
meta_test_client_quit (MetaTestClient  *client,
                       GError         **error)
{
  if (!meta_test_client_do (client, error, "destroy_all", NULL))
    return FALSE;

  if (!meta_test_client_wait (client, error))
    return FALSE;

  return TRUE;
}

void
meta_test_client_destroy (MetaTestClient *client)
{
  GError *error = NULL;

  if (client->waiter)
    meta_async_waiter_destroy (client->waiter);

  g_output_stream_close (G_OUTPUT_STREAM (client->in), NULL, &error);
  if (error)
    {
      g_warning ("Error closing client stdin: %s", error->message);
      g_clear_error (&error);
    }
  g_object_unref (client->in);

  g_input_stream_close (G_INPUT_STREAM (client->out), NULL, &error);
  if (error)
    {
      g_warning ("Error closing client stdout: %s", error->message);
      g_clear_error (&error);
    }
  g_object_unref (client->out);

  g_object_unref (client->cancellable);
  g_object_unref (client->subprocess);
  g_main_loop_unref (client->loop);
  g_free (client->id);
  g_free (client);
}

const char *
meta_test_get_plugin_name (void)
{
  const char *name;

  name = g_getenv ("MUTTER_TEST_PLUGIN_PATH");
  if (name)
    return name;
  else
    return "libdefault";
}

void
meta_set_custom_monitor_config_full (MetaBackend            *backend,
                                     const char             *filename,
                                     MetaMonitorsConfigFlag  configs_flags)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store;
  GError *error = NULL;
  const char *path;

  g_assert_nonnull (config_manager);

  config_store = meta_monitor_config_manager_get_store (config_manager);

  path = g_test_get_filename (G_TEST_DIST, "tests", "monitor-configs",
                              filename, NULL);
  if (!meta_monitor_config_store_set_custom (config_store, path, NULL,
                                             configs_flags,
                                             &error))
    g_warning ("Failed to set custom config: %s", error->message);
}

static void
on_view_presented (ClutterStage      *stage,
                   ClutterStageView  *view,
                   ClutterFrameInfo  *frame_info,
                   GList            **presented_views)
{
  *presented_views = g_list_remove (*presented_views, view);
}

void
meta_wait_for_paint (MetaContext *context)
{
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  GList *views;
  gulong handler_id;

  clutter_actor_queue_redraw (stage);

  views = g_list_copy (meta_renderer_get_views (renderer));
  handler_id = g_signal_connect (stage, "presented",
                                 G_CALLBACK (on_view_presented), &views);
  while (views)
    g_main_context_iteration (NULL, TRUE);
  g_signal_handler_disconnect (stage, handler_id);
}

MetaVirtualMonitor *
meta_create_test_monitor (MetaContext *context,
                          int          width,
                          int          height,
                          float        refresh_rate)
{
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  g_autoptr (MetaVirtualMonitorInfo) monitor_info = NULL;
  g_autoptr (GError) error = NULL;
  static int serial_count = 0x10000;
  g_autofree char *serial = NULL;
  MetaVirtualMonitor *virtual_monitor;

  serial = g_strdup_printf ("0x%x", serial_count++);
  monitor_info = meta_virtual_monitor_info_new (width, height, refresh_rate,
                                                "MetaTestVendor",
                                                "MetaVirtualMonitor",
                                                serial);
  virtual_monitor = meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                                 monitor_info,
                                                                 &error);
  if (!virtual_monitor)
    g_error ("Failed to create virtual monitor: %s", error->message);
  meta_monitor_manager_reload (monitor_manager);

  return virtual_monitor;
}
