/*
 * Copyright (C) 2021 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <gio/gio.h>
#include <glib-object.h>

#include "mdk-types.h"

#define MDK_TYPE_SESSION (mdk_session_get_type ())
G_DECLARE_FINAL_TYPE (MdkSession, mdk_session,
                      MDK, SESSION,
                      GObject)

void mdk_session_create_monitor_async (MdkSession          *session,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data);

char * mdk_session_create_monitor_finish (MdkSession    *session,
                                          GAsyncResult  *res,
                                          GError       **error);

MdkContext * mdk_session_get_context (MdkSession *session);

MdkSeat * mdk_session_get_default_seat (MdkSession *session);
