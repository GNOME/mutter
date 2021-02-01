/*
 * Copyright (C) 2021 Red Hat Inc.
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

#include "tests/native-screen-cast.h"

#include <errno.h>
#include <gio/gio.h>
#include <unistd.h>

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

static void
meta_test_screen_cast_record_virtual (void)
{
  GSubprocessLauncher *launcher;
  g_autofree char *test_client_path = NULL;
  GError *error = NULL;
  GSubprocess *subprocess;
  GMainLoop *loop;

  launcher =  g_subprocess_launcher_new ((G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                          G_SUBPROCESS_FLAGS_STDOUT_PIPE));

  test_client_path = g_test_build_filename (G_TEST_BUILT,
                                            "src",
                                            "tests",
                                            "mutter-screen-cast-client",
                                            NULL);
  g_subprocess_launcher_setenv (launcher,
                                "XDG_RUNTIME_DIR", getenv ("XDG_RUNTIME_DIR"),
                                TRUE);
  subprocess = g_subprocess_launcher_spawn (launcher,
                                            &error,
                                            test_client_path,
                                            NULL);
  if (!subprocess)
    g_error ("Failed to launch screen cast test client: %s", error->message);

  loop = g_main_loop_new (NULL, FALSE);
  g_subprocess_wait_check_async (subprocess,
                                 NULL,
                                 test_client_exited,
                                 loop);
  g_main_loop_run (loop);
  g_assert_true (g_subprocess_get_successful (subprocess));
  g_object_unref (subprocess);
}

void
init_screen_cast_tests (void)
{
  g_test_add_func ("/backends/native/screen-cast/record-virtual",
                   meta_test_screen_cast_record_virtual);
}
