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

#include <gio/gio.h>

#include "meta-test/meta-context-test.h"
#include "tests/meta-test-utils.h"

typedef struct _InputCaptureTestClient
{
  GSubprocess *subprocess;
  char *path;
  GMainLoop *main_loop;
  GDataInputStream *line_reader;
} InputCaptureTestClient;

static MetaContext *test_context;

static InputCaptureTestClient *
input_capture_test_client_new (const char *test_case)
{
  g_autofree char *test_client_path = NULL;
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  GSubprocess *subprocess;
  GError *error = NULL;
  InputCaptureTestClient *test_client;
  GInputStream *stdout_stream;
  GDataInputStream *line_reader;

  test_client_path = g_test_build_filename (G_TEST_BUILT,
                                            "src",
                                            "tests",
                                            "mutter-input-capture-test-client",
                                            NULL);
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE);
  subprocess = g_subprocess_launcher_spawn (launcher,
                                            &error,
                                            test_client_path,
                                            test_case,
                                            NULL);
  if (!subprocess)
    g_error ("Failed to launch input capture test client: %s", error->message);

  stdout_stream = g_subprocess_get_stdout_pipe (subprocess);
  line_reader = g_data_input_stream_new (stdout_stream);

  test_client = g_new0 (InputCaptureTestClient, 1);
  test_client->subprocess = subprocess;
  test_client->main_loop = g_main_loop_new (NULL, FALSE);
  test_client->line_reader = line_reader;

  return test_client;
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
input_capture_test_client_wait_for_state (InputCaptureTestClient *test_client,
                                          const char             *expected_state)
{
  WaitData data;

  data.loop = g_main_loop_new (NULL, FALSE);
  data.expected_state = expected_state;

  g_data_input_stream_read_line_async (test_client->line_reader,
                                       G_PRIORITY_DEFAULT,
                                       NULL,
                                       on_line_read,
                                       &data);

  g_main_loop_run (data.loop);
  g_main_loop_unref (data.loop);
}

static void
input_capture_test_client_finished (GObject      *source_object,
                                    GAsyncResult *res,
                                    gpointer      user_data)
{
  InputCaptureTestClient *test_client = user_data;
  GError *error = NULL;

  if (!g_subprocess_wait_finish (test_client->subprocess,
                                 res,
                                 &error))
    {
      g_error ("Failed to wait for input capture test client: %s",
               error->message);
    }

  g_main_loop_quit (test_client->main_loop);
}

static void
input_capture_test_client_finish (InputCaptureTestClient *test_client)
{
  g_subprocess_wait_async (test_client->subprocess, NULL,
                           input_capture_test_client_finished, test_client);

  g_main_loop_run (test_client->main_loop);

  g_assert_true (g_subprocess_get_successful (test_client->subprocess));

  g_main_loop_unref (test_client->main_loop);
  g_object_unref (test_client->line_reader);
  g_object_unref (test_client->subprocess);
  g_free (test_client);
}

static void
meta_test_input_capture_sanity (void)
{
  InputCaptureTestClient *test_client;

  test_client = input_capture_test_client_new ("sanity");
  input_capture_test_client_finish (test_client);
}

static void
meta_test_input_capture_zones (void)
{
  g_autoptr (MetaVirtualMonitor) virtual_monitor1 = NULL;
  g_autoptr (MetaVirtualMonitor) virtual_monitor2 = NULL;
  InputCaptureTestClient *test_client;

  virtual_monitor1 = meta_create_test_monitor (test_context, 800, 600, 20.0);
  virtual_monitor2 = meta_create_test_monitor (test_context, 1024, 768, 20.0);

  test_client = input_capture_test_client_new ("zones");

  input_capture_test_client_wait_for_state (test_client, "1");

  g_clear_object (&virtual_monitor1);

  input_capture_test_client_finish (test_client);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/input-capture/sanity",
                   meta_test_input_capture_sanity);
  g_test_add_func ("/backends/native/input-capture/zones",
                   meta_test_input_capture_zones);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = test_context =
    meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                              META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
