/*
 * Copyright (C) 2021-2024 Red Hat Inc.
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

#include <libei.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>

#include "meta-dbus-remote-desktop.h"
#include "meta-dbus-screen-cast.h"

typedef enum _CursorMode
{
  CURSOR_MODE_HIDDEN = 0,
  CURSOR_MODE_EMBEDDED = 1,
  CURSOR_MODE_METADATA = 2,
} CursorMode;

typedef enum _StreamType
{
  STREAM_TYPE_VIRTUAL,
  STREAM_TYPE_MONITOR,
} StreamType;

typedef struct _Stream
{
  MetaDBusScreenCastStream *proxy;
  uint32_t pipewire_node_id;
  struct spa_video_info_raw spa_format;
  struct pw_stream *pipewire_stream;
  struct spa_hook pipewire_stream_listener;
  enum pw_stream_state state;
  int buffer_count;

  StreamType stream_type;
  struct {
    int target_width;
    int target_height;
  } virtual;

  struct pw_buffer *buffer;

  CursorMode cursor_mode;
  int cursor_x;
  int cursor_y;
} Stream;

typedef struct _Session
{
  MetaDBusScreenCastSession *screen_cast_session_proxy;
  MetaDBusRemoteDesktopSession *remote_desktop_session_proxy;

  GArray *seat_caps;

  struct ei *ei;
  GSource *ei_source;
  struct ei_seat *ei_seat;
  struct ei_device *keyboard;
  struct ei_device *pointer;
  uint32_t ei_sequence;
  struct ei_ping *ping;
} Session;

typedef struct _RemoteDesktop
{
  MetaDBusRemoteDesktop *proxy;
} RemoteDesktop;

typedef struct _ScreenCast
{
  MetaDBusScreenCast *proxy;
} ScreenCast;

#define assert_cursor_position(stream, x, y) \
{ \
  g_assert_cmpint (stream->cursor_x, ==, (x)); \
  g_assert_cmpint (stream->cursor_y, ==, (y)); \
}

static inline CursorMode
cursor_mode_from_string (const char *name)
{
  if (strcmp (name, "hidden") == 0)
    return CURSOR_MODE_HIDDEN;
  else if (strcmp (name, "embedded") == 0)
    return CURSOR_MODE_EMBEDDED;
  else if (strcmp (name, "metadata") == 0)
    return CURSOR_MODE_METADATA;

  g_assert_not_reached ();
}

void release_pipewire (void);

void init_pipewire (void);

void stream_resize (Stream *stream,
                    int     width,
                    int     height);

void stream_wait_for_render (Stream *stream);

void stream_free (Stream *stream);

void session_notify_absolute_pointer (Session *session,
                                      Stream  *stream,
                                      double   x,
                                      double   y);

void session_add_seat_capability (Session                   *session,
                                  enum ei_device_capability  cap);

void session_remove_seat_capability (Session                   *session,
                                     enum ei_device_capability  cap);

void session_connect_to_eis (Session *session);

void session_ei_roundtrip (Session *session);

void session_start (Session *session);

void session_stop (Session *session);

Stream * session_record_virtual (Session    *session,
                                 int         width,
                                 int         height,
                                 CursorMode  cursor_mode);

Stream * session_record_monitor (Session    *session,
                                 const char *connector,
                                 CursorMode  cursor_mode);

const char * session_get_id (Session *session);

Session * session_new (MetaDBusRemoteDesktopSession *remote_desktop_session_proxy,
                       MetaDBusScreenCastSession    *screen_cast_session_proxy);

void session_free (Session *session);

Session * screen_cast_create_session (RemoteDesktop *remote_desktop,
                                      ScreenCast    *screen_cast);

RemoteDesktop * remote_desktop_new (void);

void remote_desktop_free (RemoteDesktop *remote_desktop);

ScreenCast * screen_cast_new (void);

void screen_cast_free (ScreenCast *screen_cast);
