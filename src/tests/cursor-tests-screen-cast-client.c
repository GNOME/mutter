/*
 * Copyright (C) 2024 Red Hat Inc.
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

#include "tests/meta-ref-test-utils.h"

static MetaReftestFlag
reftest_flags_from_string (const char *flags)
{
  if (strcmp (flags, "update-ref") == 0)
    return META_REFTEST_FLAG_UPDATE_REF;
  g_assert_cmpstr (flags, ==, "");
  return META_REFTEST_FLAG_NONE;
}

static void
draw_cursor (cairo_t                *cr,
             struct spa_meta_cursor *spa_cursor)
{
  struct spa_meta_bitmap *spa_bitmap;
  cairo_surface_t *cursor_image;

  spa_bitmap = SPA_MEMBER (spa_cursor, spa_cursor->bitmap_offset,
                           struct spa_meta_bitmap);
  g_assert_nonnull (spa_bitmap);

  cursor_image =
    cairo_image_surface_create_for_data (SPA_MEMBER (spa_bitmap,
                                                     spa_bitmap->offset,
                                                     uint8_t),
                                         CAIRO_FORMAT_ARGB32,
                                         spa_bitmap->size.width,
                                         spa_bitmap->size.height,
                                         spa_bitmap->stride);
  cairo_surface_mark_dirty (cursor_image);

  cairo_set_source_surface (cr, cursor_image,
                            spa_cursor->position.x - spa_cursor->hotspot.x,
                            spa_cursor->position.y - spa_cursor->hotspot.y);
  cairo_paint (cr);
}

static cairo_surface_t *
screen_cast_adaptor_capture (gpointer adaptor_data)
{
  Stream *stream = adaptor_data;
  struct spa_buffer *spa_buffer;
  cairo_surface_t *source_image;
  cairo_surface_t *surface;
  struct spa_meta_cursor *spa_cursor;
  cairo_t *cr;

  g_assert_nonnull (stream->buffer);

  spa_buffer = stream->buffer->buffer;
  g_assert_nonnull (spa_buffer->datas[0].data);

  source_image =
    cairo_image_surface_create_for_data (spa_buffer->datas[0].data,
                                         CAIRO_FORMAT_ARGB32,
                                         stream->spa_format.size.width,
                                         stream->spa_format.size.height,
                                         spa_buffer->datas[0].chunk->stride);
  cairo_surface_mark_dirty (source_image);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        stream->spa_format.size.width,
                                        stream->spa_format.size.height);
  cr = cairo_create (surface);
  cairo_set_source_surface (cr, source_image, 0.0, 0.0);
  cairo_paint (cr);

  spa_cursor = spa_buffer_find_meta_data (spa_buffer, SPA_META_Cursor,
                                          sizeof (*spa_cursor));

  switch (stream->cursor_mode)
    {
    case CURSOR_MODE_HIDDEN:
    case CURSOR_MODE_EMBEDDED:
      g_assert_false (spa_meta_cursor_is_valid (spa_cursor));
      break;
    case CURSOR_MODE_METADATA:
      g_assert_true (spa_meta_cursor_is_valid (spa_cursor));
      draw_cursor (cr, spa_cursor);
      break;
    }

  cairo_destroy (cr);

  return surface;
}

int
main (int    argc,
      char **argv)
{
  ScreenCast *screen_cast;
  Session *session;
  Stream *stream;
  const char *test_name;
  int test_seq;
  CursorMode cursor_mode;
  MetaReftestFlag ref_test_flags;

  g_test_init (&argc, &argv, NULL);

  g_assert_cmpint (argc, ==, 5);

  test_name = argv[1];
  test_seq = atoi (argv[2]);
  cursor_mode = cursor_mode_from_string (argv[3]);
  ref_test_flags = reftest_flags_from_string (argv[4]);

  g_debug ("Verifying screen cast cursor mode %s for test case %s",
           argv[2], test_name);

  g_debug ("Initializing PipeWire");
  init_pipewire ();

  g_debug ("Creating screen cast session");
  screen_cast = screen_cast_new ();
  session = screen_cast_create_session (NULL, screen_cast);
  stream = session_record_monitor (session, NULL, cursor_mode);

  g_debug ("Starting screen cast stream");
  session_start (session);

  stream_wait_for_render (stream);

  meta_ref_test_verify (screen_cast_adaptor_capture,
                        stream,
                        test_name,
                        test_seq,
                        ref_test_flags);

  g_debug ("Stopping session");

  session_stop (session);
  stream_free (stream);
  session_free (session);
  screen_cast_free (screen_cast);

  release_pipewire ();

  g_debug ("Done");

  return EXIT_SUCCESS;
}
