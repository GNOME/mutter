/*
 * Copyright 2018 Red Hat, Inc.
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

#include "cogl-config.h"

#ifdef HAVE_TRACING

#include "cogl/cogl-trace.h"

#include <sysprof-capture.h>
#include <syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define COGL_TRACE_OUTPUT_FILE "cogl-trace-sp-capture.syscap"

typedef struct
{
  int   fd;
  char *filename;
} TraceData;

static void
trace_data_free (gpointer user_data)
{
  TraceData *data = user_data;

  data->fd = -1;
  g_clear_pointer (&data->filename, g_free);
  g_free (data);
}

__thread CoglTraceThreadContext *cogl_trace_thread_context;
CoglTraceContext *cogl_trace_context;
GMutex cogl_trace_mutex;

static CoglTraceContext *
cogl_trace_context_new (int          fd,
                        const gchar *filename)
{
  CoglTraceContext *context;
  SpCaptureWriter *writer;

  if (fd != -1)
    {
      g_debug ("Initializing trace context with fd=%d", fd);
      writer = sp_capture_writer_new_from_fd (fd, 4096 * 4);
    }
  else if (filename != NULL)
    {
      g_debug ("Initializing trace context with filename='%s'", filename);
      writer = sp_capture_writer_new (filename, 4096 * 4);
    }
  else
    {
      g_debug ("Initializing trace context with default dilename");
      writer = sp_capture_writer_new (COGL_TRACE_OUTPUT_FILE, 4096 * 4);
    }

  context = g_new0 (CoglTraceContext, 1);
  context->writer = writer;
  return context;
}

static void
cogl_trace_context_free (CoglTraceContext *trace_context)
{
  g_clear_pointer (&trace_context->writer, sp_capture_writer_unref);
  g_free (trace_context);
}

static void
ensure_trace_context (TraceData *data)
{
  static GMutex mutex;

  g_mutex_lock (&mutex);
  if (!cogl_trace_context)
    cogl_trace_context = cogl_trace_context_new (data->fd, data->filename);
  g_mutex_unlock (&mutex);
}

static void
ensure_sigpipe_ignored (void)
{
  signal (SIGPIPE, SIG_IGN);
}

static CoglTraceThreadContext *
cogl_trace_thread_context_new (void)
{
  CoglTraceThreadContext *thread_context;
  pid_t tid;

  tid = (pid_t) syscall (SYS_gettid);

  thread_context = g_new0 (CoglTraceThreadContext, 1);
  thread_context->cpu_id = -1;
  thread_context->pid = getpid ();
  thread_context->group = g_strdup_printf ("t:%d", tid);

  return thread_context;
}

static gboolean
enable_tracing_idle_callback (gpointer user_data)
{
  TraceData *data = user_data;

  ensure_sigpipe_ignored ();
  ensure_trace_context (data);

  if (cogl_trace_thread_context)
    {
      g_warning ("Tracing already enabled");
      return G_SOURCE_REMOVE;
    }

  cogl_trace_thread_context = cogl_trace_thread_context_new ();

  return G_SOURCE_REMOVE;
}

static void
cogl_trace_thread_context_free (CoglTraceThreadContext *thread_context)
{
  g_free (thread_context->group);
  g_free (thread_context);
}

static gboolean
disable_tracing_idle_callback (gpointer user_data)
{
  CoglTraceContext *trace_context;

  if (!cogl_trace_thread_context)
    {
      g_warning ("Tracing not enabled");
      return G_SOURCE_REMOVE;
    }

  cogl_trace_thread_context_free (cogl_trace_thread_context);
  cogl_trace_thread_context = NULL;

  g_mutex_lock (&cogl_trace_mutex);
  trace_context = cogl_trace_context;
  sp_capture_writer_flush (trace_context->writer);

  cogl_trace_context_free (cogl_trace_context);
  cogl_trace_context = NULL;

  g_mutex_unlock (&cogl_trace_mutex);

  return G_SOURCE_REMOVE;
}

static void
set_tracing_enabled_on_thread (GMainContext *main_context,
                               int           fd,
                               const char   *filename)
{
  TraceData *data;
  GSource *source;

  data = g_new0 (TraceData, 1);
  data->fd = fd;
  data->filename = filename ? strdup (filename) : NULL;

  source = g_idle_source_new ();

  g_source_set_callback (source,
                         enable_tracing_idle_callback,
                         data,
                         trace_data_free);

  g_source_attach (source, main_context);
  g_source_unref (source);
}

void
cogl_set_tracing_enabled_on_thread_with_fd (GMainContext *main_context,
                                            int           fd)
{
  set_tracing_enabled_on_thread (main_context, fd, NULL);
}

void
cogl_set_tracing_enabled_on_thread (GMainContext *main_context,
                                    const char   *filename)
{
  set_tracing_enabled_on_thread (main_context, -1, filename);
}

void
cogl_set_tracing_disabled_on_thread (GMainContext *main_context)
{
  GSource *source;

  source = g_idle_source_new ();

  g_source_set_callback (source, disable_tracing_idle_callback, NULL, NULL);

  g_source_attach (source, main_context);
  g_source_unref (source);
}

#else

#include <string.h>
#include <stdio.h>

void
cogl_set_tracing_enabled_on_thread_with_fd (void *data,
                                            int   fd)
{
  fprintf (stderr, "Tracing not enabled");
}

void
cogl_set_tracing_enabled_on_thread (void       *data,
                                    const char *filename)
{
  fprintf (stderr, "Tracing not enabled");
}

void
cogl_set_tracing_disabled_on_thread (void *data)
{
  fprintf (stderr, "Tracing not enabled");
}

#endif /* HAVE_TRACING */
