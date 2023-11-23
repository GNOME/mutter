/*
 * Copyright 2018 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>
#include <stdint.h>
#include <errno.h>

#include "config.h"
#include "cogl/cogl-macros.h"

#ifdef HAVE_PROFILER

typedef struct _CoglTraceContext CoglTraceContext;

typedef struct _CoglTraceHead
{
  uint64_t begin_time;
  const char *name;
  char *description;
} CoglTraceHead;

COGL_EXPORT
GPrivate cogl_trace_thread_data;
COGL_EXPORT
CoglTraceContext *cogl_trace_context;
COGL_EXPORT
GMutex cogl_trace_mutex;

COGL_EXPORT
gboolean cogl_start_tracing_with_path (const char  *filename,
                                       GError     **error);

COGL_EXPORT
gboolean cogl_start_tracing_with_fd (int      fd,
                                     GError **error);

COGL_EXPORT
void cogl_stop_tracing (void);

COGL_EXPORT
void cogl_set_tracing_enabled_on_thread (GMainContext *main_context,
                                         const char   *group);

COGL_EXPORT
void cogl_set_tracing_disabled_on_thread (GMainContext *main_context);

static inline void
cogl_trace_begin (CoglTraceHead *head,
                  const char    *name)
{
  head->begin_time = g_get_monotonic_time () * 1000;
  head->name = name;
}

COGL_EXPORT void
cogl_trace_end (CoglTraceHead *head);

COGL_EXPORT void
cogl_trace_describe (CoglTraceHead *head,
                     const char    *description);

COGL_EXPORT void
cogl_trace_mark (const char *name,
                 const char *description);

static inline void
cogl_auto_trace_end_helper (CoglTraceHead **head)
{
  if (*head)
    cogl_trace_end (*head);
}

static inline gboolean
cogl_is_tracing_enabled (void)
{
  return !!g_private_get (&cogl_trace_thread_data);
}

#define COGL_TRACE_BEGIN_SCOPED(Name, name) \
  CoglTraceHead CoglTrace##Name = { 0 }; \
  __attribute__((cleanup (cogl_auto_trace_end_helper))) \
    CoglTraceHead *ScopedCoglTrace##Name = NULL; \
  if (cogl_is_tracing_enabled ()) \
    { \
      cogl_trace_begin (&CoglTrace##Name, name); \
      ScopedCoglTrace##Name = &CoglTrace##Name; \
    }

#define COGL_TRACE_END(Name)\
  if (cogl_is_tracing_enabled ()) \
    { \
      cogl_trace_end (&CoglTrace##Name); \
      ScopedCoglTrace##Name = NULL; \
    }

#define COGL_TRACE_DESCRIBE(Name, description)\
  if (cogl_is_tracing_enabled ()) \
    cogl_trace_describe (&CoglTrace##Name, description);

#define COGL_TRACE_SCOPED_ANCHOR(Name) \
  CoglTraceHead G_GNUC_UNUSED CoglTrace##Name = { 0 }; \
  __attribute__((cleanup (cogl_auto_trace_end_helper))) \
    CoglTraceHead *ScopedCoglTrace##Name = NULL; \

#define COGL_TRACE_BEGIN_ANCHORED(Name, name) \
  if (cogl_is_tracing_enabled ()) \
    { \
      cogl_trace_begin (&CoglTrace##Name, name); \
      ScopedCoglTrace##Name = &CoglTrace##Name; \
    }

#define COGL_TRACE_MESSAGE(name, ...) \
  G_STMT_START \
    { \
      if (cogl_is_tracing_enabled ()) \
        { \
          g_autofree char *CoglTraceMessage = g_strdup_printf (__VA_ARGS__); \
          cogl_trace_mark (name, CoglTraceMessage); \
        } \
    } \
  G_STMT_END

#else /* HAVE_PROFILER */

#include <stdio.h>

#define COGL_TRACE_BEGIN_SCOPED(Name, name) (void) 0
#define COGL_TRACE_END(Name) (void) 0
#define COGL_TRACE_DESCRIBE(Name, description) (void) 0
#define COGL_TRACE_SCOPED_ANCHOR(Name) (void) 0
#define COGL_TRACE_BEGIN_ANCHORED(Name, name) (void) 0
#define COGL_TRACE_MESSAGE(name, ...) (void) 0

COGL_EXPORT
gboolean cogl_start_tracing_with_path (const char  *filename,
                                       GError     **error);

COGL_EXPORT
gboolean cogl_start_tracing_with_fd (int      fd,
                                     GError **error);

COGL_EXPORT
void cogl_stop_tracing (void);

COGL_EXPORT
void cogl_set_tracing_enabled_on_thread (void       *data,
                                         const char *group);

COGL_EXPORT
void cogl_set_tracing_disabled_on_thread (void *data);

#endif /* HAVE_PROFILER */
