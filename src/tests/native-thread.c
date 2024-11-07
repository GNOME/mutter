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

#include <glib.h>
#include <glib-unix.h>

#include "backends/native/meta-thread-impl.h"
#include "backends/native/meta-thread-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-thread-impl-test.h"
#include "tests/meta-thread-test.h"

static MetaContext *test_context;
static MetaThread *test_thread;

static gboolean
quit_main_loop (gpointer user_data)
{
  GMainLoop *loop = user_data;

  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

static void
quit_main_loop_in_idle (GMainLoop *loop)
{
  GSource *idle_source;

  idle_source = g_idle_source_new ();
  g_source_set_callback (idle_source, quit_main_loop, loop, NULL);
  g_source_attach (idle_source, g_main_loop_get_context (loop));
  g_source_unref (idle_source);
}

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
                              NULL,
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
  quit_main_loop_in_idle (pipe_data->loop);
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
  MetaThread *thread;
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

  if (meta_thread_get_thread_type (idle_data->thread) ==
      META_THREAD_TYPE_KERNEL)
    meta_assert_in_thread_impl (test_thread);

  g_assert_cmpint (idle_data->state, ==, 2);
  idle_data->state = 3;

  quit_main_loop_in_idle (idle_data->loop);
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

typedef struct
{
  MetaThread *thread;
  GMainLoop *loop;

  GMutex mutex;
  int state;
} AsyncData;

static gpointer
async_func (MetaThreadImpl  *thread_impl,
            gpointer         user_data,
            GError         **error)
{
  AsyncData *async_data = user_data;

  meta_assert_in_thread_impl (async_data->thread);

  g_mutex_lock (&async_data->mutex);
  g_assert_cmpint (async_data->state, ==, 0);
  async_data->state = 1;
  g_mutex_unlock (&async_data->mutex);

  return GINT_TO_POINTER (TRUE);
}

static void
async_destroy (gpointer user_data)
{
  AsyncData *async_data = user_data;

  g_mutex_lock (&async_data->mutex);
  g_assert_cmpint (async_data->state, ==, 2);
  async_data->state = 3;
  g_main_loop_quit (async_data->loop);
  g_mutex_unlock (&async_data->mutex);
}

static void
async_feedback_func (gpointer      retval,
                     const GError *error,
                     gpointer      user_data)
{
  AsyncData *async_data = user_data;

  meta_assert_not_in_thread_impl (async_data->thread);

  g_mutex_lock (&async_data->mutex);
  g_assert_cmpint (async_data->state, ==, 1);
  async_data->state = 2;
  g_mutex_unlock (&async_data->mutex);
}

static gpointer
multiple_async_func1 (MetaThreadImpl  *thread_impl,
                      gpointer         user_data,
                      GError         **error)
{
  AsyncData *async_data = user_data;

  meta_assert_in_thread_impl (async_data->thread);

  g_mutex_lock (&async_data->mutex);
  g_assert_cmpint (async_data->state, ==, 0);
  async_data->state = 1;
  g_mutex_unlock (&async_data->mutex);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Sample error");
  return GINT_TO_POINTER (1);
}

static void
multiple_async_feedback_func1 (gpointer      retval,
                               const GError *error,
                               gpointer      user_data)
{
  AsyncData *async_data = user_data;

  meta_assert_not_in_thread_impl (async_data->thread);

  g_assert_true (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED));
  g_assert_cmpint (GPOINTER_TO_INT (retval), ==, 1);
}

static gpointer
multiple_async_func2 (MetaThreadImpl  *thread_impl,
                      gpointer         user_data,
                      GError         **error)
{
  AsyncData *async_data = user_data;

  meta_assert_in_thread_impl (async_data->thread);

  g_mutex_lock (&async_data->mutex);
  g_assert_cmpint (async_data->state, ==, 1);
  async_data->state = 2;
  g_mutex_unlock (&async_data->mutex);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Sample error");
  return GINT_TO_POINTER (2);
}

static void
multiple_async_feedback_func2 (gpointer      retval,
                               const GError *error,
                               gpointer      user_data)
{
  AsyncData *async_data = user_data;

  meta_assert_not_in_thread_impl (async_data->thread);

  g_assert_true (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED));
  g_assert_cmpint (GPOINTER_TO_INT (retval), ==, 2);
}

static gpointer
multiple_async_func3 (MetaThreadImpl  *thread_impl,
                      gpointer         user_data,
                      GError         **error)
{
  AsyncData *async_data = user_data;

  meta_assert_in_thread_impl (async_data->thread);

  g_mutex_lock (&async_data->mutex);
  g_assert_cmpint (async_data->state, ==, 2);
  async_data->state = 3;
  g_mutex_unlock (&async_data->mutex);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "Sample error");
  return GINT_TO_POINTER (3);
}

static void
multiple_async_feedback_func3 (gpointer      retval,
                               const GError *error,
                               gpointer      user_data)
{
  AsyncData *async_data = user_data;

  meta_assert_not_in_thread_impl (async_data->thread);

  g_assert_true (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED));
  g_assert_cmpint (GPOINTER_TO_INT (retval), ==, 3);

  g_main_loop_quit (async_data->loop);
}

typedef struct
{
  MetaThread *thread;

  GMutex mutex;
  int state;
} MixedData;

static gpointer
mixed_async_func (MetaThreadImpl  *thread_impl,
                  gpointer         user_data,
                  GError         **error)
{
  MixedData *mixed_data = user_data;

  meta_assert_in_thread_impl (mixed_data->thread);

  g_mutex_lock (&mixed_data->mutex);
  g_assert_cmpint (mixed_data->state, ==, 0);
  mixed_data->state = 1;
  g_mutex_unlock (&mixed_data->mutex);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Sample error");
  return GINT_TO_POINTER (1);
}

static void
mixed_async_feedback_func (gpointer      retval,
                           const GError *error,
                           gpointer      user_data)
{
  MixedData *mixed_data = user_data;

  meta_assert_not_in_thread_impl (mixed_data->thread);

  g_assert_true (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED));
  g_assert_cmpint (GPOINTER_TO_INT (retval), ==, 1);

  g_mutex_lock (&mixed_data->mutex);
  g_assert_cmpint (mixed_data->state, ==, 2);
  mixed_data->state = 3;
  g_mutex_unlock (&mixed_data->mutex);
}

static gpointer
mixed_sync_func (MetaThreadImpl  *thread_impl,
                 gpointer         user_data,
                 GError         **error)
{
  MixedData *mixed_data = user_data;

  meta_assert_in_thread_impl (mixed_data->thread);

  g_mutex_lock (&mixed_data->mutex);
  g_assert_cmpint (mixed_data->state, ==, 1);
  mixed_data->state = 2;
  g_mutex_unlock (&mixed_data->mutex);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK, "Sample error");
  return GINT_TO_POINTER (2);
}

typedef struct
{
  MetaThread *thread;
  gboolean registered;

  GThread *gthread;
  GMutex init_mutex;
  GCond init_cond;

  GMainContext *main_context;
  GMainLoop *main_loop;
  int sleep_s;

  int state;
} FlushData;

typedef struct
{
  GMainLoop *loop;
  int use_count;
} LoopUser;

static gpointer
blocking_flush_thread_func (gpointer user_data)
{
  FlushData *flush_data = user_data;

  meta_thread_register_callback_context (flush_data->thread,
                                         flush_data->main_context);
  g_mutex_lock (&flush_data->init_mutex);
  flush_data->registered = TRUE;
  g_cond_signal (&flush_data->init_cond);
  g_mutex_unlock (&flush_data->init_mutex);

  flush_data->main_loop = g_main_loop_new (flush_data->main_context, FALSE);
  g_main_loop_run (flush_data->main_loop);
  g_clear_pointer (&flush_data->main_loop, g_main_loop_unref);
  meta_thread_unregister_callback_context (flush_data->thread,
                                           flush_data->main_context);

  return GINT_TO_POINTER (TRUE);
}

static void
slow_callback (MetaThread *thread,
               gpointer    user_data)
{
  FlushData *flush_data = user_data;

  g_assert_cmpint (flush_data->state, ==, 1);
  flush_data->state = 2;

  sleep (flush_data->sleep_s);

  g_assert_cmpint (flush_data->state, ==, 2);
  flush_data->state = 3;

  g_main_loop_quit (flush_data->main_loop);
}

static gpointer
queue_slow_callback (MetaThreadImpl  *thread_impl,
                     gpointer         user_data,
                     GError         **error)
{
  FlushData *flush_data = user_data;

  g_mutex_lock (&flush_data->init_mutex);
  g_mutex_unlock (&flush_data->init_mutex);

  g_assert_cmpint (flush_data->state, ==, 0);
  flush_data->state = 1;

  meta_thread_queue_callback (meta_thread_impl_get_thread (thread_impl),
                              flush_data->main_context,
                              slow_callback,
                              flush_data,
                              NULL);
  return GINT_TO_POINTER (TRUE);
}

static void
quit_main_loop_feedback_func (gpointer      retval,
                              const GError *error,
                              gpointer      user_data)
{
  LoopUser *loop_user = user_data;

  g_assert_cmpint (loop_user->use_count, >, 0);

  loop_user->use_count--;
  if (loop_user->use_count == 0)
    g_main_loop_quit (loop_user->loop);
}

typedef struct
{
  GThread *gthread;
  GMutex init_mutex;
  MetaThread *thread;

  GMainLoop *main_thread_loop;

  GMainContext *thread_main_context;
  GMainLoop *thread_loop;

  int state;
} CallbackData;

static void
non_default_thread_callback_func (MetaThread *thread,
                                  gpointer    user_data)
{
  CallbackData *callback_data = user_data;

  g_assert_true (g_thread_self () == callback_data->gthread);

  g_assert_cmpint (callback_data->state, ==, 3);
  callback_data->state = 4;
}

static void
callback_destroy_cb (gpointer user_data)
{
  CallbackData *callback_data = user_data;

  g_assert_true (g_thread_self () == callback_data->gthread);

  g_assert_cmpint (callback_data->state, ==, 4);
  callback_data->state = 5;
}

static gpointer
queue_non_default_callback_func (MetaThreadImpl  *thread_impl,
                                 gpointer         user_data,
                                 GError         **error)
{
  CallbackData *callback_data = user_data;

  meta_assert_in_thread_impl (meta_thread_impl_get_thread (thread_impl));

  g_assert_cmpint (callback_data->state, ==, 2);
  callback_data->state = 3;

  meta_thread_queue_callback (meta_thread_impl_get_thread (thread_impl),
                              callback_data->thread_main_context,
                              non_default_thread_callback_func,
                              callback_data,
                              callback_destroy_cb);

  return GINT_TO_POINTER (42);
}

static void
non_default_thread_feedback_func (gpointer      retval,
                                  const GError *error,
                                  gpointer      user_data)
{
  CallbackData *callback_data = user_data;

  g_assert_true (g_thread_self () == callback_data->gthread);

  g_assert_cmpint (callback_data->state, ==, 5);
  callback_data->state = 6;

  g_assert_cmpint (GPOINTER_TO_INT (retval), ==, 42);
  g_assert_null (error);

  g_main_loop_quit (callback_data->thread_loop);
}

static gpointer
non_default_callback_thread_func (gpointer user_data)
{
  CallbackData *callback_data = user_data;

  g_mutex_lock (&callback_data->init_mutex);
  g_mutex_unlock (&callback_data->init_mutex);

  g_assert_cmpint (callback_data->state, ==, 1);
  callback_data->state = 2;

  callback_data->thread_main_context = g_main_context_new ();
  g_main_context_push_thread_default (callback_data->thread_main_context);
  callback_data->thread_loop =
    g_main_loop_new (callback_data->thread_main_context, FALSE);
  meta_thread_register_callback_context (callback_data->thread,
                                         callback_data->thread_main_context);

  meta_thread_post_impl_task (callback_data->thread,
                              queue_non_default_callback_func,
                              callback_data, NULL,
                              non_default_thread_feedback_func,
                              callback_data);

  g_main_loop_run (callback_data->thread_loop);
  g_main_loop_unref (callback_data->thread_loop);

  g_assert_cmpint (callback_data->state, ==, 6);
  callback_data->state = 7;

  g_main_loop_quit (callback_data->main_thread_loop);
  meta_thread_unregister_callback_context (callback_data->thread,
                                           callback_data->thread_main_context);
  g_main_context_pop_thread_default (callback_data->thread_main_context);
  g_main_context_unref (callback_data->thread_main_context);

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
  AsyncData async_data;
  MixedData mixed_data;
  FlushData flush_data1;
  FlushData flush_data2;
  LoopUser loop_user;
  CallbackData callback_data;

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
  idle_data.thread = thread;
  idle_data.loop = g_main_loop_new (NULL, FALSE);
  meta_thread_run_impl_task_sync (thread, add_idle_func, &idle_data, NULL);
  g_main_loop_run (idle_data.loop);
  g_assert_cmpint (idle_data.state, ==, 3);
  g_main_loop_unref (idle_data.loop);

  /* Test async tasks */
  g_debug ("Test async task");
  async_data = (AsyncData) { 0 };
  g_mutex_init (&async_data.mutex);
  async_data.thread = thread;
  async_data.loop = g_main_loop_new (NULL, FALSE);
  g_mutex_lock (&async_data.mutex);
  meta_thread_post_impl_task (thread, async_func, &async_data, async_destroy,
                              async_feedback_func, &async_data);
  g_assert_cmpint (async_data.state, ==, 0);
  g_mutex_unlock (&async_data.mutex);
  g_main_loop_run (async_data.loop);
  g_mutex_lock (&async_data.mutex);
  g_assert_cmpint (async_data.state, ==, 3);
  g_mutex_unlock (&async_data.mutex);
  g_main_loop_unref (async_data.loop);
  g_mutex_clear (&async_data.mutex);

  /* Multiple async tasks */
  g_debug ("Test multiple async tasks");
  async_data = (AsyncData) { 0 };
  g_mutex_init (&async_data.mutex);
  async_data.thread = thread;
  async_data.loop = g_main_loop_new (NULL, FALSE);
  g_mutex_lock (&async_data.mutex);
  meta_thread_post_impl_task (thread, multiple_async_func1, &async_data, NULL,
                              multiple_async_feedback_func1, &async_data);
  meta_thread_post_impl_task (thread, multiple_async_func2, &async_data, NULL,
                              multiple_async_feedback_func2, &async_data);
  meta_thread_post_impl_task (thread, multiple_async_func3, &async_data, NULL,
                              multiple_async_feedback_func3, &async_data);
  g_assert_cmpint (async_data.state, ==, 0);
  g_mutex_unlock (&async_data.mutex);
  g_main_loop_run (async_data.loop);
  g_mutex_lock (&async_data.mutex);
  g_assert_cmpint (async_data.state, ==, 3);
  g_mutex_unlock (&async_data.mutex);
  g_main_loop_unref (async_data.loop);
  g_mutex_clear (&async_data.mutex);

  /* Async task followed by sync task */
  g_debug ("Test mixed async and sync tasks");
  mixed_data = (MixedData) { 0 };
  g_mutex_init (&mixed_data.mutex);
  mixed_data.thread = thread;
  g_mutex_lock (&mixed_data.mutex);
  meta_thread_post_impl_task (thread, mixed_async_func, &mixed_data, NULL,
                              mixed_async_feedback_func, &mixed_data);
  g_assert_cmpint (mixed_data.state, ==, 0);
  g_mutex_unlock (&mixed_data.mutex);
  retval = meta_thread_run_impl_task_sync (thread, mixed_sync_func, &mixed_data,
                                           &error);
  g_mutex_lock (&mixed_data.mutex);
  g_assert_cmpint (mixed_data.state, ==, 2);
  g_assert_cmpint (GPOINTER_TO_INT (retval), ==, 2);
  g_assert_true (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK));
  g_clear_error (&error);
  g_mutex_unlock (&mixed_data.mutex);
  meta_thread_flush_callbacks (thread);
  g_mutex_lock (&mixed_data.mutex);
  g_assert_cmpint (mixed_data.state, ==, 3);
  g_mutex_unlock (&mixed_data.mutex);
  g_mutex_clear (&mixed_data.mutex);

  /* Blocking flush. */
  g_debug ("Test blocking flush");
  loop_user = (LoopUser) {
    .loop = g_main_loop_new (NULL, FALSE),
    .use_count = 2,
  };
  flush_data1 = (FlushData) {
    .thread = thread,
    .sleep_s = 3,
  };
  g_mutex_init (&flush_data1.init_mutex);
  g_cond_init (&flush_data1.init_cond);
  g_mutex_lock (&flush_data1.init_mutex);
  flush_data1.main_context = g_main_context_new ();
  flush_data1.gthread = g_thread_new ("blocking-flush-thread #1",
                                      blocking_flush_thread_func,
                                      &flush_data1);
  while (!flush_data1.registered)
    g_cond_wait (&flush_data1.init_cond, &flush_data1.init_mutex);
  g_mutex_unlock (&flush_data1.init_mutex);
  meta_thread_post_impl_task (thread,
                              queue_slow_callback,
                              &flush_data1, NULL,
                              quit_main_loop_feedback_func,
                              &loop_user);
  flush_data2 = (FlushData) {
    .thread = thread,
    .sleep_s = 2,
  };
  g_mutex_init (&flush_data2.init_mutex);
  g_mutex_lock (&flush_data2.init_mutex);
  flush_data2.main_context = g_main_context_new ();
  flush_data2.gthread = g_thread_new ("blocking-flush-thread #2",
                                      blocking_flush_thread_func,
                                      &flush_data2);
  while (!flush_data2.registered)
    g_cond_wait (&flush_data2.init_cond, &flush_data2.init_mutex);
  g_mutex_unlock (&flush_data2.init_mutex);
  meta_thread_post_impl_task (thread,
                              queue_slow_callback,
                              &flush_data2, NULL,
                              quit_main_loop_feedback_func,
                              &loop_user);

  g_main_loop_run (loop_user.loop);
  g_clear_pointer (&loop_user.loop, g_main_loop_unref);

  meta_thread_flush_callbacks (thread);

  g_assert_cmpint (flush_data1.state, ==, 3);
  g_assert_cmpint (flush_data2.state, ==, 3);

  g_thread_join (flush_data1.gthread);
  g_main_context_unref (flush_data1.main_context);
  g_thread_join (flush_data2.gthread);
  g_main_context_unref (flush_data2.main_context);

  /* Callbacks to non-default thread. */
  g_debug ("Test callbacks to non-default thread");
  callback_data = (CallbackData) {};
  callback_data.thread = thread;
  callback_data.main_thread_loop = g_main_loop_new (NULL, FALSE);
  g_mutex_init (&callback_data.init_mutex);
  g_mutex_lock (&callback_data.init_mutex);
  callback_data.gthread =
    g_thread_new ("test-non-default-callback-thread",
                  non_default_callback_thread_func, &callback_data);
  callback_data.state = 1;
  g_mutex_unlock (&callback_data.init_mutex);
  g_main_loop_run (callback_data.main_thread_loop);
  g_main_loop_unref (callback_data.main_thread_loop);
  g_thread_join (callback_data.gthread);
  g_mutex_clear (&callback_data.init_mutex);
  g_assert_cmpint (callback_data.state, ==, 7);
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
                           "name", "test user thread",
                           "thread-type", META_THREAD_TYPE_USER,
                           NULL);
  g_object_add_weak_pointer (G_OBJECT (thread), (gpointer *) &thread);
  g_assert_nonnull (thread);
  g_assert_null (error);
  g_assert_true (meta_thread_get_backend (thread) == backend);
  g_assert_cmpstr (meta_thread_get_name (thread), ==, "test user thread");
  test_thread = thread;

  run_thread_tests (thread);

  g_object_unref (thread);
  g_assert_null (thread);
  test_thread = NULL;
}

static void
meta_test_thread_kernel_common (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaThread *thread;
  g_autoptr (GError) error = NULL;

  thread = g_initable_new (META_TYPE_THREAD_TEST,
                           NULL, &error,
                           "backend", backend,
                           "name", "test kernel thread",
                           "thread-type", META_THREAD_TYPE_KERNEL,
                           NULL);
  g_object_add_weak_pointer (G_OBJECT (thread), (gpointer *) &thread);
  g_assert_nonnull (thread);
  g_assert_null (error);
  g_assert_true (meta_thread_get_backend (thread) == backend);
  g_assert_cmpstr (meta_thread_get_name (thread), ==, "test kernel thread");
  test_thread = thread;

  run_thread_tests (thread);

  g_object_unref (thread);
  g_assert_null (thread);
  test_thread = NULL;
}

static gpointer
late_callback (MetaThreadImpl  *thread_impl,
               gpointer         user_data,
               GError         **error)
{
  gboolean *done = user_data;

  *done = TRUE;

  return NULL;
}

static void
meta_test_thread_late_callbacks_common (MetaThreadType thread_type)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaThread *thread;
  g_autoptr (GError) error = NULL;
  gboolean done = FALSE;

  thread = g_initable_new (META_TYPE_THREAD_TEST,
                           NULL, &error,
                           "backend", backend,
                           "name", "test late callback",
                           "thread-type", thread_type,
                           NULL);
  g_object_add_weak_pointer (G_OBJECT (thread), (gpointer *) &thread);
  g_assert_nonnull (thread);
  g_assert_null (error);

  meta_thread_post_impl_task (thread, late_callback, &done, NULL, NULL, NULL);

  g_object_unref (thread);
  g_assert_null (thread);
  g_assert_true (done);
}

static void
meta_test_thread_user_late_callbacks (void)
{
  meta_test_thread_late_callbacks_common (META_THREAD_TYPE_USER);
}

static void
meta_test_thread_kernel_late_callbacks (void)
{
  meta_test_thread_late_callbacks_common (META_THREAD_TYPE_KERNEL);
}

typedef struct
{
  GThread *main_thread;
  GMainLoop *main_thread_loop;
  MetaThread *thread;
  GThread *gthread;
  GMutex init_mutex;

  gboolean done;

  GMainContext *main_context;
} RunTaskOffThreadData;

static gpointer
run_task_off_thread_in_impl (MetaThreadImpl  *thread_impl,
                             gpointer         user_data,
                             GError         **error)
{
  RunTaskOffThreadData *data = user_data;

  g_assert_true (data->gthread != g_thread_self ());

  g_assert_false (data->done);
  data->done = TRUE;

  return GINT_TO_POINTER (42);
}

static gpointer
run_task_off_thread_thread_func (gpointer user_data)
{
  RunTaskOffThreadData *data = user_data;
  gpointer result;

  g_mutex_lock (&data->init_mutex);
  g_mutex_unlock (&data->init_mutex);

  g_assert_true (data->gthread == g_thread_self ());

  result = meta_thread_run_impl_task_sync (data->thread,
                                           run_task_off_thread_in_impl,
                                           data,
                                           NULL);
  g_assert_cmpint (GPOINTER_TO_INT (result), ==, 42);
  g_assert_true (data->done);

  g_idle_add (quit_main_loop, data->main_thread_loop);

  return NULL;
}

static void
meta_test_thread_run_task_off_thread_common (MetaThreadType thread_type)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  g_autoptr (GError) error = NULL;
  RunTaskOffThreadData data = { 0 };

  g_mutex_init (&data.init_mutex);
  g_mutex_lock (&data.init_mutex);

  data.thread = g_initable_new (META_TYPE_THREAD_TEST,
                                NULL, &error,
                                "backend", backend,
                                "name", "test run task off thread",
                                "thread-type", thread_type,
                                NULL);
  g_object_add_weak_pointer (G_OBJECT (data.thread), (gpointer *) &data.thread);
  g_assert_nonnull (data.thread);
  g_assert_null (error);

  data.main_thread = g_thread_self ();
  data.main_thread_loop = g_main_loop_new (NULL, FALSE);
  data.gthread = g_thread_new ("run task off thread test",
                               run_task_off_thread_thread_func,
                               &data);
  g_assert_true (data.main_thread != data.gthread);

  g_mutex_unlock (&data.init_mutex);

  g_main_loop_run (data.main_thread_loop);
  g_main_loop_unref (data.main_thread_loop);

  g_thread_join (data.gthread);
  g_mutex_clear (&data.init_mutex);

  g_object_unref (data.thread);
  g_assert_null (data.thread);
}

static void
meta_test_thread_user_run_task_off_thread (void)
{
  meta_test_thread_run_task_off_thread_common (META_THREAD_TYPE_USER);
}

static void
meta_test_thread_kernel_run_task_off_thread (void)
{
  meta_test_thread_run_task_off_thread_common (META_THREAD_TYPE_KERNEL);
}

static gpointer
assert_not_thread (MetaThreadImpl  *thread_impl,
                   gpointer         user_data,
                   GError         **error)
{
  GThread **thread_to_check = user_data;

  g_assert_true (g_steal_pointer (thread_to_check) != g_thread_self ());

  return NULL;
}

static gpointer
assert_thread (MetaThreadImpl  *thread_impl,
               gpointer         user_data,
               GError         **error)
{
  GThread **thread_to_check = user_data;

  g_assert_true (g_steal_pointer (thread_to_check) == g_thread_self ());

  return NULL;
}

static void
meta_test_thread_change_thread_type (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaThread *thread;
  g_autoptr (GError) error = NULL;
  GThread *main_thread;
  GThread *thread_test;

  thread = g_initable_new (META_TYPE_THREAD_TEST,
                           NULL, &error,
                           "backend", backend,
                           "name", "test late callback",
                           "thread-type", META_THREAD_TYPE_KERNEL,
                           NULL);
  g_object_add_weak_pointer (G_OBJECT (thread), (gpointer *) &thread);
  g_assert_nonnull (thread);
  g_assert_null (error);

  main_thread = g_thread_self ();

  thread_test = main_thread;
  meta_thread_post_impl_task (thread, assert_not_thread, &thread_test, NULL,
                              NULL, NULL);

  meta_thread_reset_thread_type (thread, META_THREAD_TYPE_USER);
  g_assert_null (thread_test);

  thread_test = main_thread;
  meta_thread_post_impl_task (thread, assert_thread, &thread_test, NULL,
                              NULL, NULL);

  meta_thread_reset_thread_type (thread, META_THREAD_TYPE_KERNEL);
  g_assert_null (thread_test);

  thread_test = main_thread;
  meta_thread_post_impl_task (thread, assert_not_thread, &thread_test, NULL,
                              NULL, NULL);

  g_object_unref (thread);
  g_assert_null (thread);
  g_assert_null (thread_test);
}

static GVariant *
call_rtkit_mock_method (const char *method,
                        GVariant   *argument)
{
  g_autoptr (GDBusConnection) connection = NULL;
  GError *local_error = NULL;
  GVariant *ret;

  connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM,
                               NULL, NULL);

  ret = g_dbus_connection_call_sync (connection,
                                     "org.freedesktop.RealtimeKit1",
                                     "/org/freedesktop/RealtimeKit1",
                                     "org.freedesktop.DBus.Mock",
                                     method, argument,
                                     NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, -1,
                                     NULL, &local_error);
  if (!ret)
    g_error ("Failed to get tread priority: %s", local_error->message);

  return ret;
}

static void
assert_thread_levels (uint32_t expected_priority,
                      int32_t  expected_nice_level)
{
  g_autoptr (GVariant) priority_variant = NULL;
  g_autoptr (GVariant) nice_level_variant = NULL;
  uint32_t priority = UINT32_MAX;
  int32_t nice_level = INT32_MAX;

  priority_variant =
    call_rtkit_mock_method ("GetThreadPriority",
                            g_variant_new ("(t)", gettid ()));

  g_variant_get (priority_variant, "(u)", &priority);
  g_assert_cmpint (priority, ==, expected_priority);

  nice_level_variant =
    call_rtkit_mock_method ("GetThreadNiceLevel",
                            g_variant_new ("(t)", gettid ()));

  g_variant_get (nice_level_variant, "(i)", &nice_level);
  g_assert_cmpint (nice_level, ==, expected_nice_level);
}

static gpointer
assert_realtime (MetaThreadImpl  *thread_impl,
                 gpointer         user_data,
                 GError         **error)
{

  g_assert_cmpint (meta_thread_impl_get_scheduling_priority (thread_impl),
                   ==,
                   META_SCHEDULING_PRIORITY_REALTIME);

  assert_thread_levels (20, 0);

  return NULL;
}

static void
meta_test_thread_realtime (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaThread *thread;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) ret = NULL;

  ret = call_rtkit_mock_method ("Reset", NULL);

  thread = g_initable_new (META_TYPE_THREAD_TEST,
                           NULL, &error,
                           "backend", backend,
                           "name", "test realtime",
                           "thread-type", META_THREAD_TYPE_KERNEL,
                           "preferred-scheduling-priority", META_SCHEDULING_PRIORITY_REALTIME,
                           NULL);
  g_object_add_weak_pointer (G_OBJECT (thread), (gpointer *) &thread);
  g_assert_nonnull (thread);
  g_assert_null (error);

  meta_thread_post_impl_task (thread, assert_realtime, NULL, NULL,
                              NULL, NULL);

  g_object_unref (thread);
  g_assert_null (thread);
  g_assert_null (test_thread);
}

static gpointer
assert_high_priority (MetaThreadImpl  *thread_impl,
                      gpointer         user_data,
                      GError         **error)
{
  g_assert_cmpint (meta_thread_impl_get_scheduling_priority (thread_impl),
                   ==,
                   META_SCHEDULING_PRIORITY_HIGH_PRIORITY);

  assert_thread_levels (0, -15);

  return NULL;
}

static void
meta_test_thread_high_priority (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaThread *thread;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) ret = NULL;

  ret = call_rtkit_mock_method ("Reset", NULL);

  thread = g_initable_new (META_TYPE_THREAD_TEST,
                           NULL, &error,
                           "backend", backend,
                           "name", "test realtime",
                           "thread-type", META_THREAD_TYPE_KERNEL,
                           "preferred-scheduling-priority", META_SCHEDULING_PRIORITY_HIGH_PRIORITY,
                           NULL);
  g_object_add_weak_pointer (G_OBJECT (thread), (gpointer *) &thread);
  g_assert_nonnull (thread);
  g_assert_null (error);

  meta_thread_post_impl_task (thread, assert_high_priority, NULL, NULL,
                              NULL, NULL);

  g_object_unref (thread);
  g_assert_null (thread);
  g_assert_null (test_thread);
}

static gpointer
assert_no_realtime (MetaThreadImpl  *thread_impl,
                    gpointer         user_data,
                    GError         **error)
{
  g_autoptr (GVariant) ret = NULL;

  g_assert_cmpint (meta_thread_impl_get_scheduling_priority (thread_impl),
                   ==,
                   META_SCHEDULING_PRIORITY_NORMAL);

  assert_thread_levels (0, 0);

  return NULL;
}

static void
meta_test_thread_no_realtime (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaThread *thread;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) ret = NULL;

  ret = call_rtkit_mock_method ("Reset", NULL);

  thread = g_initable_new (META_TYPE_THREAD_TEST,
                           NULL, &error,
                           "backend", backend,
                           "name", "test realtime",
                           "thread-type", META_THREAD_TYPE_USER,
                           "preferred-scheduling-priority", META_SCHEDULING_PRIORITY_REALTIME,
                           NULL);
  g_object_add_weak_pointer (G_OBJECT (thread), (gpointer *) &thread);
  g_assert_nonnull (thread);
  g_assert_null (error);

  meta_thread_post_impl_task (thread, assert_no_realtime, NULL, NULL,
                              NULL, NULL);

  g_object_unref (thread);
  g_assert_null (thread);
  g_assert_null (test_thread);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/thread/user/common",
                   meta_test_thread_user_common);
  g_test_add_func ("/backends/native/thread/kernel/common",
                   meta_test_thread_kernel_common);
  g_test_add_func ("/backends/native/thread/user/late-callbacks",
                   meta_test_thread_user_late_callbacks);
  g_test_add_func ("/backends/native/thread/kernel/late-callbacks",
                   meta_test_thread_kernel_late_callbacks);
  g_test_add_func ("/backends/native/thread/user/run-task-off-thread",
                   meta_test_thread_user_run_task_off_thread);
  g_test_add_func ("/backends/native/thread/kernel/run-task-off-thread",
                   meta_test_thread_kernel_run_task_off_thread);
  g_test_add_func ("/backends/native/thread/change-thread-type",
                   meta_test_thread_change_thread_type);
  g_test_add_func ("/backends/native/thread/realtime",
                   meta_test_thread_realtime);
  g_test_add_func ("/backends/native/thread/high-priority",
                   meta_test_thread_high_priority);
  g_test_add_func ("/backends/native/thread/no-realtime",
                   meta_test_thread_no_realtime);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  test_context = context;
  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
