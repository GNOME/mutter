/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015-2017 Red Hat Inc.
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

#pragma once

#include <glib-object.h>

#include "backends/meta-backend-types.h"
#include "backends/meta-dbus-session-manager.h"
#include "backends/meta-dbus-session-watcher.h"

#include "meta-dbus-remote-desktop.h"

typedef enum _MetaRemoteDesktopDeviceTypes
{
  META_REMOTE_DESKTOP_DEVICE_TYPE_NONE = 0,
  META_REMOTE_DESKTOP_DEVICE_TYPE_KEYBOARD = 1 << 0,
  META_REMOTE_DESKTOP_DEVICE_TYPE_POINTER = 1 << 1,
  META_REMOTE_DESKTOP_DEVICE_TYPE_TOUCHSCREEN = 1 << 2,
} MetaRemoteDesktopDeviceTypes;

typedef struct _MetaRemoteDesktopSession MetaRemoteDesktopSession;

#define META_TYPE_REMOTE_DESKTOP (meta_remote_desktop_get_type ())
G_DECLARE_FINAL_TYPE (MetaRemoteDesktop, meta_remote_desktop,
                      META, REMOTE_DESKTOP,
                      MetaDbusSessionManager)

MetaRemoteDesktop * meta_remote_desktop_new (MetaBackend *backend);

gboolean meta_remote_desktop_is_enabled (MetaRemoteDesktop *remote_desktop);
