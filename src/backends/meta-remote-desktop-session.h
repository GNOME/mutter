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

#include "backends/meta-remote-desktop.h"
#include "backends/meta-screen-cast-session.h"

#define META_TYPE_REMOTE_DESKTOP_SESSION (meta_remote_desktop_session_get_type ())
META_EXPORT_TEST
G_DECLARE_FINAL_TYPE (MetaRemoteDesktopSession, meta_remote_desktop_session,
                      META, REMOTE_DESKTOP_SESSION,
                      MetaDBusRemoteDesktopSessionSkeleton)

#define META_TYPE_REMOTE_DESKTOP_SESSION_HANDLE (meta_remote_desktop_session_handle_get_type ())
G_DECLARE_FINAL_TYPE (MetaRemoteDesktopSessionHandle,
                      meta_remote_desktop_session_handle,
                      META, REMOTE_DESKTOP_SESSION_HANDLE,
                      MetaRemoteAccessHandle)

char * meta_remote_desktop_session_get_object_path (MetaRemoteDesktopSession *session);

gboolean meta_remote_desktop_session_register_screen_cast (MetaRemoteDesktopSession  *session,
                                                           MetaScreenCastSession     *screen_cast_session,
                                                           GError                   **error);

const char * meta_remote_desktop_session_acquire_mapping_id (MetaRemoteDesktopSession *session);

void meta_remote_desktop_session_release_mapping_id (MetaRemoteDesktopSession *session,
                                                     const char               *mapping_id);

META_EXPORT_TEST
void meta_remote_desktop_session_request_transfer (MetaRemoteDesktopSession  *session,
                                                   const char                *mime_type,
                                                   GTask                     *task);

META_EXPORT_TEST
MetaEis * meta_remote_desktop_session_get_eis (MetaRemoteDesktopSession *session);
