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

#include "backends/meta-backend-private.h"
#include "backends/meta-dbus-session-manager.h"
#include "backends/meta-remote-desktop-session.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-test/meta-context-test.h"

static MetaContext *test_context;

static void
meta_test_remote_desktop_emit_after_unbind (void)
{
  g_autoptr (GSubprocess) subprocess = NULL;

  subprocess = meta_launch_test_executable (G_SUBPROCESS_FLAGS_NONE,
                                            "mutter-remote-desktop-tests-client",
                                            "emit-after-unbind",
                                            NULL);
  meta_wait_test_process (subprocess);
}

static void
set_keymap_cb (GObject      *source_object,
               GAsyncResult *result,
               gpointer      user_data)
{
  MetaBackend *backend = META_BACKEND (source_object);
  gboolean *done = user_data;
  g_autoptr (GError) error = NULL;

  g_assert_true (meta_backend_set_keymap_finish (backend, result, &error));
  g_assert_no_error (error);

  *done = TRUE;
}

static gboolean
remote_desktop_test_client_command (int      argc,
                                    GStrv    argv,
                                    gpointer user_data)
{
  if (argc == 1 && strcmp (argv[0], "flush_input") == 0)
    {
      g_debug ("Flushing input");
      meta_flush_input (test_context);
      return TRUE;
    }
  else if (argc == 3 && strcmp (argv[0], "switch_keyboard_layout") == 0)
    {
      MetaBackend *backend = meta_context_get_backend (test_context);
      const char *layout = argv[1];
      const char *variant = argv[2];
      g_autoptr (GMainContext) main_context = NULL;
      gboolean done = FALSE;

      g_debug ("Switching keyboard layout to %s, %s", layout, variant);
      main_context = g_main_context_new ();
      g_main_context_push_thread_default (main_context);
      meta_backend_set_keymap_async (backend, layout, variant, "", "",
                                     NULL, set_keymap_cb, &done);
      while (!done)
        g_main_context_iteration (main_context, TRUE);
      g_main_context_pop_thread_default (main_context);

      return TRUE;
    }
  else if (argc == 2 && strcmp (argv[0], "update_viewports") == 0)
    {
      MetaBackend *backend = meta_context_get_backend (test_context);
      MetaRemoteDesktop *remote_desktop =
        meta_backend_get_remote_desktop (backend);
      MetaDbusSessionManager *session_manager =
        META_DBUS_SESSION_MANAGER (remote_desktop);
      MetaDbusSession *dbus_session;
      MetaRemoteDesktopSession *session;
      MetaEis *eis;

      dbus_session =
        meta_dbus_session_manager_get_session (session_manager,
                                               argv[1]);
      session =
        META_REMOTE_DESKTOP_SESSION (dbus_session);
      eis = meta_remote_desktop_session_get_eis (session);

      g_signal_emit_by_name (eis, "viewports-changed");

      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static void
meta_test_remote_desktop_keyboard_layout (void)
{
  g_autoptr (GSubprocess) subprocess = NULL;

  subprocess = meta_launch_test_executable (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                            G_SUBPROCESS_FLAGS_STDIN_PIPE,
                                            "mutter-remote-desktop-tests-client",
                                            "keyboard-layout",
                                            NULL);
  meta_test_process_watch_commands (subprocess,
                                    remote_desktop_test_client_command,
                                    NULL);
  meta_wait_test_process (subprocess);
}

static void
meta_test_remote_desktop_change_viewport (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaRemoteDesktop *remote_desktop = meta_backend_get_remote_desktop (backend);
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (remote_desktop);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (GSubprocess) subprocess = NULL;
  graphene_point_t pos;

  subprocess = meta_launch_test_executable (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                            G_SUBPROCESS_FLAGS_STDIN_PIPE,
                                            "mutter-remote-desktop-tests-client",
                                            "change-viewport",
                                            NULL);
  meta_test_process_watch_commands (subprocess,
                                    remote_desktop_test_client_command,
                                    NULL);
  meta_wait_test_process (subprocess);

  while (meta_dbus_session_manager_get_num_sessions (session_manager) > 0)
    g_main_context_iteration (NULL, TRUE);

  meta_flush_input (test_context);

  clutter_seat_query_state (seat, NULL, &pos, NULL);

  g_assert_cmpfloat_with_epsilon (pos.x, 1.0f, DBL_EPSILON);
  g_assert_cmpfloat_with_epsilon (pos.y, 1.0f, DBL_EPSILON);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/remote-desktop/emit-after-unbind",
                   meta_test_remote_desktop_emit_after_unbind);
  g_test_add_func ("/backends/native/remote-desktop/keyboard-layout",
                   meta_test_remote_desktop_keyboard_layout);
  g_test_add_func ("/backends/native/remote-desktop/change-viewport",
                   meta_test_remote_desktop_change_viewport);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
