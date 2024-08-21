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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <errno.h>
#include <gio/gio.h>
#include <unistd.h>

#include "meta/util.h"
#include "tests/meta-test/meta-context-test.h"

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

  launcher =  g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);

  meta_add_verbose_topic (META_DEBUG_SCREEN_CAST);

  test_client_path = g_test_build_filename (G_TEST_BUILT,
                                            "mutter-screen-cast-client",
                                            NULL);
  g_subprocess_launcher_setenv (launcher,
                                "XDG_RUNTIME_DIR", getenv ("XDG_RUNTIME_DIR"),
                                TRUE);
  g_subprocess_launcher_setenv (launcher,
                                "G_MESSAGES_DEBUG", "all",
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

  meta_remove_verbose_topic (META_DEBUG_SCREEN_CAST);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/screen-cast/record-virtual",
                   meta_test_screen_cast_record_virtual);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
