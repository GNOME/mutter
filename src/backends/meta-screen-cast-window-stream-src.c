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
#include "backends/meta-screen-cast-window-stream.h"
#include "core/window-private.h"
#include "compositor/compositor-private.h"

struct _MetaScreenCastWindowStreamSrc
{
  MetaScreenCastStreamSrc parent;

  MetaWindowActor *actor;

  MetaRectangle crop_rect;

  unsigned long actor_painted_handler_id;
  unsigned long actor_destroyed_handler_id;
};

G_DEFINE_TYPE (MetaScreenCastWindowStreamSrc,
               meta_screen_cast_window_stream_src,
               META_TYPE_SCREEN_CAST_STREAM_SRC)

static MetaScreenCastWindowStream *
get_window_stream  (MetaScreenCastWindowStreamSrc *window_src)
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

static void
get_clip_rect (MetaScreenCastWindowStreamSrc *window_src,
               cairo_rectangle_int_t         *stream_rect,
               cairo_rectangle_int_t         *clip_rect)
{
  MetaWindow *window;
  MetaSurfaceActor *surface_actor;
  MetaShapedTexture *stex;
  cairo_rectangle_int_t buffer_rect;
  double scale_x, scale_y;

  window = get_window (window_src);
  meta_window_get_buffer_rect (window, &buffer_rect);

  surface_actor = meta_window_actor_get_surface (window_src->actor);
  stex = meta_surface_actor_get_texture (surface_actor);
  clutter_actor_get_scale (CLUTTER_ACTOR (stex), &scale_x, &scale_y);

  buffer_rect.x = (int) floor ((window->rect.x - buffer_rect.x) / scale_x);
  buffer_rect.y = (int) floor ((window->rect.y - buffer_rect.y) / scale_y);
  buffer_rect.width = (int) ceil (buffer_rect.width / scale_x);
  buffer_rect.height = (int) ceil (buffer_rect.height / scale_y);

  meta_rectangle_intersect (&buffer_rect, stream_rect, clip_rect);
}

static gboolean
capture_into (MetaScreenCastWindowStreamSrc *window_src,
              uint8_t                       *data)
{
  MetaSurfaceActor *surface_actor;
  cairo_surface_t *image;
  cairo_rectangle_int_t clip;
  cairo_rectangle_int_t stream_rect;
  uint8_t *cr_data;
  int cr_width, cr_height, cr_stride;
  int bpp = 4;

  surface_actor = meta_window_actor_get_surface (window_src->actor);
  if (!surface_actor)
    return FALSE;

  stream_rect.x = 0;
  stream_rect.y = 0;
  stream_rect.width = get_stream_width (window_src);
  stream_rect.height = get_stream_height (window_src);

  get_clip_rect (window_src, &stream_rect, &clip);
  image = meta_surface_actor_get_image (surface_actor, &clip);
  cr_stride = cairo_image_surface_get_stride (image);
  cr_data = cairo_image_surface_get_data (image);
  cr_width  = cairo_image_surface_get_width (image);
  cr_height = cairo_image_surface_get_height (image);

  if (cr_width < stream_rect.width || cr_height < stream_rect.height)
    {
      uint8_t *src, *dst;
      src = cr_data;
      dst = data;

      for (int i = 0; i < cr_height; i++)
        {
          memcpy (dst, src, cr_stride);
          if (cr_width < stream_rect.width)
            bzero (dst + cr_stride, (stream_rect.width * bpp) - cr_stride);

          src += cr_stride;
          dst += stream_rect.width * bpp;
        }

      for (int i = cr_height; i < stream_rect.height; i++)
        {
          bzero (dst, stream_rect.width * bpp);
          dst += stream_rect.width * bpp;
        }
    }
  else
    {
      memcpy (data, cr_data, cr_height * cr_stride);
    }

  cairo_surface_destroy (image);

  /* Update crop region */
  window_src->crop_rect.x = 0;
  window_src->crop_rect.y = 0;
  window_src->crop_rect.width = cr_width;
  window_src->crop_rect.height = cr_height;

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

  *crop_rect = window_src->crop_rect;

  return TRUE;
}

static void
window_actor_painted (ClutterActor                  *actor,
                      MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (window_src);

  meta_screen_cast_stream_src_maybe_record_frame (src);
}

static void
meta_screen_cast_window_stream_src_stop (MetaScreenCastWindowStreamSrc *window_src)
{
  if (window_src->actor_painted_handler_id)
    {
      g_signal_handler_disconnect (window_src->actor, window_src->actor_painted_handler_id);
      window_src->actor_painted_handler_id = 0;
    }
  if (window_src->actor_destroyed_handler_id)
    {
      g_signal_handler_disconnect (window_src->actor, window_src->actor_destroyed_handler_id);
      window_src->actor_destroyed_handler_id = 0;
    }
}

static void
meta_screen_cast_window_stream_src_enable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);
  MetaWindow *window;

  window = get_window (window_src);
  window_src->actor = meta_window_actor_from_window (window);
  window_src->crop_rect.x = 0;
  window_src->crop_rect.y = 0;
  window_src->crop_rect.width = get_stream_width (window_src);
  window_src->crop_rect.height = get_stream_height (window_src);

  window_src->actor_painted_handler_id =
    g_signal_connect_after (window_src->actor, "paint",
                            G_CALLBACK (window_actor_painted),
                            window_src);
  window_src->actor_destroyed_handler_id =
    g_signal_connect_swapped (window_src->actor, "destroy",
                              G_CALLBACK (meta_screen_cast_window_stream_src_stop),
                              window_src);
}

static void
meta_screen_cast_window_stream_src_disable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);

  meta_screen_cast_window_stream_src_stop (window_src);
}

static void
meta_screen_cast_window_stream_src_record_frame (MetaScreenCastStreamSrc *src,
                                                 uint8_t                 *data)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);

  capture_into (window_src, data);
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
