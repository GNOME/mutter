/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat, Inc.
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
 */

#pragma once

#include <glib.h>
#include <X11/Xlib.h>
#include <X11/extensions/sync.h>

#include "backends/meta-backend-types.h"
#include "backends/meta-virtual-monitor.h"
#include "meta/window.h"

#define META_TEST_CLIENT_ERROR meta_test_client_error_quark ()

#define META_TEST_LOG_CALL(description, call) \
  G_STMT_START { \
    g_debug ("%s: %s: %s", G_STRFUNC, G_STRLOC, description); \
    call; \
  } G_STMT_END

typedef enum _MetaClientError
{
  META_TEST_CLIENT_ERROR_BAD_COMMAND,
  META_TEST_CLIENT_ERROR_RUNTIME_ERROR,
  META_TEST_CLIENT_ERROR_ASSERTION_FAILED
} MetaClientError;

META_EXPORT
GQuark meta_test_client_error_quark (void);

typedef struct _MetaAsyncWaiter MetaAsyncWaiter;
typedef struct _MetaTestClient MetaTestClient;

META_EXPORT
gboolean meta_async_waiter_process_x11_event (MetaAsyncWaiter       *waiter,
                                              MetaX11Display        *x11_display,
                                              XSyncAlarmNotifyEvent *event);

META_EXPORT
void meta_async_waiter_set_and_wait (MetaAsyncWaiter *waiter);

META_EXPORT
MetaAsyncWaiter * meta_async_waiter_new (MetaX11Display *x11_display);

META_EXPORT
void meta_async_waiter_destroy (MetaAsyncWaiter *waiter);

META_EXPORT
char * meta_test_client_get_id (MetaTestClient *client);

META_EXPORT
gboolean meta_test_client_wait (MetaTestClient  *client,
                                GError         **error);

META_EXPORT
gboolean meta_test_client_dov (MetaTestClient  *client,
                               GError         **error,
                               va_list          vap);

META_EXPORT
void meta_test_client_run (MetaTestClient *client,
                           const char     *script);

META_EXPORT
gboolean meta_test_client_do (MetaTestClient  *client,
                              GError         **error,
                              ...) G_GNUC_NULL_TERMINATED;

META_EXPORT
MetaWindow * meta_find_window_from_title (MetaContext *context,
                                          const char  *title);

META_EXPORT
MetaWindow * meta_test_client_find_window (MetaTestClient  *client,
                                           const char      *window_id,
                                           GError         **error);

META_EXPORT
void meta_test_client_wait_for_window_shown (MetaTestClient *client,
                                             MetaWindow     *window);

META_EXPORT
gboolean meta_test_client_quit (MetaTestClient  *client,
                                GError         **error);

META_EXPORT
MetaTestClient * meta_test_client_new (MetaContext           *context,
                                       const char            *id,
                                       MetaWindowClientType   type,
                                       GError               **error);

META_EXPORT
void meta_test_client_destroy (MetaTestClient *client);

META_EXPORT
void meta_set_custom_monitor_config_full (MetaBackend            *backend,
                                          const char             *filename,
                                          MetaMonitorsConfigFlag  configs_flags);

META_EXPORT
void meta_wait_for_monitors_changed (MetaContext *context);

META_EXPORT
void meta_wait_for_paint (MetaContext *context);

META_EXPORT
MetaVirtualMonitor * meta_create_test_monitor (MetaContext *context,
                                               int          width,
                                               int          height,
                                               float        refresh_rate);

META_EXPORT
void meta_flush_input (MetaContext *context);
