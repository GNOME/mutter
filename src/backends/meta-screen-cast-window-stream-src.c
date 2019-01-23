/*
 * Copyright (C) 2018 Red Hat Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "backends/meta-screen-cast-window-stream-src.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-screen-cast-window.h"
#include "backends/meta-screen-cast-window-stream.h"
#include "compositor/meta-window-actor-private.h"

struct _MetaScreenCastWindowStreamSrc
{
  MetaScreenCastStreamSrc parent;

  MetaScreenCastWindow *screen_cast_window;

  unsigned long screen_cast_window_after_paint_handler_id;
  unsigned long screen_cast_window_destroyed_handler_id;
};

G_DEFINE_TYPE (MetaScreenCastWindowStreamSrc,
               meta_screen_cast_window_stream_src,
               META_TYPE_SCREEN_CAST_STREAM_SRC)

static MetaScreenCastWindowStream *
get_window_stream (MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastStreamSrc *src;
  MetaScreenCastStream *stream;

  src = META_SCREEN_CAST_STREAM_SRC (window_src);
  stream = meta_screen_cast_stream_src_get_stream (src);

  return META_SCREEN_CAST_WINDOW_STREAM (stream);
}

static MetaWindow *
get_window (MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastWindowStream *window_stream;

  window_stream = get_window_stream (window_src);

  return meta_screen_cast_window_stream_get_window (window_stream);
}

static int
get_stream_width (MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastWindowStream *window_stream;

  window_stream = get_window_stream (window_src);

  return meta_screen_cast_window_stream_get_width (window_stream);
}

static int
get_stream_height (MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastWindowStream *window_stream;

  window_stream = get_window_stream (window_src);

  return meta_screen_cast_window_stream_get_height (window_stream);
}

static gboolean
capture_into (MetaScreenCastWindowStreamSrc *window_src,
              uint8_t                       *data)
{
  MetaRectangle stream_rect;

  stream_rect.x = 0;
  stream_rect.y = 0;
  stream_rect.width = get_stream_width (window_src);
  stream_rect.height = get_stream_height (window_src);

  meta_screen_cast_window_capture_into (window_src->screen_cast_window,
                                        &stream_rect, data);

  return TRUE;
}

static void
meta_screen_cast_window_stream_src_get_specs (MetaScreenCastStreamSrc *src,
                                              int                     *width,
                                              int                     *height,
                                              float                   *frame_rate)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);

  *width = get_stream_width (window_src);
  *height = get_stream_height (window_src);
  *frame_rate = 60.0f;
}

static gboolean
meta_screen_cast_window_stream_src_get_videocrop (MetaScreenCastStreamSrc *src,
                                                  MetaRectangle           *crop_rect)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);
  MetaRectangle stream_rect;

  meta_screen_cast_window_get_frame_bounds (window_src->screen_cast_window,
                                            crop_rect);

  stream_rect.x = 0;
  stream_rect.y = 0;
  stream_rect.width = get_stream_width (window_src);
  stream_rect.height = get_stream_height (window_src);

  meta_rectangle_intersect (crop_rect, &stream_rect, crop_rect);

  return TRUE;
}

static void
meta_screen_cast_window_stream_src_stop (MetaScreenCastWindowStreamSrc *window_src)

{
  if (!window_src->screen_cast_window)
    return;


  if (window_src->screen_cast_window_after_paint_handler_id)
    g_signal_handler_disconnect (window_src->screen_cast_window,
                                 window_src->screen_cast_window_after_paint_handler_id);
  window_src->screen_cast_window_after_paint_handler_id = 0;

  if (window_src->screen_cast_window_destroyed_handler_id)
    g_signal_handler_disconnect (window_src->screen_cast_window,
                                 window_src->screen_cast_window_destroyed_handler_id);
  window_src->screen_cast_window_destroyed_handler_id = 0;
}

static void
screen_cast_window_after_paint (MetaScreenCastWindow          *screen_cast_window,
                                MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (window_src);

  meta_screen_cast_stream_src_maybe_record_frame (src);
}

static void
screen_cast_window_destroyed (MetaScreenCastWindow          *screen_cast_window,
                              MetaScreenCastWindowStreamSrc *window_src)
{
  meta_screen_cast_window_stream_src_stop (window_src);
  window_src->screen_cast_window = NULL;
}

static void
meta_screen_cast_window_stream_src_enable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);
  MetaWindowActor *window_actor;

  window_actor = meta_window_actor_from_window (get_window (window_src));
  if (!window_actor)
    return;

  window_src->screen_cast_window = META_SCREEN_CAST_WINDOW (window_actor);

  window_src->screen_cast_window_after_paint_handler_id =
    g_signal_connect_after (window_src->screen_cast_window,
                            "paint",
                            G_CALLBACK (screen_cast_window_after_paint),
                            window_src);

  window_src->screen_cast_window_destroyed_handler_id =
    g_signal_connect (window_src->screen_cast_window,
                      "destroy",
                      G_CALLBACK (screen_cast_window_destroyed),
                      window_src);
}

static void
meta_screen_cast_window_stream_src_disable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);

  meta_screen_cast_window_stream_src_stop (window_src);
}

static gboolean
meta_screen_cast_window_stream_src_record_frame (MetaScreenCastStreamSrc *src,
                                                 uint8_t                 *data)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);

  capture_into (window_src, data);

  return TRUE;
}

MetaScreenCastWindowStreamSrc *
meta_screen_cast_window_stream_src_new (MetaScreenCastWindowStream  *window_stream,
                                        GError                     **error)
{
  return g_initable_new (META_TYPE_SCREEN_CAST_WINDOW_STREAM_SRC, NULL, error,
                         "stream", window_stream,
                         NULL);
}

static void
meta_screen_cast_window_stream_src_init (MetaScreenCastWindowStreamSrc *window_src)
{
}

static void
meta_screen_cast_window_stream_src_class_init (MetaScreenCastWindowStreamSrcClass *klass)
{
  MetaScreenCastStreamSrcClass *src_class =
    META_SCREEN_CAST_STREAM_SRC_CLASS (klass);

  src_class->get_specs = meta_screen_cast_window_stream_src_get_specs;
  src_class->enable = meta_screen_cast_window_stream_src_enable;
  src_class->disable = meta_screen_cast_window_stream_src_disable;
  src_class->record_frame = meta_screen_cast_window_stream_src_record_frame;
  src_class->get_videocrop = meta_screen_cast_window_stream_src_get_videocrop;
}
