/*
 * Copyright (C) 2021 Red Hat Inc.
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

#include "config.h"

#include "tests/remote-desktop-utils.h"

static void
stream_wait_for_node (Stream *stream)
{
  while (!stream->pipewire_node_id)
    g_main_context_iteration (NULL, TRUE);
}

static void
stream_wait_for_cursor_position (Stream *stream,
                                 int     x,
                                 int     y)
{
  while (stream->cursor_x != x &&
         stream->cursor_y != y)
    g_main_context_iteration (NULL, TRUE);
}

static void
stream_wait_for_streaming (Stream *stream)
{
  g_debug ("Waiting for stream to stream");
  while (stream->state != PW_STREAM_STATE_STREAMING)
    g_main_context_iteration (NULL, TRUE);
}

int
main (int    argc,
      char **argv)
{
  RemoteDesktop *remote_desktop;
  ScreenCast *screen_cast;
  Session *session;
  Stream *stream;

  g_log_writer_default_set_use_stderr (TRUE);

  g_debug ("Initializing PipeWire");
  init_pipewire ();

  g_debug ("Creating screen cast session");
  remote_desktop = remote_desktop_new ();
  screen_cast = screen_cast_new ();
  session = screen_cast_create_session (remote_desktop, screen_cast);
  stream = session_record_virtual (session, 50, 40, CURSOR_MODE_METADATA);

  g_debug ("Starting screen cast stream");
  session_start (session);

  /* Check that the display server handles events being emitted too early. */
  session_notify_absolute_pointer (session, stream, 2, 3);

  /* Check that we receive the initial frame */
  g_debug ("Waiting for stream to be established");
  stream_wait_for_node (stream);
  stream_wait_for_streaming (stream);
  stream_wait_for_render (stream);
  session_notify_absolute_pointer (session, stream, 6, 5);
  session_notify_absolute_pointer (session, stream, 5, 6);

  g_debug ("Waiting for frame");
  stream_wait_for_render (stream);
  stream_wait_for_cursor_position (stream, 5, 6);
  g_assert_cmpint (stream->spa_format.size.width, ==, 50);
  g_assert_cmpint (stream->spa_format.size.height, ==, 40);

  /* Check that resizing works */
  g_debug ("Resizing stream");
  stream_resize (stream, 70, 60);
  while (TRUE)
    {
      stream_wait_for_render (stream);

      if (stream->spa_format.size.width == 70 &&
          stream->spa_format.size.height == 60)
        break;

      g_assert_cmpint (stream->spa_format.size.width, ==, 50);
      g_assert_cmpint (stream->spa_format.size.height, ==, 40);
    }

  /* Check that resizing works */
  stream_resize (stream, 60, 60);

  g_debug ("Stopping session");
  session_stop (session);

  stream_free (stream);
  session_free (session);
  screen_cast_free (screen_cast);
  remote_desktop_free (remote_desktop);

  release_pipewire ();

  g_debug ("Done");

  return EXIT_SUCCESS;
}
