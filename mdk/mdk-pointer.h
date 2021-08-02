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
#include <gdk/gdk.h>

#include "mdk-types.h"

typedef enum _MdkScrollFlags
{
  MDK_SCROLL_FLAG_FINISH = 1 << 0,
  MDK_SCROLL_FLAG_SOURCE_WHEEL = 1 << 1,
  MDK_SCROLL_FLAG_SOURCE_FINGER = 1 << 2,
  MDK_SCROLL_FLAG_SOURCE_CONTINUOUS = 1 << 3,
} MdkScrollFlags;

#define MDK_TYPE_POINTER (mdk_pointer_get_type ())
G_DECLARE_FINAL_TYPE (MdkPointer, mdk_pointer,
                      MDK, POINTER,
                      GObject)

MdkPointer * mdk_pointer_new (MdkSession                  *session,
                              MdkDBusRemoteDesktopSession *session_proxy,
                              MdkMonitor                  *monitor);

void mdk_pointer_release_all (MdkPointer *pointer);

void mdk_pointer_notify_motion (MdkPointer *pointer,
                                double      x,
                                double      y);

void mdk_pointer_notify_button (MdkPointer *pointer,
                                int32_t     button,
                                int         state);

void mdk_pointer_notify_scroll (MdkPointer *pointer,
                                double      dx,
                                double      dy);

void mdk_pointer_notify_scroll_end (MdkPointer *pointer);

void mdk_pointer_notify_scroll_discrete (MdkPointer         *pointer,
                                         GdkScrollDirection  direction);
