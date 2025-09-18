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
#include <libevdev/libevdev.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "backends/meta-monitor-config-utils.h"
#include "backends/meta-virtual-monitor.h"
#include "clutter/clutter.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/meta-window-drag.h"
#include "core/meta-workspace-manager-private.h"
#include "core/meta-workspace-manager-private.h"
#include "core/window-private.h"
#include "core/workspace-private.h"
#include "meta-test/meta-context-test.h"
#include "meta/util.h"
#include "meta/window.h"
#include "tests/meta-test-utils.h"
#include "wayland/meta-wayland-keyboard.h"
#include "wayland/meta-wayland-pointer.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-window-wayland.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11-private.h"

typedef enum _StackFilter
{
  STACK_FILTER_ALL,
  STACK_FILTER_SHOWING,
} StackFilter;

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
  ClutterVirtualInputDevice *keyboard;
  GHashTable *cloned_windows;
  GHashTable *popups;
} TestCase;

#define META_SIDE_TEST_CASE_NONE G_MAXINT32

static gboolean test_case_do (TestCase    *test,
                              const char  *filename,
                              int          line_no,
                              int          argc,
                              char       **argv,
                              GError     **error);

static void
set_true_cb (gboolean *value)
{
  *value = TRUE;
}

static void
wait_for_signal_emission (gpointer    instance,
                          const char *signal_name)
{
  gulong handler_id;
  gboolean changed = FALSE;

  handler_id = g_signal_connect_swapped (instance, signal_name,
                                         G_CALLBACK (set_true_cb), &changed);
  while (!changed)
    g_main_context_iteration (NULL, TRUE);
  g_signal_handler_disconnect (instance, handler_id);
}

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
  test->keyboard = clutter_seat_create_virtual_device (seat,
                                                       CLUTTER_KEYBOARD_DEVICE);

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
                           StackFilter     filter,
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

      if ((filter & STACK_FILTER_SHOWING) && window && window->hidden)
        continue;

      if (window && workspace && !meta_window_located_on_workspace (window, workspace))
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
maybe_multiply (const char  *str,
                int          value,
                const char **out_str)
{
  *out_str = str;

  if (str[0] == '*')
    {
      double multiplier;

      str += 1;
      multiplier = g_strtod (str, (char **) out_str);

      value = (int) round (multiplier * value);
    }

  return value;
}

static int
maybe_divide (const char  *str,
              int          value,
              const char **out_str)
{
  *out_str = str;

  if (str[0] == '/')
    {
      double divider;

      str += 1;
      divider = g_strtod (str, (char **) out_str);

      value = (int) round (value / divider);
    }

  return value;
}

static int
maybe_add (const char  *str,
           int          value,
           const char **out_str)
{
  *out_str = str;

  if (str[0] == '+')
    {
      double term;

      str += 1;
      term = g_strtod (str, (char **) out_str);

      value = (int) round (value + term);
    }

  return value;
}

static int
maybe_subtract (const char  *str,
                int          value,
                const char **out_str)
{
  *out_str = str;

  if (str[0] == '-')
    {
      double term;

      str += 1;
      term = g_strtod (str, (char **) out_str);

      value = (int) round (value - term);
    }

  return value;
}

static int
maybe_do_math (const char  *str,
               int          value,
               const char **out_str)
{
  switch (str[0])
    {
    case '*':
      value = maybe_multiply (str, value, &str);
      break;
    case '/':
      value = maybe_divide (str, value, &str);
      break;
    case '+':
      value = maybe_add (str, value, &str);
      break;
    case '-':
      value = maybe_subtract (str, value, &str);
      break;
    default:
      *out_str = str;
      return value;
    }

  return maybe_do_math (str, value, out_str);
}

static int
parse_monitor_size (MtkRectangle *logical_monitor_layout,
                    const char   *size_str)
{
  int value;

  if (strstr (size_str, "MONITOR_WIDTH") == size_str)
    {
      value = logical_monitor_layout->width;
      size_str += strlen ("MONITOR_WIDTH");
      value = maybe_do_math (size_str, value, &size_str);
    }
  else if (strstr (size_str, "MONITOR_HEIGHT") == size_str)
    {
      value = logical_monitor_layout->height;
      size_str += strlen ("MONITOR_HEIGHT");
      value = maybe_do_math (size_str, value, &size_str);
    }
  else
    {
      value = atoi (size_str);
    }

  return value;
}

static int
parse_window_size (MetaWindow *window,
                   const char *size_str)
{
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle logical_monitor_layout;

  logical_monitor = meta_window_find_monitor_from_frame_rect (window);
  g_assert_nonnull (logical_monitor);

  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);

  return parse_monitor_size (&logical_monitor_layout, size_str);
}

static MetaLogicalMonitor *
get_logical_monitor (TestCase    *test,
                     const char  *monitor_id,
                     GError     **error)
{
  MetaBackend *backend = meta_context_get_backend (test->context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaDisplay *display = meta_context_get_display (test->context);
  MetaWindow *focus_window;
  MetaLogicalMonitor *logical_monitor;

  if (monitor_id)
    {
      MetaVirtualMonitor *virtual_monitor;
      MetaOutput *output;
      MetaMonitor *monitor;

      virtual_monitor =
        g_hash_table_lookup (test->virtual_monitors, monitor_id);

      if (!virtual_monitor)
        {
          g_set_error (error, META_TEST_CLIENT_ERROR,
                       META_TEST_CLIENT_ERROR_BAD_COMMAND,
                       "Unknown monitor %s", monitor_id);
          return NULL;
        }

      output = meta_virtual_monitor_get_output (virtual_monitor);
      monitor = meta_output_get_monitor (output);

      return meta_monitor_get_logical_monitor (monitor);
    }

  focus_window = meta_display_get_focus_window (display);
  logical_monitor = meta_window_get_main_logical_monitor (focus_window);

  if (logical_monitor)
    return logical_monitor;

  return meta_monitor_manager_get_primary_logical_monitor (monitor_manager);
}

static gboolean
str_to_bool (const char *str,
             gboolean   *val)
{
  if (g_ascii_strcasecmp (str, "true") == 0)
    {
      if (val != NULL)
        *val = TRUE;
      return TRUE;
    }

  if (g_ascii_strcasecmp (str, "false") == 0)
    {
      if (val != NULL)
        *val = FALSE;
      return TRUE;
    }

  return FALSE;
}

static gboolean
str_to_side (const char *str,
             MetaSide   *out_side)
{
  if (g_str_equal (str, "left"))
    {
      *out_side = META_SIDE_LEFT;
      return TRUE;
    }

  if (g_str_equal (str, "right"))
    {
      *out_side = META_SIDE_RIGHT;
      return TRUE;
    }

  if (g_str_equal (str, "top"))
    {
      *out_side = META_SIDE_TOP;
      return TRUE;
    }

  if (g_str_equal (str, "bottom"))
    {
      *out_side = META_SIDE_BOTTOM;
      return TRUE;
    }

  return FALSE;
}

static gboolean
test_case_add_strut (TestCase    *test,
                     int          x,
                     int          y,
                     int          width,
                     int          height,
                     MetaSide     side,
                     GError     **error)
{
  MetaDisplay *display = meta_context_get_display (test->context);
  MetaWorkspaceManager *workspace_manager =
    meta_display_get_workspace_manager (display);
  MtkRectangle rect = { x, y, width, height };
  MetaStrut strut = { rect, side };
  GList *workspaces =
    meta_workspace_manager_get_workspaces (workspace_manager);
  GList *l;

  for (l = workspaces; l; l = l->next)
    {
      MetaWorkspace *workspace = l->data;
      g_autoptr (GSList) struts_list = NULL;
      g_autoslist (MetaStrut) struts = NULL;

      struts_list = meta_workspace_get_builtin_struts (workspace);
      struts = g_slist_append (g_steal_pointer (&struts_list),
                               g_memdup2 (&strut, sizeof (MetaStrut)));
      meta_workspace_set_builtin_struts (workspace, struts);
    }

  wait_for_signal_emission (display, "workareas-changed");

  return TRUE;
}

static gboolean
test_case_clear_struts (TestCase  *test,
                        MetaSide   side,
                        GError   **error)
{
  MetaDisplay *display = meta_context_get_display (test->context);
  MetaWorkspaceManager *workspace_manager =
    meta_display_get_workspace_manager (display);
  GList *workspaces =
    meta_workspace_manager_get_workspaces (workspace_manager);
  GList *l;

  for (l = workspaces; l; l = l->next)
    {
      MetaWorkspace *workspace = l->data;
      g_autoslist (MetaStrut) struts = NULL;

      if (side != META_SIDE_TEST_CASE_NONE)
        {
          GSList *sl;

          struts = meta_workspace_get_builtin_struts (workspace);

          for (sl = struts; sl;)
            {
              MetaStrut *strut = sl->data;
              GSList *old;

              old = sl;
              sl = sl->next;

              if (strut->side == side)
                {
                  struts = g_slist_remove_link (struts, old);
                  g_clear_pointer (&strut, g_free);
                }
            }
        }

      meta_workspace_set_builtin_struts (workspace, struts);
    }

  return TRUE;
}

typedef struct
{
  TestCase *test_case;
  const char *filename;
  int line_no;
  int argc;
  char **argv;
  GError **error;
  GObject *instance;
  gulong handler_id;
} TestCaseArgs;

static void
test_case_signal_cb (TestCaseArgs *test_case_args)
{
  g_autoptr (GError) error = NULL;

  g_signal_handler_disconnect (test_case_args->instance,
                               test_case_args->handler_id);

  if (!test_case_do (test_case_args->test_case,
                     test_case_args->filename,
                     test_case_args->line_no,
                     test_case_args->argc,
                     test_case_args->argv,
                     &error))
    g_warning ("Failed to run test command in signal handler: %s",
               error->message);

  g_strfreev (test_case_args->argv);
  g_free (test_case_args);
}

static gboolean
test_case_parse_signal (TestCase *test,
                        int       argc,
                        char    **argv,
                        char    **out_signal_name,
                        GObject **out_signal_instance,
                        GError  **error)
{
  const char *signal_start;
  GObject *instance_obj = NULL;
  const char *signal_name;

  *out_signal_instance = NULL;
  *out_signal_name = NULL;

  if (argc < 3 || !g_str_equal (argv[1], "=>"))
    BAD_COMMAND ("usage: [window-id]::signal => command");

  signal_start = strstr (argv[0], "::");
  if (!signal_start)
    BAD_COMMAND ("Invalid syntax, no signal parameter");

  signal_name = signal_start + 2;

  if (!strlen (signal_name))
    BAD_COMMAND ("Invalid syntax, empty signal name");

  if (signal_start != argv[0])
    {
      g_autoptr (GError) local_error = NULL;
      g_autofree char *instance = g_strndup (argv[0], signal_start - argv[0]);
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;

      if (!test_case_parse_window_id (test, instance, &client,
                                      &window_id, &local_error))
        {
          BAD_COMMAND ("Cannot find window for instance %s: %s",
                       instance, local_error->message);
        }

      window = meta_test_client_find_window (client, window_id, &local_error);
      if (!window)
        {
          BAD_COMMAND ("Cannot find window for window id %s: %s",
                       window_id, local_error->message);
        }

      instance_obj = G_OBJECT (window);
    }

  if (!instance_obj)
    {
      if (g_str_equal (signal_name, "monitors-changed"))
        {
          MetaBackend *backend = meta_context_get_backend (test->context);
          MetaMonitorManager *monitor_manager =
            meta_backend_get_monitor_manager (backend);

          instance_obj = G_OBJECT (monitor_manager);
        }
      else
        {
          BAD_COMMAND ("Unknown global signal name '%s'", signal_name);
        }
    }

  if (!g_signal_lookup (signal_name, G_TYPE_FROM_INSTANCE (instance_obj)))
    {
      BAD_COMMAND ("No signal '%s' in object of type %s",
                   signal_name,
                   g_type_name_from_instance ((GTypeInstance *) instance_obj));
    }

  *out_signal_instance = g_object_ref (instance_obj);
  *out_signal_name = g_strdup (signal_name);

  return TRUE;
}

static MetaGrabOp
grab_op_from_edge (const char *edge)
{
  MetaGrabOp op = META_GRAB_OP_WINDOW_BASE;

  if (strcmp (edge, "top") == 0)
    op |= META_GRAB_OP_WINDOW_DIR_NORTH;
  else if (strcmp (edge, "bottom") == 0)
    op |= META_GRAB_OP_WINDOW_DIR_SOUTH;
  else if (strcmp (edge, "left") == 0)
    op |= META_GRAB_OP_WINDOW_DIR_WEST;
  else if (strcmp (edge, "right") == 0)
    op |= META_GRAB_OP_WINDOW_DIR_EAST;

  return op;
}

static gboolean
is_popup (gconstpointer a,
          gconstpointer b)
{
  MetaWindow *window = META_WINDOW (a);

  switch (meta_window_get_window_type (window))
    {
    case META_WINDOW_DROPDOWN_MENU:
    case META_WINDOW_POPUP_MENU:
      return TRUE;
    default:
      return FALSE;
    }
}

static MetaWindow *
find_popup (MetaWindow *window)
{
  GPtrArray *transient_children;
  unsigned int i;

  transient_children = meta_window_get_transient_children (window);
  if (!transient_children)
    return NULL;

  if (!g_ptr_array_find_with_equal_func (transient_children, NULL,
                                         is_popup, &i))
    return NULL;

  window = g_ptr_array_index (transient_children, i);
  return window;
}

static gboolean
track_popup (TestCase        *test,
             MetaTestClient  *client,
             const char      *window_id,
             const char      *parent_id,
             GError         **error)
{
  MetaWindow *parent;
  MetaWindow *popup;

  if (!test->popups)
    {
      test->popups = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, g_free);
    }

  g_hash_table_insert (test->popups,
                       g_strdup (window_id), g_strdup (parent_id));

  parent = meta_test_client_find_window (client, parent_id, error);
  if (!parent)
    return FALSE;

  if (meta_test_client_get_client_type (client) ==
      META_WINDOW_CLIENT_TYPE_WAYLAND)
    {
      g_autofree char *popup_title = NULL;

      while (TRUE)
        {
          popup = find_popup (parent);
          if (popup)
            break;

          g_main_context_iteration (NULL, TRUE);
        }

      popup_title = g_strdup_printf ("test/%s/%s",
                                     meta_test_client_get_id (client),
                                     window_id);
      meta_window_set_title (popup, popup_title);
    }
  else
    {
      if (!test_case_wait (test, error))
        return FALSE;

      popup = meta_test_client_find_window (client, window_id, error);
      if (!popup)
        return FALSE;
    }

  meta_wait_for_window_shown (popup);

  if (!test_case_wait (test, error))
    return FALSE;

  return TRUE;
}

static gboolean
logical_monitor_config_has_connector (MetaLogicalMonitorConfig *logical_monitor_config,
                                      const char               *connector)
{
  GList *l;

  for (l = logical_monitor_config->monitor_configs; l; l = l->next)
    {
      MetaMonitorConfig *monitor_config = l->data;

      if (g_strcmp0 (monitor_config->monitor_spec->connector, connector) == 0)
        return TRUE;
    }

  return FALSE;
}

static MetaLogicalMonitorConfig *
find_logical_monitor_config (MetaMonitorsConfig *config,
                             const char         *connector)
{
  GList *l;

  for (l = config->logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;

      if (logical_monitor_config_has_connector (logical_monitor_config,
                                                connector))
        return logical_monitor_config;
    }
  return NULL;
}

typedef struct _PointerMotionInterpolation
{
  TestCase *test;
  ClutterInterval *interval_x;
  ClutterInterval *interval_y;
  float last_x;
  float last_y;
} PointerMotionInterpolation;

static void
on_pointer_motion_frame (ClutterTimeline            *timeline,
                         int                         elapsed_ms,
                         PointerMotionInterpolation *interpolation)
{
  ClutterVirtualInputDevice *pointer = interpolation->test->pointer;
  float progress;
  const GValue *x_value, *y_value;
  float x, y;

  progress = (float) elapsed_ms / clutter_timeline_get_duration (timeline);
  x_value = clutter_interval_compute (interpolation->interval_x, progress);
  y_value = clutter_interval_compute (interpolation->interval_y, progress);
  x = g_value_get_float (x_value);
  y = g_value_get_float (y_value);

  if (x == interpolation->last_x &&
      y == interpolation->last_y)
    return;

  interpolation->last_x = x;
  interpolation->last_y = y;

  clutter_virtual_input_device_notify_absolute_motion (pointer,
                                                       CLUTTER_CURRENT_TIME,
                                                       x, y);
  meta_flush_input (interpolation->test->context);
}

static gboolean
interpolate_pointer_motion (TestCase  *test,
                            float      x,
                            float      y,
                            uint32_t   duration_ms,
                            GError   **error)
{
  ClutterSeat *seat =
    clutter_virtual_input_device_get_seat (test->pointer);
  MetaBackend *backend = meta_context_get_backend (test->context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  PointerMotionInterpolation interpolation = {
    .test = test,
  };
  g_autoptr (ClutterTimeline) timeline = NULL;
  graphene_point_t source;

  clutter_seat_query_state (seat, NULL, &source, NULL);
  interpolation.interval_x = clutter_interval_new (G_TYPE_FLOAT,
                                                   source.x, x);
  interpolation.interval_y = clutter_interval_new (G_TYPE_FLOAT,
                                                   source.y, y);

  timeline = clutter_timeline_new_for_actor (stage, duration_ms);
  g_signal_connect (timeline, "new-frame", G_CALLBACK (on_pointer_motion_frame),
                    &interpolation);
  clutter_timeline_start (timeline);
  while (clutter_timeline_is_playing (timeline))
    g_main_context_iteration (NULL, TRUE);

  g_object_unref (interpolation.interval_x);
  g_object_unref (interpolation.interval_y);

  if (!test_case_dispatch (test, error))
    return FALSE;

  return TRUE;
}

static gboolean
warp_pointer_to (TestCase  *test,
                 float      x,
                 float      y,
                 GError   **error)
{
  clutter_virtual_input_device_notify_absolute_motion (test->pointer,
                                                       CLUTTER_CURRENT_TIME,
                                                       x, y);
  meta_flush_input (test->context);
  if (!test_case_dispatch (test, error))
    return FALSE;

  return TRUE;
}

static graphene_point_t *
point_copy (const graphene_point_t *point)
{
  return graphene_point_init_from_point (graphene_point_alloc (), point);
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
        BAD_COMMAND ("usage: new_client <client-id> [wayland|x11]");

      if (strcmp (argv[2], "x11") == 0)
        type = META_WINDOW_CLIENT_TYPE_X11;
      else if (strcmp (argv[2], "wayland") == 0)
        type = META_WINDOW_CLIENT_TYPE_WAYLAND;
      else
        BAD_COMMAND ("usage: new_client <client-id> [wayland|x11]");

      if (g_hash_table_lookup (test->clients, argv[1]))
        BAD_COMMAND ("client %s already exists", argv[1]);

      client = meta_test_client_new (test->context, argv[1], type, error);
      if (!client)
        return FALSE;

      g_hash_table_insert (test->clients, meta_test_client_get_id (client), client);
    }
  else if (strcmp (argv[0], "quit_client") == 0)
    {
      if (argc != 2)
        BAD_COMMAND ("usage: quit_client <client-id>");

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
        BAD_COMMAND ("usage: %s <client-id>/<window-id > [override|csd]", argv[0]);

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
        {
          BAD_COMMAND ("usage: %s <client-id>/<window-id> <parent-window-id>",
                       argv[0]);
        }

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
        BAD_COMMAND ("usage: %s <client-id>/<window-id> [true|false]", argv[0]);

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
        BAD_COMMAND ("usage: %s <client-id>/<window-id> [true|false]", argv[0]);

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
        BAD_COMMAND ("usage: %s <client-id>/<window-id> [true|false]", argv[0]);

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
        BAD_COMMAND ("usage: %s <client-id>/<window-id> [async]", argv[0]);

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
        meta_wait_for_window_shown (window);
    }
  else if (strcmp (argv[0], "sync_shown") == 0)
    {
      MetaWindow *window;
      MetaTestClient *client;
      const char *window_id;

      if (argc != 2)
        BAD_COMMAND ("usage: %s <client-id>/<window-id>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      meta_wait_for_window_shown (window);
    }
  else if (strcmp (argv[0], "resize") == 0 ||
           strcmp (argv[0], "resize_ignore_titlebar") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;
      int width, height;
      g_autofree char *width_str = NULL;
      g_autofree char *height_str = NULL;

      if (argc != 4)
        BAD_COMMAND ("usage: %s <client-id>/<window-id> width height", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, NULL);
      if (window)
        {
          width = parse_window_size (window, argv[2]);
          height = parse_window_size (window, argv[3]);
        }
      else
        {
          width = atoi (argv[2]);
          height = atoi (argv[3]);
        }
      if (width == 0 || height == 0)
        BAD_COMMAND ("Invalid resize dimension %s x %s", argv[2], argv[3]);

      width_str = g_strdup_printf ("%d", width);
      height_str = g_strdup_printf ("%d", height);

      if (!meta_test_client_do (client, error, argv[0], window_id,
                                width_str, height_str, NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "x11_geometry") == 0)
    {
      MetaTestClient *client;
      const char *window_id;

      if (argc != 3)
        BAD_COMMAND ("usage: %s <client-id>/<window-id> <x11-geometry>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!meta_test_client_do (client, error, argv[0], window_id,
                                argv[2], NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "begin_resize") == 0)
    {
      MetaBackend *backend = meta_context_get_backend (test->context);
      ClutterBackend *clutter_backend =
        meta_backend_get_clutter_backend (backend);
      ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
      ClutterSprite *sprite;
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;
      MetaGrabOp grab_op;
      MtkRectangle rect;
      gboolean ret;
      graphene_point_t grab_origin;
      MetaWindowDrag *window_drag;

      if (argc != 3)
        BAD_COMMAND ("usage: %s <client-id>/<window-id> [top|bottom|left|right]", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);

      grab_op = grab_op_from_edge (argv[2]);

      meta_window_get_frame_rect (window, &rect);

      grab_origin = GRAPHENE_POINT_INIT (rect.x + rect.width / 2.0f,
                                         rect.y + rect.height / 2.0f);

      if (!warp_pointer_to (test, grab_origin.x, grab_origin.y, error))
        return FALSE;

      window_drag =
        meta_compositor_get_current_window_drag (window->display->compositor);
      g_assert_null (window_drag);

      sprite = clutter_backend_get_pointer_sprite (clutter_backend, stage);
      ret = meta_window_begin_grab_op (window,
                                       grab_op,
                                       sprite,
                                       meta_display_get_current_time_roundtrip (window->display),
                                       &grab_origin);
      g_assert_true (ret);

      window_drag =
        meta_compositor_get_current_window_drag (window->display->compositor);
      g_assert_nonnull (window_drag);
      g_assert_true (meta_window_drag_get_window (window_drag) == window);
      g_object_set_data_full (G_OBJECT (window_drag), "test-resize-drag",
                              point_copy (&grab_origin),
                              (GDestroyNotify) graphene_point_free);
    }
  else if (strcmp (argv[0], "update_resize") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;
      MtkRectangle rect;
      float delta_x, delta_y;
      graphene_point_t *grab_origin;
      MetaWindowDrag *window_drag;

      if (argc != 4)
        BAD_COMMAND ("usage: %s <client-id>/<window-id> <x> <y>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);

      meta_window_get_frame_rect (window, &rect);
      delta_x = (float) atof (argv[2]);
      delta_y = (float) atof (argv[3]);

      window_drag =
        meta_compositor_get_current_window_drag (window->display->compositor);
      g_assert_nonnull (window_drag);
      g_assert_true (meta_window_drag_get_window (window_drag) == window);

      grab_origin = g_object_get_data (G_OBJECT (window_drag),
                                       "test-resize-drag");
      g_assert_nonnull (grab_origin);
      if (!warp_pointer_to (test,
                            grab_origin->x + delta_x,
                            grab_origin->y + delta_y, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "end_resize") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;
      MetaWindowDrag *window_drag;

      if (argc != 2)
        BAD_COMMAND ("usage: %s <client-id>/<window-id>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);

      window_drag =
        meta_compositor_get_current_window_drag (window->display->compositor);
      g_assert_nonnull (window_drag);
      g_assert_true (meta_window_drag_get_window (window_drag) == window);

      meta_window_drag_end (window_drag);
    }
  else if (strcmp (argv[0], "move") == 0)
    {
      MetaWindow *window;

      if (argc != 4)
        BAD_COMMAND ("usage: %s <client-id>/<window-id> x y", argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      meta_window_move_frame (window, TRUE, atoi (argv[2]), atoi (argv[3]));
    }
  else if (strcmp (argv[0], "move_to_monitor") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;
      MetaLogicalMonitor *logical_monitor;

      if (argc != 3)
        BAD_COMMAND ("usage: %s <client-id>/<window-id> <monitor-id>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      logical_monitor = get_logical_monitor (test, argv[2], error);
      if (!logical_monitor)
        BAD_COMMAND ("Unknown monitor %s", argv[1]);

      meta_window_move_to_monitor (window, logical_monitor->number);
    }
  else if (strcmp (argv[0], "tile") == 0)
    {
      MetaWindow *window;

      if (argc != 3)
        BAD_COMMAND ("usage: %s <client-id>/<window-id> [right|left]", argv[0]);

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
        BAD_COMMAND ("usage: %s <client-id>/<window-id>", argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      meta_window_untile (window);
    }
  else if (strcmp (argv[0], "set_maximize_flag") == 0)
    {
      MetaWindow *window;
      MetaTestClient *client;
      const char *window_id;
      MetaMaximizeFlags flags;

      if (argc != 3)
        BAD_COMMAND ("usage: %s <client-id>/<window-id> [vertically|horizontally]", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      if (strcmp (argv[2], "vertically") == 0)
        {
          flags = META_MAXIMIZE_VERTICAL;
        }
      else if (strcmp (argv[2], "horizontally") == 0)
        {
          flags = META_MAXIMIZE_HORIZONTAL;
        }
      else
        {
          g_set_error (error,
                       META_TEST_CLIENT_ERROR,
                       META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                       "Invalid tile mode '%s'", argv[2]);
          return FALSE;
        }

      meta_window_set_maximize_flags (window, flags);
    }
  else if (strcmp (argv[0], "hide") == 0 ||
           strcmp (argv[0], "activate") == 0 ||
           strcmp (argv[0], "raise") == 0 ||
           strcmp (argv[0], "lower") == 0 ||
           strcmp (argv[0], "minimize") == 0 ||
           strcmp (argv[0], "unminimize") == 0 ||
           strcmp (argv[0], "maximize") == 0 ||
           strcmp (argv[0], "unmaximize") == 0 ||
           strcmp (argv[0], "unfullscreen") == 0 ||
           strcmp (argv[0], "set_modal") == 0 ||
           strcmp (argv[0], "unset_modal") == 0 ||
           strcmp (argv[0], "freeze") == 0 ||
           strcmp (argv[0], "thaw") == 0 ||
           strcmp (argv[0], "destroy") == 0)
    {
      if (argc != 2)
        BAD_COMMAND ("usage: %s <client-id>/<window-id>", argv[0]);

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!meta_test_client_do (client, error, argv[0], window_id, NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "fullscreen") == 0)
    {
      MetaTestClient *client;
      const char *window_id;

      if (argc != 2 && argc != 3)
        BAD_COMMAND ("usage: %s <client-id>/<window-id> [<connector>]", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (argc == 3)
        {
          MetaVirtualMonitor *virtual_monitor;
          MetaOutput *output;

          virtual_monitor = g_hash_table_lookup (test->virtual_monitors,
                                                 argv[2]);
          if (!virtual_monitor)
            BAD_COMMAND ("Unknown monitor %s", argv[2]);

          output = meta_virtual_monitor_get_output (virtual_monitor);
          if (!meta_test_client_do (client, error, argv[0], window_id,
                                    meta_output_get_name (output),
                                    NULL))
            return FALSE;
        }
      else
        {
          if (!meta_test_client_do (client, error, argv[0], window_id, NULL))
            return FALSE;
        }
    }
  else if (strcmp (argv[0], "local_activate") == 0)
    {
      MetaWindow *window;

      if (argc != 2)
        BAD_COMMAND ("usage: %s <client-id>/<window-id>", argv[0]);

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
        BAD_COMMAND ("usage: %s", argv[0]);

      if (!test_case_wait (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "wait_reconfigure") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      g_autoptr (GPtrArray) windows = NULL;
      g_autoptr (GArray) serials = NULL;
      int i;
      gboolean has_x11_window = FALSE;
      gboolean has_unfinished_configurations;

      if (argc < 2)
        BAD_COMMAND ("usage: %s [<client-id>/<window-id>..]", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      /*
       * 1. Wait once to reconfigure
       * 2. Wait for window to receive back any pending configuration
       */

      if (!test_case_wait (test, error))
        return FALSE;

      windows = g_ptr_array_new ();
      serials = g_array_new (FALSE, FALSE, sizeof (uint32_t));

      for (i = 1; i < argc; i++)
        {
          MetaWindow *window;

          window = meta_test_client_find_window (client, window_id, error);
          if (!window)
            return FALSE;

          if (META_IS_WINDOW_WAYLAND (window))
            {
              MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
              uint32_t serial;

              if (meta_window_wayland_get_pending_serial (wl_window, &serial))
                {
                  g_ptr_array_add (windows, window);
                  g_array_append_val (serials, serial);
                }
            }
          else
            {
              has_x11_window = TRUE;
            }
        }

      if (has_x11_window)
        {
          /* There is no reliable configure tracking on X11, just make a
           * genuien attempt, by first making sure pending operations have
           * reached us, that we have flushed any outgoing data, and that any
           * new pending operation from that has reached us. */
          if (!test_case_wait (test, error))
            return FALSE;
          if (!test_case_dispatch (test, error))
            return FALSE;
          if (!test_case_wait (test, error))
            return FALSE;
        }

      while (TRUE)
        {
          has_unfinished_configurations = FALSE;
          for (i = 0; i < windows->len; i++)
            {
              MetaWindowWayland *wl_window = g_ptr_array_index (windows, i);
              uint32_t serial = g_array_index (serials, uint32_t, i);

              if (meta_window_wayland_peek_configuration (wl_window, serial))
                {
                  has_unfinished_configurations = TRUE;
                  break;
                }
            }

          if (has_unfinished_configurations)
            g_main_context_iteration (NULL, TRUE);
          else
            break;
        }
    }
  else if (strcmp (argv[0], "wait_size") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;
      int width, height;

      if (argc != 4)
        BAD_COMMAND ("usage: %s <client-id>/<window-id> <width> <height>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);

      width = atoi (argv[2]);
      height = atoi (argv[3]);

      while (TRUE)
        {
          MtkRectangle rect;

          meta_window_get_frame_rect (window, &rect);
          if (rect.width == width && rect.height == height)
            break;

          g_main_context_iteration (NULL, TRUE);
        }
    }
  else if (strcmp (argv[0], "dispatch") == 0)
    {
      if (argc != 1)
        BAD_COMMAND ("usage: %s", argv[0]);

      if (!test_case_dispatch (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "sleep") == 0)
    {
      uint64_t interval_ms;

      if (argc != 2)
        BAD_COMMAND ("usage: %s <milliseconds>|<known-time>", argv[0]);

      if (strcmp (argv[1], "suspend_timeout") == 0)
        interval_ms = s2ms (meta_get_window_suspend_timeout_s ());
      else if (!g_ascii_string_to_unsigned (argv[1], 10, 0, G_MAXUINT32,
                                            &interval_ms, error))
        return FALSE;

      if (!test_case_sleep (test, (uint32_t) interval_ms, error))
        return FALSE;
    }
  else if (g_str_equal (argv[0], "add_strut") ||
           g_str_equal (argv[0], "set_strut"))
    {
      if (argc < 6 || argc > 7)
        {
          BAD_COMMAND ("usage: %s <x> <y> <width> <height> <side> [monitor-id]",
                       argv[0]);
        }

      MetaLogicalMonitor *logical_monitor;
      const char *monitor_id = argc > 6 ? argv[6] : NULL;

      logical_monitor = get_logical_monitor (test, monitor_id, error);
      if (!logical_monitor)
        return FALSE;

      if (g_str_equal (argv[0], "set_strut"))
        {
          if (!test_case_clear_struts (test, META_SIDE_TEST_CASE_NONE, error))
            return FALSE;
        }

      MtkRectangle monitor_layout =
        meta_logical_monitor_get_layout (logical_monitor);

      int x = parse_monitor_size (&monitor_layout, argv[1]);
      int y = parse_monitor_size (&monitor_layout, argv[2]);
      int width = parse_monitor_size (&monitor_layout, argv[3]);
      int height = parse_monitor_size (&monitor_layout, argv[4]);

      MetaSide side;
      if (!str_to_side (argv[5], &side))
        BAD_COMMAND ("Invalid side: %s", argv[5]);

      if (!test_case_add_strut (test, x, y, width, height, side, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "clear_struts") == 0)
    {
      MetaSide side = META_SIDE_TEST_CASE_NONE;

      if (argc < 1 || argc > 2)
        BAD_COMMAND ("usage: %s [side]", argv[0]);

      if (argc > 1 && !str_to_side (argv[1], &side))
        BAD_COMMAND ("Invalid side: %s", argv[1]);

      if (!test_case_clear_struts (test, side, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "assert_stacking") == 0)
    {
      if (!test_case_assert_stacking (test, argv + 1, argc - 1,
                                      STACK_FILTER_ALL,
                                      NULL,
                                      error))
        return FALSE;

      if (!test_case_check_xserver_stacking (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "assert_stacking_showing") == 0)
    {
      if (!test_case_assert_stacking (test, argv + 1, argc - 1,
                                      STACK_FILTER_SHOWING,
                                      NULL,
                                      error))
        return FALSE;

      if (!test_case_check_xserver_stacking (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "assert_focused") == 0)
    {
      if (!test_case_assert_focused (test, argv[1], error))
        return FALSE;
    }
  else if (strcmp (argv[0], "wait_focused") == 0)
    {
      MetaDisplay *display = meta_context_get_display (test->context);
      MetaWindow *old_focus;
      const char *expected_window;

      if (argc != 2)
        BAD_COMMAND ("usage: %s <client-id>/<window-id>|none", argv[0]);

      expected_window = argv[1];
      old_focus = display->focus_window;

      if (g_strcmp0 (expected_window, "none") == 0)
        {
          while (TRUE)
            {
              if (display->focus_window &&
                  display->focus_window != old_focus)
                {
                  const char *focused = display->focus_window->title;

                  if (g_str_has_prefix (focused, "test/"))
                    focused += 5;

                  g_set_error (error,
                               META_TEST_CLIENT_ERROR,
                               META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                               "focus: expected='none', actual='%s'",
                               focused);
                  return FALSE;
                }
              else if (!display->focus_window)
                {
                  break;
                }

              g_main_context_iteration (NULL, TRUE);
            }
        }
      else
        {
          while (TRUE)
            {
              if (display->focus_window != old_focus &&
                  !display->focus_window)
                {
                  g_set_error (error,
                               META_TEST_CLIENT_ERROR,
                               META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                               "focus: expected='%s', actual='none'",
                               expected_window);
                  return FALSE;
                }
              else if (display->focus_window)
                {
                  const char *focused;

                  focused = display->focus_window->title;
                  if (g_str_has_prefix (focused, "test/"))
                    focused += 5;

                  if (g_strcmp0 (focused, expected_window) == 0)
                    {
                      break;
                    }
                  else if (old_focus != display->focus_window)
                    {
                      g_set_error (error,
                                   META_TEST_CLIENT_ERROR,
                                   META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                                   "focus: expected='%s', actual='%s'",
                                   expected_window, focused);
                      return FALSE;
                    }
                }

              g_main_context_iteration (NULL, TRUE);
            }
        }
    }
  else if (strcmp (argv[0], "assert_keyboard_focus") == 0)
    {
      MetaWaylandCompositor *wayland_compositor;
      MetaWaylandKeyboard *wayland_keyboard;
      MetaWaylandSurface *focus_surface;
      struct wl_resource *focus_surface_resource = NULL;

      if (argc != 2)
        BAD_COMMAND ("usage: %s <client-id>/<window-id>|none", argv[0]);

      wayland_compositor = meta_context_get_wayland_compositor (test->context);
      wayland_keyboard = wayland_compositor->seat->keyboard;
      focus_surface =
        meta_wayland_keyboard_get_focus_surface (wayland_keyboard);
      if (focus_surface)
        {
          focus_surface_resource =
            meta_wayland_surface_get_resource (focus_surface);
        }

      if (g_strcmp0 (argv[1], "none") == 0)
        {
          if (focus_surface)
            {
              g_set_error (error,
                           META_TEST_CLIENT_ERROR,
                           META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                           "Expected no keyboard focus, but found wl_surface#%d",
                           wl_resource_get_id (focus_surface_resource));
              return FALSE;
            }
        }
      else
        {
          MetaTestClient *client;
          const char *window_id;
          MetaWindow *window;
          MetaWaylandSurface *surface;
          struct wl_resource *surface_resource;

          if (!test_case_parse_window_id (test, argv[1],
                                          &client, &window_id, error))
            return FALSE;

          if (meta_test_client_get_client_type (client) !=
              META_WINDOW_CLIENT_TYPE_WAYLAND)
            BAD_COMMAND ("%s only works with Wayland clients", argv[0]);

          window = meta_test_client_find_window (client, window_id, error);
          if (!window)
            return FALSE;

          surface = meta_window_get_wayland_surface (window);
          surface_resource = meta_wayland_surface_get_resource (surface);

          if (focus_surface &&
              focus_surface != surface)
            {
              g_set_error (error,
                           META_TEST_CLIENT_ERROR,
                           META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                           "Expected keyboard focus wl_surface#%d, "
                           "but found wl_surface#%d",
                           wl_resource_get_id (surface_resource),
                           wl_resource_get_id (focus_surface_resource));
              return FALSE;
            }
          else if (!focus_surface)
            {
              g_set_error (error,
                           META_TEST_CLIENT_ERROR,
                           META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                           "Expected keyboard focus wl_surface#%d, but found none",
                           wl_resource_get_id (surface_resource));
              return FALSE;
            }
        }
    }
  else if (strcmp (argv[0], "assert_pointer_focus") == 0)
    {
      MetaWaylandCompositor *wayland_compositor;
      MetaWaylandPointer *wayland_pointer;
      MetaWaylandSurface *focus_surface;
      struct wl_resource *focus_surface_resource = NULL;

      if (argc != 2)
        BAD_COMMAND ("usage: %s <client-id>/<window-id>|none", argv[0]);

      wayland_compositor = meta_context_get_wayland_compositor (test->context);
      wayland_pointer = wayland_compositor->seat->pointer;
      focus_surface = meta_wayland_pointer_get_focus_surface (wayland_pointer);
      if (focus_surface)
        {
          focus_surface_resource =
            meta_wayland_surface_get_resource (focus_surface);
        }

      if (g_strcmp0 (argv[1], "none") == 0)
        {
          if (focus_surface)
            {
              g_set_error (error,
                           META_TEST_CLIENT_ERROR,
                           META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                           "Expected no pointer focus, but found wl_surface#%d",
                           wl_resource_get_id (focus_surface_resource));
              return FALSE;
            }
        }
      else
        {
          MetaTestClient *client;
          const char *window_id;
          MetaWindow *window;
          MetaWaylandSurface *surface;
          struct wl_resource *surface_resource;

          if (!test_case_parse_window_id (test, argv[1],
                                          &client, &window_id, error))
            return FALSE;

          if (meta_test_client_get_client_type (client) !=
              META_WINDOW_CLIENT_TYPE_WAYLAND)
            BAD_COMMAND ("%s only works with Wayland clients", argv[0]);

          window = meta_test_client_find_window (client, window_id, error);
          if (!window)
            return FALSE;

          surface = meta_window_get_wayland_surface (window);
          surface_resource = meta_wayland_surface_get_resource (surface);

          if (focus_surface &&
              focus_surface != surface)
            {
              g_set_error (error,
                           META_TEST_CLIENT_ERROR,
                           META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                           "Expected pointer focus wl_surface#%d, "
                           "but found wl_surface#%d",
                           wl_resource_get_id (surface_resource),
                           wl_resource_get_id (focus_surface_resource));
              return FALSE;
            }
          else if (!focus_surface)
            {
              g_set_error (error,
                           META_TEST_CLIENT_ERROR,
                           META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                           "Expected pointer focus wl_surface#%d, but found none",
                           wl_resource_get_id (surface_resource));
              return FALSE;
            }
        }
    }
  else if (strcmp (argv[0], "assert_size") == 0)
    {
      MetaWindow *window;
      int width;
      int height;
      int client_window_width;
      int client_window_height;
      g_autofree char *width_str = NULL;
      g_autofree char *height_str = NULL;

      if (argc != 4)
        {
          BAD_COMMAND ("usage: %s <client-id>/<window-id> <width> <height>",
                       argv[0]);
        }

      MetaTestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      width = parse_window_size (window, argv[2]);
      height = parse_window_size (window, argv[3]);
      client_window_width = width;
      client_window_height = height;

      if (META_IS_WINDOW_X11 (window) && meta_window_x11_get_frame (window))
        {
          MetaFrameBorders frame_borders;

          g_assert_true (meta_window_x11_get_frame_borders (window,
                                                            &frame_borders));

          client_window_width -=
            frame_borders.visible.left + frame_borders.visible.right;
          client_window_height -=
            frame_borders.visible.top + frame_borders.visible.bottom;
        }

      width_str = g_strdup_printf ("%d", client_window_width);
      height_str = g_strdup_printf ("%d", client_window_height);

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
          BAD_COMMAND ("usage: %s <client-id>/<window-id> <x> <y>",
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
      int x = parse_window_size (window, argv[2]);
      int y = parse_window_size (window, argv[3]);
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
        BAD_COMMAND ("usage: %s <client-id>", argv[0]);

      MetaTestClient *client = test_case_lookup_client (test, argv[1], error);
      if (!client)
        return FALSE;

      if (!meta_test_client_do (client, error, argv[0], NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "clipboard-set") == 0)
    {
      if (argc != 4)
        BAD_COMMAND ("usage: %s <client-id> <mimetype> <text>", argv[0]);

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
  else if (strcmp (argv[0], "set_monitor_order") == 0)
    {
      MetaBackend *backend = meta_context_get_backend (test->context);
      MetaMonitorManager *monitor_manager =
        meta_backend_get_monitor_manager (backend);
      MetaMonitorsConfig *current_config;
      g_autoptr (MetaMonitorsConfig) new_config = NULL;
      int i;
      int total_width = 0;

      if (argc < 2)
        BAD_COMMAND ("usage: %s [<monitor-id>, ...]", argv[0]);

      current_config =
        meta_monitor_config_manager_get_current (monitor_manager->config_manager);
      new_config =
        meta_monitors_config_copy (current_config);

      for (i = 1; i < argc; i++)
        {
          MetaVirtualMonitor *virtual_monitor;
          MetaOutput *output;
          MetaLogicalMonitorConfig *logical_monitor_config;

          virtual_monitor =
            g_hash_table_lookup (test->virtual_monitors, argv[i]);
          if (!virtual_monitor)
            BAD_COMMAND ("Unknown monitor %s", argv[1]);

          output = meta_virtual_monitor_get_output (virtual_monitor);
          logical_monitor_config =
            find_logical_monitor_config (new_config,
                                         meta_output_get_name (output));
          logical_monitor_config->layout.x = total_width;
          total_width += logical_monitor_config->layout.width;
        }

      if (!meta_monitor_manager_apply_monitors_config (monitor_manager,
                                                       new_config,
                                                       META_MONITORS_CONFIG_METHOD_TEMPORARY,
                                                       error))
        return FALSE;
    }
  else if (strcmp (argv[0], "assert_window_main_monitor") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;
      MetaLogicalMonitor *logical_monitor;
      const char *monitor_id;

      if (argc != 3)
        BAD_COMMAND ("usage: %s <window-id> <monitor-id>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      monitor_id = argv[2];
      logical_monitor = get_logical_monitor (test, monitor_id, error);
      if (!logical_monitor)
        return FALSE;

      if (window->monitor != logical_monitor)
        {
          g_set_error (error,
                       META_TEST_CLIENT_ERROR,
                       META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                       "Monitor %s (%d, %dx%d+%d+%d) is not the primary monitor of window %s (%d, %dx%d+%d+%d)",
                       monitor_id,
                       logical_monitor->number,
                       logical_monitor->rect.width,
                       logical_monitor->rect.height,
                       logical_monitor->rect.x,
                       logical_monitor->rect.y,
                       window_id,
                       window->monitor->number,
                       window->monitor->rect.width,
                       window->monitor->rect.height,
                       window->monitor->rect.x,
                       window->monitor->rect.y);
          return FALSE;
        }
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
  else if (strcmp (argv[0], "reload_monitors") == 0)
    {
      MetaBackend *backend = meta_context_get_backend (test->context);
      MetaMonitorManager *monitor_manager =
        meta_backend_get_monitor_manager (backend);

      if (argc != 1)
        BAD_COMMAND ("usage: %s", argv[0]);

      meta_monitor_manager_reload (monitor_manager);
    }
  else if (strcmp (argv[0], "num_workspaces") == 0)
    {
      if (argc != 2)
        BAD_COMMAND ("usage: %s <num>", argv[0]);

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
        BAD_COMMAND ("usage: %s <workspace-index>", argv[0]);

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
        BAD_COMMAND ("usage: %s <workspace-index> <window-id>", argv[0]);

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
        BAD_COMMAND ("usage: %s <workspace-index> [<window-id1> ...]", argv[0]);

      MetaDisplay *display = meta_context_get_display (test->context);
      MetaWorkspaceManager *workspace_manager =
        meta_display_get_workspace_manager (display);

      int index = atoi (argv[1]);
      if (index >= meta_workspace_manager_get_n_workspaces (workspace_manager))
        return FALSE;

      MetaWorkspace *workspace =
        meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                       index);

      if (!test_case_assert_stacking (test, argv + 2, argc - 2,
                                      STACK_FILTER_ALL,
                                      workspace,
                                      error))
        return FALSE;

      if (!test_case_check_xserver_stacking (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "window_to_workspace") == 0)
    {
      if (argc != 3)
        BAD_COMMAND ("usage: %s <window-id> <workspace-index>", argv[0]);

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
        {
          BAD_COMMAND ("usage: %s <client-id>/<window-id> [true|false]",
                       argv[0]);
        }

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
  else if (strcmp (argv[0], "stick") == 0 ||
           strcmp (argv[0], "unstick") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;

      if (argc != 2)
        BAD_COMMAND ("usage: %s <client-id>/<window-id>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      if (g_strcmp0 (argv[0], "stick") == 0)
        meta_window_stick (window);
      else if (g_strcmp0 (argv[0], "unstick") == 0)
        meta_window_unstick (window);
    }
  else if (strcmp (argv[0], "assert_sticky") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      MetaWindow *window;
      gboolean should_be_sticky;
      gboolean is_sticky;

      if (argc != 3 || !str_to_bool (argv[2], &should_be_sticky))
        {
          BAD_COMMAND ("usage: %s <client-id>/<window-id> [true|false]",
                       argv[0]);
        }

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      is_sticky = meta_window_is_on_all_workspaces (window);
      if (should_be_sticky != is_sticky)
        {
          g_set_error (error,
                       META_TEST_CLIENT_ERROR,
                       META_TEST_CLIENT_ERROR_ASSERTION_FAILED,
                       "stickyness: expected %s, actually %s",
                       should_be_sticky ? "sticky" : "not sticky",
                       is_sticky ? "sticky" : "not sticky");
          return FALSE;
        }
    }
  else if (strcmp (argv[0], "focus_default_window") == 0)
    {
      if (argc != 1)
        BAD_COMMAND ("usage: %s", argv[0]);

      MetaDisplay *display = meta_context_get_display (test->context);
      uint32_t timestamp = meta_display_get_current_time_roundtrip (display);

      meta_display_focus_default_window (display, timestamp);
    }
  else if (strcmp (argv[0], "move_cursor_to") == 0)
    {
      float x = (float) atof (argv[1]);
      float y = (float) atof (argv[2]);

      if (argc != 3 && argc != 4)
        BAD_COMMAND ("usage: %s <x> <y> [<interpolation duration (s/ms)>]", argv[0]);

      if (argc == 4)
        {
          char *duration_str = argv[3];
          int duration_ms;

          if (g_str_has_suffix (duration_str, "ms"))
            duration_ms = atoi (duration_str);
          else if (g_str_has_suffix (duration_str, "s"))
            duration_ms = s2ms (atoi (duration_str));
          else
            BAD_COMMAND ("Unknown interpolation time granularity");

          if (!interpolate_pointer_motion (test, x, y, duration_ms, error))
            return FALSE;
        }
      else
        {
          if (!warp_pointer_to (test, x, y, error))
            return FALSE;
        }
    }
  else if (strcmp (argv[0], "click") == 0)
    {
      if (argc != 1)
        BAD_COMMAND ("usage: %s", argv[0]);

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
  else if (strcmp (argv[0], "click_and_hold") == 0)
    {
      if (argc != 1)
        BAD_COMMAND ("usage: %s", argv[0]);

      clutter_virtual_input_device_notify_button (test->pointer,
                                                  CLUTTER_CURRENT_TIME,
                                                  CLUTTER_BUTTON_PRIMARY,
                                                  CLUTTER_BUTTON_STATE_PRESSED);
      meta_flush_input (test->context);
      if (!test_case_dispatch (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "release_click") == 0)
    {
      if (argc != 1)
        BAD_COMMAND ("usage: %s", argv[0]);

      clutter_virtual_input_device_notify_button (test->pointer,
                                                  CLUTTER_CURRENT_TIME,
                                                  CLUTTER_BUTTON_PRIMARY,
                                                  CLUTTER_BUTTON_STATE_RELEASED);
      meta_flush_input (test->context);
      if (!test_case_dispatch (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "key_press") == 0 ||
           strcmp (argv[0], "key_release") == 0)
    {
      ClutterKeyState key_state;
      int key;

      if (argc != 2)
        BAD_COMMAND ("usage: %s <key-code>", argv[0]);

      key_state = strcmp (argv[0], "key_press") == 0 ?
        CLUTTER_KEY_STATE_PRESSED : CLUTTER_KEY_STATE_RELEASED;

      key = libevdev_event_code_from_name (EV_KEY, argv[1]);
      if (key == -1)
        BAD_COMMAND ("Invalid key code %s", argv[1]);

      clutter_virtual_input_device_notify_key (test->keyboard,
                                               CLUTTER_CURRENT_TIME,
                                               key, key_state);

      meta_flush_input (test->context);
      if (!test_case_dispatch (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "set_pref") == 0)
    {
      GSettings *wm;
      GSettings *mutter;

      if (argc != 3)
        BAD_COMMAND ("usage: %s <KEY> <VALUE>", argv[0]);

      wm = g_settings_new ("org.gnome.desktop.wm.preferences");
      g_assert_nonnull (wm);
      mutter = g_settings_new ("org.gnome.mutter");
      g_assert_nonnull (mutter);

      if (strcmp (argv[1], "raise-on-click") == 0)
        {
          gboolean value;
          if (!str_to_bool (argv[2], &value))
            BAD_COMMAND ("usage: %s %s [true|false]", argv[0], argv[1]);

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
            BAD_COMMAND ("usage: %s %s [click|sloppy|mouse]", argv[0], argv[1]);

          g_assert_true (g_settings_set_enum (wm, "focus-mode", mode));
        }
      else if (strcmp (argv[1], "workspaces-only-on-primary") == 0)
        {
          gboolean value;
          if (!str_to_bool (argv[2], &value))
            BAD_COMMAND ("usage: %s %s [true|false]", argv[0], argv[1]);

          g_assert_true (g_settings_set_boolean (mutter, "workspaces-only-on-primary", value));
        }
      else if (strcmp (argv[1], "focus-change-on-pointer-rest") == 0)
        {
          gboolean value;
          if (!str_to_bool (argv[2], &value))
            BAD_COMMAND ("usage: %s %s [true|false]", argv[0], argv[1]);

          g_assert_true (g_settings_set_boolean (mutter, "focus-change-on-pointer-rest", value));
        }
      else if (strcmp (argv[1], "auto-raise") == 0)
        {
          gboolean value;
          if (!str_to_bool (argv[2], &value))
            BAD_COMMAND ("usage: %s %s [true|false]", argv[0], argv[1]);

          g_assert_true (g_settings_set_boolean (wm, "auto-raise", value));
        }
      else if (strcmp (argv[1], "auto-raise-delay") == 0)
        {
          int value = atoi (argv[2]);

          g_assert_true (g_settings_set_int (wm, "auto-raise-delay", value));
        }
      else if (strcmp (argv[1], "center-new-windows") == 0)
        {
          gboolean value;

          if (!str_to_bool (argv[2], &value))
            BAD_COMMAND ("usage: %s %s [true|false]", argv[0], argv[1]);

          g_assert_true (g_settings_set_boolean (mutter, "center-new-windows",
                                                 value));
        }
      else if (strcmp (argv[1], "auto-maximize") == 0)
        {
          gboolean value;

          if (!str_to_bool (argv[2], &value))
            BAD_COMMAND ("usage: %s %s [true|false]", argv[0], argv[1]);

          g_assert_true (g_settings_set_boolean (mutter, "auto-maximize",
                                                 value));
        }
      else if (strcmp (argv[1], "edge-tiling") == 0)
        {
          gboolean value;

          if (!str_to_bool (argv[2], &value))
            BAD_COMMAND ("usage: %s %s [true|false]", argv[0], argv[1]);

          g_assert_true (g_settings_set_boolean (mutter, "edge-tiling",
                                                 value));
        }
      else
        {
          BAD_COMMAND ("Unknown preference %s", argv[1]);
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
        BAD_COMMAND ("usage: %s <client-id>/<window-id>", argv[0]);

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
        BAD_COMMAND ("usage: %s <client-id>/<window-id>", argv[0]);

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

      if (argc != 2)
        BAD_COMMAND ("usage: %s <client-id>/<window-id>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      window = meta_test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      meta_wait_for_effects (window);
    }
  else if (argc > 2 && g_str_equal (argv[1], "=>"))
    {
      g_autoptr (GObject) signal_instance = NULL;
      g_autofree char *signal_name = NULL;
      TestCaseArgs *test_case_args;

      if (!test_case_parse_signal (test, argc, argv,
                                   &signal_name, &signal_instance, error))
        return FALSE;

      g_debug ("Connected to signal '%s' on object %p (%s)",
               signal_name, signal_instance,
               g_type_name_from_instance ((GTypeInstance *) signal_instance));

      test_case_args = g_new0 (TestCaseArgs, 1);
      test_case_args->test_case = test;
      test_case_args->filename = filename;
      test_case_args->line_no = line_no;
      test_case_args->argc = argc - 2;
      test_case_args->argv = g_strdupv (&argv[2]);
      test_case_args->instance = signal_instance;
      test_case_args->handler_id =
        g_signal_connect_swapped (signal_instance,
                                  signal_name,
                                  G_CALLBACK (test_case_signal_cb),
                                  test_case_args);
    }
  else if (strcmp (argv[0], "popup") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      const char *parent_id;

      if (argc != 3)
        BAD_COMMAND ("usage: %s <client-id>/<popup-id> <parent-id>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1],
                                      &client, &window_id, error))
        return FALSE;

      parent_id = argv[2];

      if (!meta_test_client_do (client, error,
                                argv[0], window_id,
                                parent_id,
                                NULL))
        return FALSE;

      if (!track_popup (test, client, window_id, parent_id, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "popup_at") == 0)
    {
      MetaTestClient *client;
      const char *window_id;
      const char *parent_id;
      g_autoptr (GStrvBuilder) args_builder = NULL;
      g_auto (GStrv) args = NULL;
      int i;

      if (argc < 6)
        {
          BAD_COMMAND ("usage: %s <client-id>/<popup-id> <parent-id> "
                       "<top|bottom|left|right|center> "
                       "<width> <height> [<grab>,<resize>,<flip>]", argv[0]);
        }

      if (!test_case_parse_window_id (test, argv[1],
                                      &client, &window_id, error))
        return FALSE;

      parent_id = argv[2];

      args_builder = g_strv_builder_new ();
      g_strv_builder_add_many (args_builder,
                               argv[0],
                               window_id,
                               parent_id,
                               NULL);
      for (i = 3; i < argc; i++)
        g_strv_builder_add (args_builder, argv[i]);

      args = g_strv_builder_end (args_builder);
      if (!meta_test_client_do_strv (client, (const char **) args, error))
        return FALSE;

      if (!track_popup (test, client, window_id, parent_id, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "dismiss") == 0)
    {
      MetaTestClient *client;
      const char *window_id;

      if (argc != 2)
        BAD_COMMAND ("usage: %s <client-id>/<popup-id>", argv[0]);

      if (!test_case_parse_window_id (test, argv[1],
                                      &client, &window_id, error))
        return FALSE;

      if (!meta_test_client_do (client, error,
                                argv[0], window_id,
                                NULL))
        return FALSE;

      g_hash_table_remove (test->popups, argv[1]);
    }
  else
    {
      BAD_COMMAND ("Unknown command %s", argv[0]);
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

  if (!test_case_assert_stacking (test, NULL, 0, STACK_FILTER_ALL, NULL, error))
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
  g_object_unref (test->keyboard);
  g_clear_pointer (&test->popups, g_hash_table_unref);
  g_free (test);

  return TRUE;
}

static void
check_window_has_transient_child (MetaWindow *window,
                                  MetaWindow *transient_child)
{
  GPtrArray *transient_children;

  transient_children = meta_window_get_transient_children (window);
  g_assert_nonnull (transient_children);
  g_assert_true (g_ptr_array_find (transient_children, transient_child, NULL));
}

static void
sanity_check_transient_for (MetaWindow *window,
                            GList      *windows)
{
  if (window->transient_for)
    {
      g_assert_nonnull (g_list_find (windows, window->transient_for));

      check_window_has_transient_child (window->transient_for, window);
    }
}

static void
sanity_check_transient_children (MetaWindow *window,
                                 GList      *windows)
{
  GPtrArray *transient_children;

  transient_children = meta_window_get_transient_children (window);
  if (transient_children &&
      transient_children->len > 0)
    {
      int i;

      for (i = 0; i < transient_children->len; i++)
        {
          MetaWindow *transient_child =
            g_ptr_array_index (transient_children, i);

          g_assert_nonnull (g_list_find (windows, transient_child));
        }
    }
}

static void
sanity_check_monitor (MetaWindow *window)
{
  if (!meta_window_is_hidden (window))
    {
      MtkRectangle rect;

      g_assert_nonnull (window->monitor);
      rect = meta_window_config_get_rect (window->config);
      g_assert_true (mtk_rectangle_overlap (&rect, &window->monitor->rect));
    }
}

static void
sanity_check (MetaContext *context)
{
  MetaDisplay *display = meta_context_get_display (context);
  g_autoptr (GList) windows = NULL;
  GList *l;

  windows = meta_display_list_all_windows (display);
  for (l = windows; l; l = l->next)
    {
      MetaWindow *window = l->data;

      sanity_check_transient_for (window, windows);
      sanity_check_transient_children (window, windows);
      sanity_check_monitor (window);
    }
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
      g_autofree char *line = NULL;
      int argc;
      g_auto (GStrv) argv = NULL;

      line = g_data_input_stream_read_line_utf8 (in, NULL, NULL, &error);
      if (line == NULL)
        break;

      line_no++;

      if (!g_shell_parse_argv (line, &argc, &argv, &error))
        {
          if (g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_EMPTY_STRING))
            {
              g_clear_error (&error);
              goto next;
            }

          /* Prior to glib 2.85.0, empty comment lines "#" emitted this */
          if (g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING) &&
              line[0] == '#')
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
      else
        sanity_check (context);
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

  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

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
