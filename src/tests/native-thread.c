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

#include <glib.h>
#include <glib-unix.h>

#include "backends/native/meta-thread.h"
#include "backends/native/meta-thread-impl.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-thread-impl-test.h"
#include "tests/meta-thread-test.h"

static MetaContext *test_context;
static MetaThread *test_thread;

static gpointer
impl_func (MetaThreadImpl  *thread_impl,
           gpointer         user_data,
           GError         **error)
{
  gboolean *done = user_data;

  meta_assert_in_thread_impl (meta_thread_impl_get_thread (thread_impl));

  *done = TRUE;
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Not a real error");

  return GINT_TO_POINTER (42);
}

static void
callback_func (MetaThread *thread,
               gpointer    user_data)
{
  int *state = user_data;

  meta_assert_not_in_thread_impl (thread);

  g_assert_cmpint (*state, ==, 1);
  *state = 2;
}

static void
user_data_destroy (gpointer user_data)
{
  int *state = user_data;

  meta_assert_not_in_thread_impl (test_thread);

  g_assert_cmpint (*state, ==, 2);
  *state = 3;
}

static gpointer
queue_callback_func (MetaThreadImpl  *thread_impl,
                     gpointer         user_data,
                     GError         **error)
{
  int *state = user_data;

  meta_assert_in_thread_impl (meta_thread_impl_get_thread (thread_impl));

  g_assert_cmpint (*state, ==, 0);
  *state = 1;

  meta_thread_queue_callback (meta_thread_impl_get_thread (thread_impl),
                              callback_func,
                              user_data,
                              user_data_destroy);
  return GINT_TO_POINTER (TRUE);
}

typedef struct
{
  int fd;
  GMainLoop *loop;
  int read_value;

  GSource *source;
} PipeData;

static gpointer
dispatch_pipe (MetaThreadImpl  *thread_impl,
               gpointer         user_data,
               GError         **error)
{
  PipeData *pipe_data = user_data;

  meta_assert_in_thread_impl (meta_thread_impl_get_thread (thread_impl));

  g_assert_cmpint (read (pipe_data->fd, &pipe_data->read_value,
                         sizeof (pipe_data->read_value)),
                   ==,
                   sizeof (pipe_data->read_value));
  g_main_loop_quit (pipe_data->loop);
  g_source_destroy (pipe_data->source);
  g_source_unref (pipe_data->source);

  return GINT_TO_POINTER (TRUE);
}

static gpointer
register_fd_func (MetaThreadImpl  *thread_impl,
                  gpointer         user_data,
                  GError         **error)
{
  PipeData *pipe_data = user_data;

  pipe_data->source = meta_thread_impl_register_fd (thread_impl,
                                                    pipe_data->fd,
                                                    dispatch_pipe,
                                                    pipe_data);

  return GINT_TO_POINTER (TRUE);
}

typedef struct
{
  GMainLoop *loop;

  int state;

  GSource *source;
} IdleData;

static gboolean
idle_cb (gpointer user_data)
{
  IdleData *idle_data = user_data;

  meta_assert_in_thread_impl (test_thread);

  if (idle_data->state == 1)
    {
      idle_data->state = 2;
      return G_SOURCE_REMOVE;
    }

  g_assert_cmpint (idle_data->state, ==, 0);
  idle_data->state = 1;
  return G_SOURCE_CONTINUE;
}

static void
idle_data_destroy (gpointer user_data)
{
  IdleData *idle_data = user_data;

  /* XXX: This can only be checked on kernel type threads. */
  /* meta_assert_in_thread_impl (test_thread); */

  g_assert_cmpint (idle_data->state, ==, 2);
  idle_data->state = 3;

  g_main_loop_quit (idle_data->loop);
}

static gpointer
add_idle_func (MetaThreadImpl  *thread_impl,
               gpointer         user_data,
               GError         **error)
{
  IdleData *idle_data = user_data;
  GSource *source;

  meta_assert_in_thread_impl (meta_thread_impl_get_thread (thread_impl));

  source = meta_thread_impl_add_source (thread_impl,
                                        idle_cb,
                                        idle_data,
                                        idle_data_destroy);
  g_source_unref (source);

  return GINT_TO_POINTER (TRUE);
}

static void
run_thread_tests (MetaThread *thread)
{
  gboolean done = FALSE;
  GError *error = NULL;
  gpointer retval;
  int state;
  int fds[2];
  int buf;
  PipeData pipe_data;
  IdleData idle_data;

  meta_assert_not_in_thread_impl (thread);

  /* Test that sync tasks run correctly. */
  g_debug ("Test synchronous tasks");
  retval = meta_thread_run_impl_task_sync (thread, impl_func, &done, &error);
  g_assert_true (done);
  g_assert_nonnull (error);
  g_assert_cmpint (GPOINTER_TO_INT (retval), ==, 42);
  g_clear_error (&error);

  /* Test that callbacks run correctly. */
  g_debug ("Test callbacks");
  state = 0;
  meta_thread_run_impl_task_sync (thread, queue_callback_func, &state, NULL);
  g_assert_cmpint (state, ==, 1);
  while (g_main_context_iteration (NULL, FALSE));
  g_assert_cmpint (state, ==, 3);

  /* Test callback flushing */
  g_debug ("Test callbacks flushing");
  state = 0;
  meta_thread_run_impl_task_sync (thread, queue_callback_func, &state, NULL);
  g_assert_cmpint (state, ==, 1);
  meta_thread_flush_callbacks (thread);
  g_assert_cmpint (state, ==, 3);

  /* Test fd source */
  g_debug ("Test fd source");
  pipe_data = (PipeData) { 0 };
  g_assert_true (g_unix_open_pipe (fds, FD_CLOEXEC, NULL));
  pipe_data.fd = fds[0];
  pipe_data.loop = g_main_loop_new (NULL, FALSE);
  meta_thread_run_impl_task_sync (thread, register_fd_func, &pipe_data, NULL);
  buf = 100;
  g_assert_cmpint (write (fds[1], &buf, sizeof (buf)), ==, sizeof (buf));
  g_main_loop_run (pipe_data.loop);
  g_assert_cmpint (pipe_data.read_value, ==, 100);
  g_main_loop_unref (pipe_data.loop);
  close (fds[0]);
  close (fds[1]);

  /* Test idle source */
  g_debug ("Test idle source");
  idle_data = (IdleData) { 0 };
  idle_data.loop = g_main_loop_new (NULL, FALSE);
  meta_thread_run_impl_task_sync (thread, add_idle_func, &idle_data, NULL);
  g_main_loop_run (idle_data.loop);
  g_assert_cmpint (idle_data.state, ==, 3);
  g_main_loop_unref (idle_data.loop);
}

static void
meta_test_thread_user_common (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaThread *thread;
  g_autoptr (GError) error = NULL;

  thread = g_initable_new (META_TYPE_THREAD_TEST,
                           NULL, &error,
                           "backend", backend,
                           NULL);
  g_object_add_weak_pointer (G_OBJECT (thread), (gpointer *) &thread);
  g_assert_nonnull (thread);
  g_assert_null (error);
  g_assert (meta_thread_get_backend (thread) == backend);
  test_thread = thread;

  run_thread_tests (thread);

  g_object_unref (thread);
  g_assert_null (thread);
  test_thread = NULL;
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/thread/user/common",
                   meta_test_thread_user_common);
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

  test_context = context;
  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
