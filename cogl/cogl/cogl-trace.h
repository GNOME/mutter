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

#ifndef COGL_TRACE_H
#define COGL_TRACE_H

#include "cogl-config.h"

#ifdef HAVE_TRACING

#include <glib.h>
#include <sysprof-capture-writer.h>
#include <sysprof-clock.h>
#include <stdint.h>
#include <errno.h>

typedef struct _CoglTraceContext
{
  SysprofCaptureWriter *writer;
} CoglTraceContext;

typedef struct _CoglTraceThreadContext
{
  int cpu_id;
  GPid pid;
  char *group;
} CoglTraceThreadContext;

typedef struct _CoglTraceHead
{
  SysprofTimeStamp begin_time;
  const char *name;
} CoglTraceHead;

extern GPrivate cogl_trace_thread_data;
extern CoglTraceContext *cogl_trace_context;
extern GMutex cogl_trace_mutex;

void cogl_set_tracing_enabled_on_thread_with_fd (GMainContext *main_context,
                                                 const char   *group,
                                                 int           fd);

void cogl_set_tracing_enabled_on_thread (GMainContext *main_context,
                                         const char   *group,
                                         const char   *filename);

void cogl_set_tracing_disabled_on_thread (GMainContext *main_context);

static inline void
cogl_trace_begin (CoglTraceHead *head,
                  const char    *name)
{
  head->begin_time = g_get_monotonic_time () * 1000;
  head->name = name;
}

static inline void
cogl_trace_end (CoglTraceHead *head)
{
  SysprofTimeStamp end_time;
  CoglTraceContext *trace_context;
  CoglTraceThreadContext *trace_thread_context;

  end_time = g_get_monotonic_time () * 1000;
  trace_context = cogl_trace_context;
  trace_thread_context = g_private_get (&cogl_trace_thread_data);

  g_mutex_lock (&cogl_trace_mutex);
  if (!sysprof_capture_writer_add_mark (trace_context->writer,
                                        head->begin_time,
                                        trace_thread_context->cpu_id,
                                        trace_thread_context->pid,
                                        (uint64_t) end_time - head->begin_time,
                                        trace_thread_context->group,
                                        head->name,
                                        NULL))
    {
      /* XXX: g_main_context_get_thread_default() might be wrong, it probably
       * needs to store the GMainContext in CoglTraceThreadContext when creating
       * and use it here.
       */
      if (errno == EPIPE)
        cogl_set_tracing_disabled_on_thread (g_main_context_get_thread_default ());
    }
  g_mutex_unlock (&cogl_trace_mutex);
}

static inline void
cogl_auto_trace_end_helper (CoglTraceHead **head)
{
  if (*head)
    cogl_trace_end (*head);
}

#define COGL_TRACE_BEGIN(Name, description) \
  CoglTraceHead CoglTrace##Name = { 0 }; \
  if (g_private_get (&cogl_trace_thread_data)) \
    cogl_trace_begin (&CoglTrace##Name, description); \

#define COGL_TRACE_END(Name)\
  if (g_private_get (&cogl_trace_thread_data)) \
    cogl_trace_end (&CoglTrace##Name);

#define COGL_TRACE_BEGIN_SCOPED(Name, description) \
  CoglTraceHead CoglTrace##Name = { 0 }; \
  __attribute__((cleanup (cogl_auto_trace_end_helper))) \
    CoglTraceHead *ScopedCoglTrace##Name = NULL; \
  if (g_private_get (&cogl_trace_thread_data)) \
    { \
      cogl_trace_begin (&CoglTrace##Name, description); \
      ScopedCoglTrace##Name = &CoglTrace##Name; \
    }

#else /* HAVE_TRACING */

#include <stdio.h>

#define COGL_TRACE_BEGIN(Name, description) (void) 0
#define COGL_TRACE_END(Name) (void) 0
#define COGL_TRACE_BEGIN_SCOPED(Name, description) (void) 0

void cogl_set_tracing_enabled_on_thread_with_fd (void       *data,
                                                 const char *group,
                                                 int         fd);
void cogl_set_tracing_enabled_on_thread (void       *data,
                                         const char *group,
                                         const char *filename);
void cogl_set_tracing_disabled_on_thread (void *data);

#endif /* HAVE_TRACING */

#endif /* COGL_TRACE_H */
