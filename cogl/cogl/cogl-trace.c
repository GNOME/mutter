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
#define BUFFER_LENGTH (4096 * 4)

static void cogl_trace_thread_context_free (gpointer data);

GPrivate cogl_trace_thread_data = G_PRIVATE_INIT (cogl_trace_thread_context_free);
CoglTraceContext *cogl_trace_context;
GMutex cogl_trace_mutex;

static CoglTraceContext *
cogl_trace_context_new (int fd)
{
  CoglTraceContext *context;
  SysprofCaptureWriter *writer;

  g_debug ("Initializing trace context with fd=%d", fd);

  if (fd == -1)
    {
      writer = sysprof_capture_writer_new (COGL_TRACE_OUTPUT_FILE,
                                           BUFFER_LENGTH);
    }
  else
    {
      writer = sysprof_capture_writer_new_from_fd (fd, BUFFER_LENGTH);
    }

  context = g_new0 (CoglTraceContext, 1);
  context->writer = writer;
  return context;
}

static void
cogl_trace_context_free (CoglTraceContext *trace_context)
{
  g_clear_pointer (&trace_context->writer, sysprof_capture_writer_unref);
  g_free (trace_context);
}

static void
ensure_trace_context (int fd)
{
  g_mutex_lock (&cogl_trace_mutex);
  if (!cogl_trace_context)
    cogl_trace_context = cogl_trace_context_new (fd);
  g_mutex_unlock (&cogl_trace_mutex);
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
  CoglTraceThreadContext *thread_context =
    g_private_get (&cogl_trace_thread_data);
  int fd = GPOINTER_TO_INT (user_data);

  ensure_trace_context (fd);

  if (thread_context)
    {
      g_warning ("Tracing already enabled");
      return G_SOURCE_REMOVE;
    }

  thread_context = cogl_trace_thread_context_new ();
  g_private_set (&cogl_trace_thread_data, thread_context);

  return G_SOURCE_REMOVE;
}

static void
cogl_trace_thread_context_free (gpointer data)
{
  CoglTraceThreadContext *thread_context = data;

  if (!thread_context)
    return;

  g_free (thread_context->group);
  g_free (thread_context);
}

static gboolean
disable_tracing_idle_callback (gpointer user_data)
{
  CoglTraceThreadContext *thread_context =
    g_private_get (&cogl_trace_thread_data);
  CoglTraceContext *trace_context;

  if (!thread_context)
    {
      g_warning ("Tracing not enabled");
      return G_SOURCE_REMOVE;
    }

  g_private_replace (&cogl_trace_thread_data, NULL);

  g_mutex_lock (&cogl_trace_mutex);
  trace_context = cogl_trace_context;
  sysprof_capture_writer_flush (trace_context->writer);

  g_clear_pointer (&cogl_trace_context, cogl_trace_context_free);

  g_mutex_unlock (&cogl_trace_mutex);

  return G_SOURCE_REMOVE;
}

void
cogl_set_tracing_enabled_on_thread (GMainContext *main_context,
                                    int           fd)
{
  GSource *source;

  source = g_idle_source_new ();

  g_source_set_callback (source, enable_tracing_idle_callback, GINT_TO_POINTER (fd), NULL);

  g_source_attach (source, main_context);
  g_source_unref (source);
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
cogl_set_tracing_enabled_on_thread (void *data,
                                    int   fd)
{
  fprintf (stderr, "Tracing not enabled");
}

void
cogl_set_tracing_disabled_on_thread (void *data)
{
  fprintf (stderr, "Tracing not enabled");
}

#endif /* HAVE_TRACING */
