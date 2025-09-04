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

#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-monitor-config-store.h"
#include "backends/meta-virtual-monitor.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-input-thread.h"
#include "backends/native/meta-seat-native.h"
#include "compositor/meta-window-actor-private.h"
#include "core/display-private.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "wayland/meta-wayland.h"
#include "wayland/meta-window-wayland.h"
#include "wayland/meta-xwayland.h"
#include "x11/meta-x11-display-private.h"

struct _MetaTestClient
{
  MetaContext *context;

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
  MetaX11AlarmFilter *alarm_filter;
};

struct _MetaAsyncWaiter
{
  MetaX11Display *x11_display;

  XSyncCounter counter;
  int counter_value;
  XSyncAlarm alarm;

  GMainLoop *loop;
  int counter_wait_value;
};

typedef struct
{
  GList *subprocesses;
} ClientProcessHandler;

typedef struct _MetaTestComandWatcher
{
  MetaTestCommandFunc func;
  gpointer user_data;

  GDataInputStream *client_stdout;
  GOutputStream *client_stdin;
  GCancellable *cancellable;
} MetaTestCommandWatcher;

G_DEFINE_QUARK (meta-test-client-error-quark, meta_test_client_error)

static char *test_runner_client_path;

static void read_line_async (GDataInputStream       *client_stdout,
                             MetaTestCommandWatcher *watcher);

void
meta_ensure_test_client_path (int    argc,
                              char **argv)
{
  test_runner_client_path = g_test_build_filename (G_TEST_BUILT,
                                                   "mutter-test-client",
                                                   NULL);
  if (!g_file_test (test_runner_client_path,
                    G_FILE_TEST_EXISTS | G_FILE_TEST_IS_EXECUTABLE))
    {
      g_autofree char *basename = NULL;
      g_autofree char *dirname = NULL;

      basename = g_path_get_basename (argv[0]);

      dirname = g_path_get_dirname (argv[0]);
      test_runner_client_path = g_build_filename (dirname,
                                                  "mutter-test-client", NULL);
    }

  if (!g_file_test (test_runner_client_path,
                    G_FILE_TEST_EXISTS | G_FILE_TEST_IS_EXECUTABLE))
    g_error ("mutter-test-client executable not found");
}

MetaAsyncWaiter *
meta_async_waiter_new (MetaX11Display *x11_display)
{
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);
  MetaAsyncWaiter *waiter;
  XSyncValue value;
  XSyncAlarmAttributes attr;

  waiter = g_new0 (MetaAsyncWaiter, 1);

  waiter->x11_display = x11_display;
  g_object_add_weak_pointer (G_OBJECT (x11_display),
                             (gpointer *) &waiter->x11_display);
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
  MetaX11Display *x11_display;

  x11_display = waiter->x11_display;
  if (x11_display)
    {
      Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);

      XSyncDestroyAlarm (xdisplay, waiter->alarm);
      XSyncDestroyCounter (xdisplay, waiter->counter);

      g_object_remove_weak_pointer (G_OBJECT (x11_display),
                                    (gpointer *) &waiter->x11_display);
    }
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
  Display *xdisplay;
  int wait_value;

  g_return_if_fail (waiter->x11_display);

  wait_value = meta_async_waiter_next_value (waiter);

  XSyncValue sync_value;
  XSyncIntToValue (&sync_value, wait_value);

  xdisplay = meta_x11_display_get_xdisplay (waiter->x11_display);
  XSyncSetCounter (xdisplay, waiter->counter, sync_value);
  meta_async_waiter_wait (waiter, wait_value);
}

gboolean
meta_async_waiter_process_x11_event (MetaAsyncWaiter       *waiter,
                                     MetaX11Display        *x11_display,
                                     XSyncAlarmNotifyEvent *event)
{
  g_assert_true (x11_display == waiter->x11_display);

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

MetaWindowClientType
meta_test_client_get_client_type (MetaTestClient *client)
{
  return client->type;
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

static gboolean
meta_test_client_do_line (MetaTestClient  *client,
                          const char      *line_out,
                          GError         **error)
{
  g_autoptr (GError) local_error = NULL;
  g_autofree char *line = NULL;

  if (!g_data_output_stream_put_string (client->in, line_out,
                                        client->cancellable, error))
    return FALSE;

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

  if (local_error)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  if (!line)
    {
      g_set_error (error,
                   META_TEST_CLIENT_ERROR,
                   META_TEST_CLIENT_ERROR_RUNTIME_ERROR,
                   "test client exited");
      return FALSE;
    }

  if (strcmp (line, "OK") != 0)
    {
      g_set_error (error,
                   META_TEST_CLIENT_ERROR,
                   META_TEST_CLIENT_ERROR_RUNTIME_ERROR,
                   "%s", line);
      return FALSE;
    }

  return TRUE;
}

gboolean
meta_test_client_do_strv (MetaTestClient  *client,
                          const char     **args,
                          GError         **error)
{
  g_autoptr (GString) command = NULL;
  const char **arg;

  command = g_string_new (NULL);
  for (arg = &args[0]; *arg; arg++)
    {
      g_autofree char *quoted = NULL;

      if (command->len > 0)
        g_string_append_c (command, ' ');

      quoted = g_shell_quote (*arg);
      g_string_append (command, quoted);
    }

  g_string_append_c (command, '\n');

  return meta_test_client_do_line (client, command->str, error);
}

gboolean
meta_test_client_dov (MetaTestClient  *client,
                      GError         **error,
                      va_list          vap)
{
  g_autoptr (GStrvBuilder) args_builder = NULL;
  g_auto (GStrv) args = NULL;

  args_builder = g_strv_builder_new ();
  while (TRUE)
    {
      char *word = va_arg (vap, char *);

      if (!word)
        break;

      g_strv_builder_add (args_builder, word);
    }

  args = g_strv_builder_end (args_builder);
  return meta_test_client_do_strv (client, (const char **) args, error);
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

void
meta_test_client_run (MetaTestClient *client,
                      const char     *script)
{
  g_auto (GStrv) lines = NULL;
  int i;

  lines = g_strsplit (script, "\n", -1);
  for (i = 0; lines[i]; i++)
    {
      g_autoptr (GError) error = NULL;

      if (strlen (lines[i]) > 1)
        {
          g_autofree char *line = NULL;

          line = g_strdup_printf ("%s\n", lines[i]);
          if (!meta_test_client_do_line (client, line, &error))
            g_error ("Failed to do line '%s': %s", lines[i], error->message);
        }
    }
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
  MetaDisplay *display = meta_context_get_display (client->context);
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
window_weak_notify_cb (gpointer  user_data,
                       GObject  *where_the_object_was)
{
  g_error ("Window was destroyed when waiting to be shown");
}

static void
on_window_shown (MetaWindow       *window,
                 WaitForShownData *data)
{
  g_object_weak_unref (G_OBJECT (window),
                       window_weak_notify_cb,
                       NULL);
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
      g_object_weak_unref (G_OBJECT (data->window),
                           window_weak_notify_cb,
                           NULL);
      g_main_loop_quit (data->loop);
    }

  return G_SOURCE_REMOVE;
}

void
meta_wait_for_window_shown (MetaWindow *window)
{
  MetaDisplay *display = meta_window_get_display (window);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  MetaLaters *laters = meta_compositor_get_laters (compositor);

  g_object_weak_ref (G_OBJECT (window),
                     window_weak_notify_cb,
                     NULL);

  WaitForShownData data = {
    .loop = g_main_loop_new (NULL, FALSE),
    .window = window,
  };
  meta_laters_add (laters, META_LATER_BEFORE_REDRAW,
                   wait_for_showing_before_redraw,
                   &data,
                   NULL);
  g_main_loop_run (data.loop);
  g_clear_signal_handler (&data.shown_handler_id, window);
  g_main_loop_unref (data.loop);
}

static gboolean
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

static void
on_prepare_shutdown (MetaBackend          *backend,
                     ClientProcessHandler *process_handler)
{
  g_debug ("Waiting for test clients to exit before shutting down");
  while (process_handler->subprocesses)
    g_main_context_iteration (NULL, TRUE);
}

static void
wait_check_cb (GObject      *source_object,
               GAsyncResult *result,
               gpointer      user_data)
{
  GSubprocess *subprocess = G_SUBPROCESS (source_object);
  ClientProcessHandler *process_handler = user_data;
  g_autoptr (GError) error = NULL;

  if (!g_subprocess_wait_check_finish (subprocess, result, &error))
    {
      if (g_error_matches (error, G_SPAWN_EXIT_ERROR, 1))
        {
          g_debug ("Test client process %s exited with exit status 1",
                   g_subprocess_get_identifier (subprocess));
        }
      else
        {
          g_warning ("Test client process %s crashed with status %d",
                     g_subprocess_get_identifier (subprocess),
                     error->code);
        }
    }

  process_handler->subprocesses = g_list_remove (process_handler->subprocesses,
                                                 subprocess);
}

static ClientProcessHandler *
ensure_process_handler (MetaContext *context)
{
  ClientProcessHandler *process_handler;
  const char data_key[] = "test-client-subprocess-handler";
  MetaBackend *backend;

  process_handler = g_object_get_data (G_OBJECT (context), data_key);
  if (process_handler)
    return process_handler;

  process_handler = g_new0 (ClientProcessHandler, 1);
  g_object_set_data_full (G_OBJECT (context), data_key,
                          process_handler, g_free);

  backend = meta_context_get_backend (context);
  g_signal_connect (backend, "prepare-shutdown",
                    G_CALLBACK (on_prepare_shutdown),
                    process_handler);

  return process_handler;
}

static gboolean
alarm_filter (MetaX11Display        *x11_display,
              XSyncAlarmNotifyEvent *event,
              gpointer               user_data)
{
  MetaTestClient *client = user_data;

  return meta_test_client_process_x11_event (client, x11_display, event);
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
  ClientProcessHandler *process_handler;
  MetaWaylandCompositor *compositor;
  const char *wayland_display_name;
#ifdef HAVE_XWAYLAND
  const char *x11_display_name;
#endif

  launcher =  g_subprocess_launcher_new ((G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                          G_SUBPROCESS_FLAGS_STDOUT_PIPE));

  g_assert_true (meta_is_wayland_compositor ());
  compositor = meta_context_get_wayland_compositor (context);
  wayland_display_name = meta_wayland_get_wayland_display_name (compositor);
#ifdef HAVE_XWAYLAND
  x11_display_name = meta_wayland_get_public_xwayland_display_name (compositor);
#endif
  if (wayland_display_name)
    {
      g_subprocess_launcher_setenv (launcher,
                                    "WAYLAND_DISPLAY", wayland_display_name,
                                    TRUE);
    }

#ifdef HAVE_XWAYLAND
  if (x11_display_name)
    {
      g_subprocess_launcher_setenv (launcher,
                                    "DISPLAY", x11_display_name,
                                    TRUE);
    }
#endif

  subprocess = g_subprocess_launcher_spawn (launcher,
                                            error,
                                            test_runner_client_path,
                                            "--client-id",
                                            id,
                                            (type == META_WINDOW_CLIENT_TYPE_WAYLAND ?
                                             "--wayland" : NULL),
                                            NULL);
  g_object_unref (launcher);

  if (!subprocess)
    return NULL;

  process_handler = ensure_process_handler (context);
  process_handler->subprocesses = g_list_prepend (process_handler->subprocesses,
                                                  subprocess);
  g_subprocess_wait_check_async (subprocess, NULL,
                                 wait_check_cb,
                                 process_handler);

  client = g_new0 (MetaTestClient, 1);
  client->context = context;
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
      MetaDisplay *display = meta_context_get_display (context);
      MetaX11Display *x11_display;

      x11_display = meta_display_get_x11_display (display);
      if (!x11_display)
        {
          GThread *thread;

          thread = g_thread_new ("Mutter Spawn Xwayland Thread",
                                 spawn_xwayland,
                                 NULL);
          meta_context_test_wait_for_x11_display (META_CONTEXT_TEST (context));
          g_thread_join (thread);
        }
      x11_display = meta_display_get_x11_display (display);
      g_assert_nonnull (x11_display);

      client->alarm_filter = meta_x11_display_add_alarm_filter (x11_display,
                                                                alarm_filter,
                                                                client);

      client->waiter = meta_async_waiter_new (x11_display);
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
  MetaDisplay *display = meta_context_get_display (client->context);
  MetaX11Display *x11_display;
  GError *error = NULL;

  x11_display = meta_display_get_x11_display (display);
  if (client->alarm_filter && x11_display)
    {
      meta_x11_display_remove_alarm_filter (x11_display,
                                            client->alarm_filter);
    }

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
  g_autofree char *path = NULL;

  g_assert_nonnull (config_manager);

  config_store = meta_monitor_config_manager_get_store (config_manager);

  path = g_test_build_filename (G_TEST_DIST, "monitor-configs",
                                filename, NULL);
  if (!meta_monitor_config_store_set_custom (config_store, path, NULL,
                                             configs_flags,
                                             &error))
    g_warning ("Failed to set custom config: %s", error->message);
}

static void
set_true_cb (gpointer user_data)
{
  gboolean *value = user_data;

  *value = TRUE;
}

void
meta_wait_for_monitors_changed (MetaContext *context)
{
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  gulong monitors_changed_handler_id;
  gboolean monitors_changed = FALSE;

  monitors_changed_handler_id =
    g_signal_connect_swapped (monitor_manager, "monitors-changed",
                              G_CALLBACK (set_true_cb), &monitors_changed);
  while (!monitors_changed)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (monitor_manager, monitors_changed_handler_id);
}

static void
on_view_presented (ClutterStage      *stage,
                   ClutterStageView  *view,
                   ClutterFrameInfo  *frame_info,
                   GList            **presented_views)
{
  *presented_views = g_list_remove (*presented_views, view);
}

static void
raise_error (const char *message)
{
  g_error ("%s", message);
}

void
meta_wait_for_paint (MetaContext *context)
{
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  GList *views;
  gulong presented_handler_id;
  gulong monitors_changed_handler_id;

  monitors_changed_handler_id =
    g_signal_connect_swapped (monitor_manager, "monitors-changed",
                              G_CALLBACK (raise_error),
                              (char *) "Monitors changed while waiting for paint");

  clutter_actor_queue_redraw (stage);

  views = g_list_copy (meta_renderer_get_views (renderer));
  presented_handler_id = g_signal_connect (stage, "presented",
                                           G_CALLBACK (on_view_presented), &views);
  while (views)
    g_main_context_iteration (NULL, TRUE);
  g_signal_handler_disconnect (stage, presented_handler_id);
  g_signal_handler_disconnect (monitor_manager, monitors_changed_handler_id);
}

static void
on_after_update (ClutterStage     *stage,
                 ClutterStageView *view,
                 ClutterFrame     *frame,
                 gboolean         *done)
{
  *done = TRUE;
}

void
meta_wait_for_update (MetaContext *context)
{
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  gulong after_update_handler_id;
  gboolean done = FALSE;

  clutter_stage_schedule_update (CLUTTER_STAGE (stage));

  after_update_handler_id = g_signal_connect (stage, "after-update",
                                              G_CALLBACK (on_after_update),
                                              &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);
  g_signal_handler_disconnect (stage, after_update_handler_id);
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

#ifdef HAVE_NATIVE_BACKEND
static GMutex mutex;
static GCond cond;

static gboolean
queue_callback (GTask *task)
{
  g_mutex_lock (&mutex);
  g_cond_signal (&cond);
  g_mutex_unlock (&mutex);

  g_task_return_boolean (task, TRUE);

  return G_SOURCE_REMOVE;
}
#endif

void
meta_flush_input (MetaContext *context)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterSeat *seat;
  MetaSeatNative *seat_native;
  g_autoptr (GTask) task = NULL;

  g_assert_true (META_IS_BACKEND_NATIVE (backend));

  seat = meta_backend_get_default_seat (backend);
  seat_native = META_SEAT_NATIVE (seat);

  task = g_task_new (backend, NULL, NULL, NULL);

  g_mutex_lock (&mutex);

  meta_seat_impl_run_input_task (seat_native->impl, task,
                                 (GSourceFunc) queue_callback);

  g_cond_wait (&cond, &mutex);
  g_mutex_unlock (&mutex);
#endif
}

GSubprocess *
meta_launch_test_executable (GSubprocessFlags  subprocess_flags,
                             const char       *name,
                             const char       *argv0,
                             ...)
{
  g_autoptr (GPtrArray) args = NULL;
  const char *arg;
  va_list ap;
  g_autofree char *test_client_path = NULL;
  GSubprocessLauncher *launcher;
  GSubprocess *subprocess;
  GError *error = NULL;

  args = g_ptr_array_new ();

  test_client_path = g_test_build_filename (G_TEST_BUILT, name, NULL);
  g_ptr_array_add (args, test_client_path);

  va_start (ap, argv0);
  g_ptr_array_add (args, (char *) argv0);
  while ((arg = va_arg (ap, const char *)))
    g_ptr_array_add (args, (char *) arg);

  g_ptr_array_add (args, NULL);
  va_end (ap);

  launcher = g_subprocess_launcher_new (subprocess_flags);
  g_subprocess_launcher_setenv (launcher,
                                "XDG_RUNTIME_DIR", getenv ("XDG_RUNTIME_DIR"),
                                TRUE);
  g_subprocess_launcher_setenv (launcher,
                                "G_TEST_SRCDIR", g_test_get_dir (G_TEST_DIST),
                                TRUE);
  g_subprocess_launcher_setenv (launcher,
                                "G_TEST_BUILDDIR", g_test_get_dir (G_TEST_BUILT),
                                TRUE);
  g_subprocess_launcher_setenv (launcher,
                                "G_MESSAGES_DEBUG", "all",
                                TRUE);
  subprocess = g_subprocess_launcher_spawnv (launcher,
                                             (const char * const *) args->pdata,
                                             &error);
  if (!subprocess)
    g_error ("Failed to launch screen cast test client: %s", error->message);

  return subprocess;
}

static void
process_line (const char             *line,
              MetaTestCommandWatcher *watcher)
{
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) argv = NULL;
  int argc;

  if (!g_shell_parse_argv (line, &argc, &argv, &error))
    g_assert_no_error (error);

  if (!watcher->func (argc, argv, watcher->user_data))
    g_error ("Unknown command '%s'", line);

  if (watcher->client_stdin)
    {
      g_output_stream_printf (watcher->client_stdin, NULL, NULL, &error,
                              "OK\n");
      g_assert_no_error (error);
      g_output_stream_flush (watcher->client_stdin, NULL, &error);
      g_assert_no_error (error);
    }
}

static void
line_read_cb (GObject      *source_object,
              GAsyncResult *res,
              gpointer      user_data)
{
  GDataInputStream *client_stdout = G_DATA_INPUT_STREAM (source_object);
  MetaTestCommandWatcher *watcher = user_data;
  g_autoptr (GError) error = NULL;
  g_autofree char *line = NULL;

  line = g_data_input_stream_read_line_finish_utf8 (client_stdout,
                                                    res,
                                                    NULL,
                                                    &error);
  if (error)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_error ("Failed to read line: %s", error->message);
      return;
    }

  if (line)
    process_line (line, watcher);

  read_line_async (client_stdout, watcher);
}

static void
read_line_async (GDataInputStream       *client_stdout,
                 MetaTestCommandWatcher *watcher)
{
  g_data_input_stream_read_line_async (client_stdout,
                                       G_PRIORITY_DEFAULT,
                                       watcher->cancellable,
                                       line_read_cb,
                                       watcher);
}

static void
watcher_test_client_exited (GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  MetaTestCommandWatcher *watcher = user_data;
  GError *error = NULL;

  if (!g_subprocess_wait_finish (G_SUBPROCESS (source_object),
                                 result,
                                 &error))
    g_error ("Screen cast test client exited with an error: %s", error->message);

  g_cancellable_cancel (watcher->cancellable);
  g_clear_object (&watcher->client_stdout);
  g_clear_object (&watcher->client_stdin);
  g_free (watcher);
}

void
meta_test_process_watch_commands (GSubprocess         *subprocess,
                                  MetaTestCommandFunc  func,
                                  gpointer             user_data)
{
  MetaTestCommandWatcher *watcher;
  GInputStream *stdout_stream;
  GOutputStream *stdin_stream;

  watcher = g_new0 (MetaTestCommandWatcher, 1);

  watcher->func = func;
  watcher->user_data = user_data;

  stdout_stream = g_subprocess_get_stdout_pipe (subprocess);
  if (stdout_stream)
    watcher->client_stdout = g_data_input_stream_new (stdout_stream);

  stdin_stream = g_subprocess_get_stdin_pipe (subprocess);
  if (stdin_stream)
    watcher->client_stdin = g_object_ref (stdin_stream);

  watcher->cancellable = g_cancellable_new ();

  read_line_async (watcher->client_stdout, watcher);

  g_subprocess_wait_check_async (subprocess,
                                 NULL,
                                 watcher_test_client_exited,
                                 watcher);
}

static void
test_client_exited (GObject      *source_object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  GError *error = NULL;

  if (!g_subprocess_wait_finish (G_SUBPROCESS (source_object),
                                 result,
                                 &error))
    g_error ("Screen cast test client exited with an error: %s", error->message);

  g_main_loop_quit (user_data);
}

void
meta_wait_test_process (GSubprocess *subprocess)
{
  GMainLoop *loop;

  loop = g_main_loop_new (NULL, FALSE);
  g_subprocess_wait_check_async (subprocess,
                                 NULL,
                                 test_client_exited,
                                 loop);
  g_main_loop_run (loop);
  g_assert_true (g_subprocess_get_successful (subprocess));
}

void
meta_wait_for_window_cursor (MetaContext *context)
{
  MetaBackend *backend = meta_context_get_backend (context);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);

  while (!meta_cursor_tracker_has_window_cursor (cursor_tracker))
    g_main_context_iteration (NULL, TRUE);
}

void
meta_wait_for_effects (MetaWindow *window)
{
  MetaWindowActor *window_actor;

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

void
meta_wait_wayland_window_reconfigure (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  uint32_t serial;

  g_assert_true (meta_window_wayland_get_pending_serial (wl_window, &serial));
  while (meta_window_wayland_peek_configuration (wl_window, serial))
    g_main_context_iteration (NULL, TRUE);
}
