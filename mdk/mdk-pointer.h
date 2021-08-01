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

#include <glib-object.h>

#include "mdk-types.h"

#define MDK_TYPE_POINTER (mdk_pointer_get_type ())
G_DECLARE_FINAL_TYPE (MdkPointer, mdk_pointer,
                      MDK, POINTER,
                      GObject)

MdkPointer * mdk_pointer_new (MdkSession                  *session,
                              MdkDBusRemoteDesktopSession *session_proxy,
                              MdkMonitor                  *monitor);

void mdk_pointer_notify_motion (MdkPointer *pointer,
                                double      x,
                                double      y);

void mdk_pointer_notify_button (MdkPointer *pointer,
                                int32_t     button,
                                int         state);
