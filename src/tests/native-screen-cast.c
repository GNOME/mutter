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

#include "meta/meta-backend.h"
#include "meta/util.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-test/meta-context-test.h"

static MetaContext *test_context;

static void read_line_async (GDataInputStream *client_stdout,
                             GCancellable     *cancellable);

static void
process_line (const char *line)
{
  g_autoptr (GError) error = NULL;
  int argc;
  g_auto (GStrv) argv = NULL;

  if (!g_shell_parse_argv (line, &argc, &argv, &error))
    g_assert_no_error (error);

  if (argc == 1 && g_strcmp0 (argv[0], "post_damage") == 0)
    {
      MetaBackend *backend = meta_context_get_backend (test_context);
      ClutterActor *stage = meta_backend_get_stage (backend);

      g_debug ("Posting damage");
      clutter_actor_queue_redraw (stage);
    }
  else
    {
      g_error ("Unknown command '%s'", line);
    }
}

static void
line_read_cb (GObject      *source_object,
              GAsyncResult *res,
              gpointer      user_data)
{
  GDataInputStream *client_stdout = G_DATA_INPUT_STREAM (source_object);
  GCancellable *cancellable = G_CANCELLABLE (user_data);
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
    process_line (line);

  read_line_async (client_stdout, cancellable);
}

static void
read_line_async (GDataInputStream *client_stdout,
                 GCancellable     *cancellable)
{
  g_data_input_stream_read_line_async (client_stdout,
                                       G_PRIORITY_DEFAULT,
                                       cancellable,
                                       line_read_cb,
                                       cancellable);
}

static void
run_screen_cast_test_client (const char *client_name)
{
  g_autoptr (GSubprocess) subprocess = NULL;
  g_autoptr (GDataInputStream) client_stdout = NULL;
  g_autoptr (GCancellable) cancellable = NULL;

  meta_add_verbose_topic (META_DEBUG_SCREEN_CAST);
  subprocess = meta_launch_test_executable (G_SUBPROCESS_FLAGS_STDOUT_PIPE,
                                            client_name,
                                            NULL);

  client_stdout =
    g_data_input_stream_new (g_subprocess_get_stdout_pipe (subprocess));
  cancellable = g_cancellable_new ();

  read_line_async (client_stdout, cancellable);

  meta_wait_test_process (subprocess);
  g_cancellable_cancel (cancellable);

  meta_remove_verbose_topic (META_DEBUG_SCREEN_CAST);
}

static void
meta_test_screen_cast_record_virtual (void)
{
  run_screen_cast_test_client ("mutter-screen-cast-client");
}

static void
meta_test_screen_cast_record_virtual_driver (void)
{
  run_screen_cast_test_client ("mutter-screen-cast-client-driver");
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/screen-cast/record-virtual",
                   meta_test_screen_cast_record_virtual);
  g_test_add_func ("/backends/native/screen-cast/record-virtual-driver",
                   meta_test_screen_cast_record_virtual_driver);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  meta_add_verbose_topic (META_DEBUG_SCREEN_CAST);

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
