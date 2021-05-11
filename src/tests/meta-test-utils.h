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

#ifndef META_TEST_UTILS_H
#define META_TEST_UTILS_H

#include <glib.h>
#include <X11/Xlib.h>
#include <X11/extensions/sync.h>

#include "meta/window.h"

#define META_TEST_CLIENT_ERROR meta_test_client_error_quark ()

typedef enum _MetaClientError
{
  META_TEST_CLIENT_ERROR_BAD_COMMAND,
  META_TEST_CLIENT_ERROR_RUNTIME_ERROR,
  META_TEST_CLIENT_ERROR_ASSERTION_FAILED
} MetaClientError;

GQuark meta_test_client_error_quark (void);

typedef struct _MetaAsyncWaiter MetaAsyncWaiter;
typedef struct _MetaTestClient MetaTestClient;

gboolean meta_async_waiter_process_x11_event (MetaAsyncWaiter       *waiter,
                                              MetaX11Display        *display,
                                              XSyncAlarmNotifyEvent *event);

void meta_async_waiter_set_and_wait (MetaAsyncWaiter *waiter);

MetaAsyncWaiter * meta_async_waiter_new (void);

void meta_async_waiter_destroy (MetaAsyncWaiter *waiter);

char * meta_test_client_get_id (MetaTestClient *client);

gboolean meta_test_client_process_x11_event (MetaTestClient        *client,
                                             MetaX11Display        *x11_display,
                                             XSyncAlarmNotifyEvent *event);

gboolean meta_test_client_wait (MetaTestClient  *client,
                                GError         **error);

gboolean meta_test_client_do (MetaTestClient  *client,
                              GError         **error,
                              ...) G_GNUC_NULL_TERMINATED;

MetaWindow * meta_test_client_find_window (MetaTestClient  *client,
                                           const char      *window_id,
                                           GError         **error);

void meta_test_client_wait_for_window_shown (MetaTestClient *client,
                                             MetaWindow     *window);

gboolean meta_test_client_quit (MetaTestClient  *client,
                                GError         **error);

MetaTestClient * meta_test_client_new (MetaContext           *context,
                                       const char            *id,
                                       MetaWindowClientType   type,
                                       GError               **error);

void meta_test_client_destroy (MetaTestClient *client);

const char * meta_test_get_plugin_name (void);

#endif /* TEST_UTILS_H */
